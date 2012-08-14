#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
MODULE_LICENSE("GPL");

#define TTYHUB_VERSION "0.10 pre-alpha"

#define N_TTYHUB 29
#if N_TTYHUB >= NR_LDISCS
#error N_TTYHUB is larger than the maximum allowed value
#endif

static int max_subsys = 16;
module_param(max_subsys, int, 0);
MODULE_PARM_DESC(max_subsys, "Maximum number of TTYHUB subsystems");

static int probe_buf_size = 32;
module_param(probe_buf_size, int, 0);
MODULE_PARM_DESC(probe_buf_size, "Size of the TTYHUB receive probe buffer");

struct ttyhub_state {
        int recv_subsys;
        unsigned char *probed_subsystems;

        unsigned char *probe_buf;
        int probe_buf_consumed;
        int probe_buf_count;

        int cp_consumed;
};

struct ttyhub_subsystem {
        /* subsystem operations called by ttyhub */
        int (*open)(void); // TODO correct arguments for subsys ops
        void (*close)(void);
        int (*probe_data)(const unsigned char *, int);
        int (*probe_size)(const unsigned char *, int);
        int (*do_receive)(const unsigned char *, int);

        /* minimum bytes received before probing the submodule */
        int probe_data_minimum_bytes;
};


static struct ttyhub_subsystem **ttyhub_subsystems;
static spinlock_t ttyhub_subsystems_lock;

/*
 * Probe subsystems if they can identify a received data chunk.
 * The recv_subsys field and the array pointed to by probed_subsystems
 * of the ttyhub_state structure is changed according to the probe results,
 * but the probe buffer is not filled in case more data is needed.
 * This is a helper function for ttyhub_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 *
 * Returns:
 *  0   either a subsystem has identified the data or all subsystems have
 *      already been probed - state machine must continue in this call
 *  1   the received data has not been identified - wait for more data
 */
static int ttyhub_probe_subsystems(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        int i, j, subsys_remaining = 0;
        struct ttyhub_subsystem *subs;

        for (i=0; i < max_subsys; i++) {
                subs = ttyhub_subsystems[i];
                if (subs == NULL)
                        continue;
                if (state->probed_subsystems[i/8] & 1 << i%8)
                        continue;
                if (subs->probe_data_minimum_bytes > count) {
                        subsys_remaining = 1;
                        continue;
                }
                if (subs->probe_data(cp, count)) {
                        state->recv_subsys = i;
                        for (j=0; j < (max_subsys-1)/8 + 1; j++)
                                state->probed_subsystems[j] = 0;
                        return 0;
                }
                state->probed_subsystems[i/8] |= 1 << i%8;
        }

        if (!subsys_remaining) {
                state->recv_subsys = -2;
                for (j=0; j < (max_subsys-1)/8 + 1; j++)
                        state->probed_subsystems[j] = 0;
        }

        return subsys_remaining;
}

static int ttyhub_probe_subsystems_size(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        // TODO implement + doc
}

/*
 * Append data to probe buffer.
 * Buffer is packed before appending when parts of the data
 * have already been consumed.
 *
 * All locks to involved data structures are asssumed to be held already.
 *
 * Returns the number of bytes that were copied to the probe buffer.
 */
static int ttyhub_probebuf_push(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        int room, n;
        if (state->probe_buf_consumed) {
                /* probe buffer in use and partly consumed - pack */
                int i;
                int max = state->probe_buf_count - state->probe_buf_consumed;
                int offset = state->probe_buf_consumed;
                for (i=0; i < max; i++)
                        state->probe_buf[i] = state->probe_buf[i + offset];
                state->probe_buf_count -= offset;
                state->probe_buf_consumed = 0;
        }

        room = probe_buf_size - state->probe_buf_count;
        n = count > room ? room : count;
        memcpy(state->probe_buf + state->probe_buf_count, cp, n);
        state->probe_buf_count += n;
        state->cp_consumed += n;
        return n;
}

// TODO doc
static void ttyhub_get_recvd_data_head(struct ttyhub_state *state,
                        const unsigned char *cp, int count,
                        const unsigned char **out_cp, int *out_count)
{
        int probe_buf_fillstate = state->probe_buf_count -
                state->probe_buf_consumed;
        if (probe_buf_fillstate) {
                /* probe buffer in use */
                *out_cp = state->probe_buf + state->probe_buf_consumed;
                *out_count = probe_buf_fillstate;
        }
        else {
                /* probe buffer not in use */
                *out_cp = cp + state->cp_consumed;
                *out_count = count - state->cp_consumed;
        }
}

// TODO doc
static void ttyhub_recvd_data_consumed(struct ttyhub_state *state, int count)
{
        if (state->probe_buf_count - state->probe_buf_consumed)
                /* probe buffer in use */
                state->probe_buf_consumed += count;
        else
                /* probe buffer not in use */
                state->cp_consumed += count;
}

/* Line discipline open() operation */
static int ttyhub_open(struct tty_struct *tty)
{
        struct ttyhub_state *state;
        int err = -ENOBUFS;

        state = kmalloc(sizeof(*state), GFP_KERNEL);
        if (state == NULL)
                goto error_exit;

        state->recv_subsys = -1;

        /* allocate probe buffer */
        state->probe_buf = kmalloc(probe_buf_size, GFP_KERNEL);
        if (state->probe_buf == NULL)
                goto error_cleanup_state;
        state->probe_buf_consumed = 0;
        state->probe_buf_count = 0;

        /* allocate char array with 1 bit for each subsystem */
        state->probed_subsystems = kmalloc((max_subsys-1)/8 + 1, GFP_KERNEL);
        if (state->probed_subsystems == NULL)
                goto error_cleanup_probebuf;

        /* success */
        tty->disc_data = state;
        return 0;

error_cleanup_probebuf:
        kfree(state->probe_buf);
error_cleanup_state:
        kfree(state);
error_exit:
        return err;
}

/* Line discipline close() operation */
static void ttyhub_close(struct tty_struct *tty)
{
        struct ttyhub_state *state = tty->disc_data;

        if (state == NULL)
                return;

        if (state->probed_subsystems != NULL)
                kfree(state->probed_subsystems);
        if (state->probe_buf != NULL)
                kfree(state->probe_buf);
        kfree(state);
}

/* Line discipline ioctl() operation */
static int ttyhub_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

/*
 * Line discipline receive_buf() operation
 * Called by the hardware driver when new data arrives.
 *
 * Only one call of this function is active at a time so the state
 * machine needs no explicit lock.
 */
static void ttyhub_receive_buf(struct tty_struct *tty,
                        const unsigned char *cp, char *fp, int count)
{
        struct ttyhub_state *state = tty->disc_data;
        const unsigned char *r_cp;
        int r_count;

        // TODO conditional output if debug var set
        printk(KERN_INFO "ttyhub_receive_buf() called with count=%d\n", count);

        /* Receive state machine:
         * The relevant fields in the ttyhub_state struct are:
         *   1) recv_subsys
         *        values from 0 to (max_subsys-1):
         *              addressed subsystem is known, proceed with receiving
         *              data immediately
         *        value -1:
         *              addressed subsystem is unknown and has to be probed
         *        value -2:
         *              addressed subsystem is unknown and all registered
         *              subsystems have already been probed
         *   2) probed_subsystems
         *        This is a pointer to an unsigned char array containing one
         *        bit for every possible subsystem. When the addressed
         *        subsystem is unknown a set bit indicates that the subsystem
         *        has already been probed and should not be probed again.
         *   TODO describe probe_buf management related fields
         */

        /* when cp is read partially, this is used as an offset */
        state->cp_consumed = 0;

        /* when probe buffer is already partially filled append new incoming
           data to probe buffer */
        if (state->probe_buf_count)
                ttyhub_probebuf_push(state, cp, count);

        while (1) { // TODO lock subsystem spinlock
                if (state->recv_subsys == -1) {
                        /* this may change recv_subsys to -2 or a nonnegative
                           value which then gets processed immediately */
                        int status;
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        status = ttyhub_probe_subsystems(state, r_cp, r_count);
                        if (status) {
                                /* wait for more data to probe more subsystems */
                                if (state->probe_buf_count == 0)
                                        ttyhub_probebuf_push(state,
                                                r_cp, r_count);
                                        // TODO check num of bytes actually copied?
                                return;
                        }
                }
                if (state->recv_subsys == -2) {
                        int status;
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        status = ttyhub_probe_subsystems_size(state,
                                r_cp, r_count);
                        // TODO check return code and do something
                        // TODO when probe for size fails check if probe_buf is in use
                        //      and full
                        //          - if unused only copy to probe_buf and wait for more
                        //            data when there is less data than fits in the buffer
                        //            otherwise handle same as if full
                        //          - if full maybe try to drop based on tty rcv idle time,
                        //            but generally speaking, this is not good
                        //          - when there is still space in the probe_buf wait for
                        //            more data and then retry
                        //          - this is the only place where we have to check for a full buffer
                        //            because in the end we always come here (!!!CHECK!!!)
                }
                if (state->recv_subsys >= 0) {
                        int n;
                        struct ttyhub_subsystem *subs =
                                ttyhub_subsystems[state->recv_subsys];
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        n = subs->do_receive(r_cp, r_count);
                        if (n < 0) {
                                /* subsystem expects more data */
                                ttyhub_recvd_data_consumed(state, r_count);
                        }
                        else {
                                /* subsystem finished receiving */
                                ttyhub_recvd_data_consumed(state, n);
                                state->recv_subsys = -1;

                                /* if there is data remaining in the probe
                                   buffer as well as in cp fill up the buffer
                                   before probing the subsystems again */
                                if (state->probe_buf_count)
                                        ttyhub_probebuf_push(state, cp +
                                                state->cp_consumed, count -
                                                state->cp_consumed);
                        }

                        /* all data from cp and probe buffer consumed? */
                        if (state->cp_consumed == count &&
                                state->probe_buf_count ==
                                state->probe_buf_consumed)
                                return;
                }
        }
}

static void ttyhub_write_wakeup(struct tty_struct *tty)
{
        //int written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
        // TODO update buffer pointer, bytes_in_buffer
}

struct tty_ldisc_ops ttyhub_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "ttyhub",
        .open         = ttyhub_open,
        .close        = ttyhub_close,
        .ioctl        = ttyhub_ioctl,
        .receive_buf  = ttyhub_receive_buf,
        .write_wakeup = ttyhub_write_wakeup
};

/* module init/exit functions */
static int __init ttyhub_init(void)
{
        int status;

        if (max_subsys < 2)
                max_subsys = 2;

        if (probe_buf_size < 16)
                probe_buf_size = 16;

        printk(KERN_INFO "TTYHUB: version %s, max. subsystems = %d, probe "
                "bufsize = %d\n", TTYHUB_VERSION, max_subsys, probe_buf_size);

        /* allocate space for pointers to subsystems and init semaphore */
        ttyhub_subsystems = kzalloc(
                sizeof(struct ttyhub_subsystem *) * max_subsys,
                GFP_KERNEL);
        if (ttyhub_subsystems == NULL)
                return -ENOMEM;
        spin_lock_init(&ttyhub_subsystems_lock);

        /* register line discipline */
        status = tty_register_ldisc(N_TTYHUB, &ttyhub_ldisc); // TODO dynamic LDISC nr
        if (status != 0) {
                kfree(ttyhub_subsystems);
                printk(KERN_ERR "TTYHUB: can't register line discipline "
                        "(err = %d)\n", status);
        }
        return status;
}

static void __exit ttyhub_exit(void)
{
        int status;

        status = tty_unregister_ldisc(N_TTYHUB);
        if (status != 0)
                printk(KERN_ERR "TTYHUB: can't unregister line "
                        "discipline (err = %d)\n", status);

        if (ttyhub_subsystems != NULL)
                kfree(ttyhub_subsystems);
}

module_init(ttyhub_init);
module_exit(ttyhub_exit);


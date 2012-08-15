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
        unsigned char *enabled_subsystems;
        int discard_bytes_remaining;

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

        /* nonzero while subsystem may not be unregistered */
        int refcount;
        // TODO increase refcount when subsystem is enabled on a tty
        //      decrease refcount when subsystem is disabled on a tty (or closed)
};


static struct ttyhub_subsystem **ttyhub_subsystems;
static spinlock_t ttyhub_subsystems_lock;

// TODO doc
int ttyhub_register_subsystem(struct ttyhub_subsystem *subs)
{
        unsigned long flags;
        int i;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                if (ttyhub_subsystems[i] == NULL)
                        break;
        }
        if (i == max_subsys)
                /* no more space for this subsystem */
                goto error_unlock;

        ttyhub_subsystems[i] = subs;
        subs->refcount = 0;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return i;

error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return -1;
}
EXPORT_SYMBOL_GPL(ttyhub_register_subsystem);

int ttyhub_unregister_subsystem(int index)
{
        unsigned long flags;
        struct ttyhub_subsystem *subs;

        if (index >= max_subsys  || index < 0)
                return -1;

        subs = ttyhub_subsystems[index];
        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        if (subs == NULL)
                goto error_unlock;
        if (subs->refcount != 0)
                goto error_unlock;
        ttyhub_subsystems[index] = NULL;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return 0;

error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return -1;
}
EXPORT_SYMBOL_GPL(ttyhub_unregister_subsystem);
/*
 * Probe subsystems if they can identify a received data chunk.
 * The recv_subsys field and the array pointed to by probed_subsystems
 * of the ttyhub_state structure is changed according to the probe results,
 * but the probe buffer is not filled in case more data is needed.
 * This is a helper function for ttyhub_receive_buf().
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while searching
 *      for an entry to probe, but not while probing the subsystem.
 *
 * Returns:
 *  0   either a subsystem has identified the data or all subsystems have
 *      already been probed - state machine must continue in this call
 *  1   the received data has not been identified - wait for more data
 */
static int ttyhub_probe_subsystems(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        unsigned long flags;
        int i, j, subsys_remaining = 0;
        struct ttyhub_subsystem *subs;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                subs = ttyhub_subsystems[i];
                if (subs == NULL)
                        continue;
                if (!(state->enabled_subsystems[i/8] & 1 << i%8))
                        continue;
                if (state->probed_subsystems[i/8] & 1 << i%8)
                        continue;
                if (subs->probe_data_minimum_bytes > count) {
                        subsys_remaining = 1;
                        continue;
                }
                spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
                if (subs->probe_data(cp, count)) {
                        /* data identified by subsystem */
                        state->recv_subsys = i;
                        for (j=0; j < (max_subsys-1)/8 + 1; j++)
                                state->probed_subsystems[j] = 0;
                        return 0;
                }
                state->probed_subsystems[i/8] |= 1 << i%8;
                spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        }

        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        if (!subsys_remaining) {
                state->recv_subsys = -2;
                for (j=0; j < (max_subsys-1)/8 + 1; j++)
                        state->probed_subsystems[j] = 0;
        }

        return subsys_remaining;
}

/*
 * Probe subsystems if they can identify the size of a received data chunk.
 * The recv_subsys and discard_bytes_remaining fields of the ttyhub_state 
 * structure are changed according to the probe results, but the probe buffer
 * is not filled in case more data is needed.
 * This is a helper function for ttyhub_receive_buf().
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while searching
 *      for an entry to probe, but not while probing the subsystem.
 *
 * Returns:
 *  0   either a subsystem has identified the size or the probe buffer is
 *      full - state machine must continue in this call
 *  1   the received data has not been identified and there is still space
 *      in the probe buffer left - wait for more data
 */
// TODO when timed drop packet mode is implemented change documentation
//      (an additional field in the state is changed)
static int ttyhub_probe_subsystems_size(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        unsigned long flags;
        int i, status, probe_buf_room;
        struct ttyhub_subsystem *subs;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                subs = ttyhub_subsystems[i];
                if (subs == NULL)
                        continue;
                if (!(state->enabled_subsystems[i/8] & 1 << i%8))
                        continue;
                spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
                status = subs->probe_size(cp, count);
                if (status > 0) {
                        /* size recognized */
                        state->recv_subsys = -3;
                        state->discard_bytes_remaining = status;
                        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
                        return 0;
                }
                else if (status < 0) {
                        // TODO size not recognized but subsystem can identify
                        //      end of data - implement! (set recv_subsys to i)
                        //      WHEN RETURNING DONT FORGET TO UNLOCK
                }
                spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        }
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);

        probe_buf_room = probe_buf_size - state->probe_buf_count +
                state->probe_buf_consumed;
        if ((state->probe_buf_count - state->probe_buf_consumed == 0 &&
                        count >= probe_buf_size) || probe_buf_room == 0) {
                /* no more space in the probe buffer to wait for more data (or
                   probe buffer is unused but received data is larger than
                   the probe buffer) */
                // TODO activate timed drop packet mode
                return 0;
        }

        /* wait for more data - there is still space in the probe buffer */
        return 1;
}

/*
 * Append data to probe buffer.
 * Buffer is packed before appending when parts of the data
 * have already been consumed.
 * This is a helper function for ttyhub_receive_buf().
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
                int offset = state->probe_buf_consumed;
                int max = state->probe_buf_count - offset;
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

/*
 * Get pointer to and length of received data.
 * This gets either a pointer to the probe buffer read head (when it is at
 * least partially filled) or a pointer to unread data in cp. The amount of
 * consumed data is considered for both situations.
 * This is a helper function for ttyhub_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 */
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

/*
 * Mark received data as consumed - advance pointers for the next read access.
 * This is a helper function for ttyhub_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 */
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
        state->discard_bytes_remaining = 0;

        /* allocate probe buffer */
        state->probe_buf = kmalloc(probe_buf_size, GFP_KERNEL);
        if (state->probe_buf == NULL)
                goto error_cleanup_state;
        state->probe_buf_consumed = 0;
        state->probe_buf_count = 0;

        /* allocate 2x char array with 1 bit per subsystem each */
        state->probed_subsystems = kzalloc(2*((max_subsys-1)/8+1), GFP_KERNEL);
        if (state->probed_subsystems == NULL)
                goto error_cleanup_probebuf;
        state->enabled_subsystems = state->probed_subsystems +
                (max_subsys-1)/8 + 1;

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
         *              subsystems have already been probed - probe for size
         *        value -3:
         *              addressed subsystem is unknown, size of packet has
         *              been recognized
         *   2) probed_subsystems
         *        This is a pointer to an unsigned char array containing one
         *        bit for every possible subsystem. When the addressed
         *        subsystem is unknown a set bit indicates that the subsystem
         *        has already been probed and should not be probed again.
         *   3) discard_bytes_remaining
         *        When recv_subsys is -3 this stores the number of bytes to
         *        be discarded. Decremented after data has been received.
         *   TODO describe probe_buf management related fields
         * The state machine continues until either...
         *   ...more data is needed for probing
         *      --> received data is kept in the probe buffer between calls
         *    OR
         *   ...the probe buffer and cp are completely consumed
         */

        /* when cp is read partially, this is used as an offset */
        state->cp_consumed = 0;

        /* when probe buffer is already partially filled append new incoming
           data to probe buffer */
        if (state->probe_buf_count)
                ttyhub_probebuf_push(state, cp, count);

        while (1) { // TODO lock subsystem spinlock
                if (state->recv_subsys == -1) {
                        int status;
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        status = ttyhub_probe_subsystems(state, r_cp, r_count);
                        if (status) {
                                /* wait for data to probe more subsystems */
                                if (state->probe_buf_count == 0)
                                        ttyhub_probebuf_push(state,
                                                r_cp, r_count);
                                        // TODO check num of bytes actually copied?
                                return;
                        }
                }
                if (state->recv_subsys == -2) {
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        if (ttyhub_probe_subsystems_size(state,
                                                r_cp, r_count)) {
                                /* wait for data to probe more subsystems */
                                if (state->probe_buf_count == 0)
                                        ttyhub_probebuf_push(state,
                                                r_cp, r_count);
                        }
                }
                if (state->recv_subsys == -3) {
                        int n;
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        n = r_count > state->discard_bytes_remaining ?
                                state->discard_bytes_remaining : r_count;
                        ttyhub_recvd_data_consumed(state, n);
                        state->discard_bytes_remaining -= n;
                        if (state->discard_bytes_remaining == 0)
                                state->recv_subsys = -1;
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

        /* allocate space for pointers to subsystems and init lock */
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


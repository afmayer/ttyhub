#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
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
};

struct ttyhub_subsystem {
        /* subsystem operations called by ttyhub */
        int (*open)(void); // TODO correct arguments for subsys ops
        void (*close)(void);
        int (*probe_data)(void);
        int (*probe_size)(void);
        int (*do_receive)(void);

        /* minimum bytes received before probing the submodule */
        int probe_data_minimum_bytes;
};


static struct ttyhub_subsystem **ttyhub_subsystems;
static struct semaphore ttyhub_subsystems_sem;

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
 *  1   the received data has not been identified but may be later with
 *      more data - probe buffer has to be filled
 */
static int ttyhub_probe_subsystems(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        // TODO implement according to documentation
}

static int ttyhub_probe_subsystems_size(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        // TODO implement + doc
}

static int ttyhub_probebuf_push(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        // TODO implement + doc
        //  (1) probe_buf not yet in use
        //      we need to preserve current buffer for next call --> copy
        //  (2) probe_buf already in use
}

static void ttyhub_get_recvd_data_head(struct ttyhub_state *state,
                        const unsigned char *cp, int count,
                        const unsigned char **out_cp, int *out_count)
{
        // TODO
        //  (1) probe_buf not in use
        //      copy the values of cp and count to *out_cp and *out_count
        //  (2) probe_buf not in use, but parts of cp consumed
        //      *out_cp = cp + consumed_bytes
        //      *out_count = count - consumed_bytes
        //  (3) probe_buf in use (and maybe already partly consumed)
        //      copy a pointer to the first unread byte in the probe buffer to *out_cp
        //      and the number of valid bytes from that point on to *out_count
        //      !!!CAUTION!!! IF THE PROBE BUFFER CONSTANTLY GETS ONLY PARTIALLY CONSUMED
        //                    IT MIGHT RUN TO ITS END - SO COMPACT IT EVERYTIME ONLY
        //                    A PART GETS CONSUMED
        //  In case (3), we should copy newly received data to the probe_buf when it is
        //  already in use! Update of consumed_bytes neccessary. When the buffer is full
        //  and then consumed there are still received bytes remaining in
        //  the cp buffer (count - consumed_bytes).
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

        /* receive probe buffer */
        state->probe_buf = kmalloc(probe_buf_size, GFP_KERNEL);
        if (state->probe_buf == NULL)
                goto error_cleanup_state;

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
 * Line discipline ioctl() operation
 * Called by the hardware driver when new data arrives.
 *
 * Only one call of this function is active at a time so the state
 * machine needs no explicit lock.
 */
static void ttyhub_receive_buf(struct tty_struct *tty,
                        const unsigned char *cp, char *fp, int count)
{
        struct ttyhub_state *state = tty->disc_data;
        int status;
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
        while (1) { // TODO lock subsystem semaphore
                if (state->recv_subsys == -1) {
                        /* this may change recv_subsys to -2 or a nonnegative
                           value which then gets processed in the same call */
                        ttyhub_get_recvd_data_head(state, cp, count,
                                                &r_cp, &r_count);
                        status = ttyhub_probe_subsystems(state, r_cp, r_count);
                        if (status) {
                                /* not enough data to probe all subsystems */
                                status = ttyhub_probebuf_push(state,
                                        cp, count);
                                return;
                        }
                }
                if (state->recv_subsys == -2) {
                        // TODO use ttyhub_get_recvd_data_head()
                        status = ttyhub_probe_subsystems_size(state,
                                cp, count);
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
                        struct ttyhub_subsystem *subs =
                                ttyhub_subsystems[state->recv_subsys];
                        // TODO use ttyhub_get_recvd_data_head()
                        status = subs->do_receive();
                        // TODO subfunction to be called after consumption of data:
                        //      if probe buffer is in use update related data
                        //      fields in ttyhub_state structure and compact the
                        //      buffer it if only a part of the data has been consumed
                        if (status < 0) {
                                /* subsystem expects more data */
                                return;
                        } else if (status == 0) {
                                /* subsystem expects no more data */
                                state->recv_subsys = -1;
                                return;
                        } else {
                                /* no more data expected, bytes remain */
                                state->recv_subsys = -1;
                                // TODO copy to probe buf and proceed
                        }
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

        printk(KERN_INFO "TTYHUB: version %s, max. subsystems = %d, probe bufsize"
                " = %d\n", TTYHUB_VERSION, max_subsys, probe_buf_size);

        /* allocate space for pointers to subsystems and init semaphore */
        ttyhub_subsystems = kzalloc(
                sizeof(struct ttyhub_subsystem *) * max_subsys,
                GFP_KERNEL);
        if (ttyhub_subsystems == NULL)
                return -ENOMEM;
        sema_init(&ttyhub_subsystems_sem, 1);

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


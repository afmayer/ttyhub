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
        unsigned char *probe_buf;
        unsigned char *probed_subsystems;
};

struct ttyhub_subsystem {
        /* subsystem operations called by ttyhub */
        int (*open)(void); // TODO correct arguments for subsys ops
        void (*close)(void);
        int (*probe_data)(void);
        int (*probe_size)(void);
        int (*do_receive)(void);
};


static struct ttyhub_subsystem **ttyhub_subsystems;
static struct semaphore ttyhub_subsystems_sem;

/* line discipline operations */
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

static int ttyhub_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

static void ttyhub_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                        char *fp, int count)
{
        struct ttyhub_state *state = tty->disc_data;

        // TODO conditional output if debug var set
        printk(KERN_INFO "ttyhub_receive_buf() called with count=%d\n", count);
}

static void ttyhub_write_wakeup(struct tty_struct *tty)
{
        int written;

        //written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
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


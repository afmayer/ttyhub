#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/slab.h>
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

struct ttyhub_state
{
        int recv_subsys;
        unsigned char *probe_buf;
};

static int ttyhub_open(struct tty_struct *tty)
{
        struct ttyhub_state *state;
        int err = -ENOBUFS;

        state = kmalloc(sizeof(*state), GFP_KERNEL);
        if (state == NULL)
                goto error_exit;
        state->recv_subsys = -1;
        state->probe_buf = kmalloc(probe_buf_size, GFP_KERNEL);
        if (state->probe_buf == NULL)
                goto error_cleanup_state;

        /* success */
        tty->disc_data = state;
        return 0;

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

        if (state->probe_buf != NULL)
                kfree(state->probe_buf);
        kfree(state);
}

static int ttyhub_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

static int ttyhub_hangup(struct tty_struct *tty)
{
        ttyhub_close(tty);
        return 0;
}

static void ttyhub_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                        char *fp, int count)
{
        // TODO conditional output if debug var set
        printk(KERN_INFO "ttyhub_receive_buf() called with count=%d\n", count);

        while (count--)
        {
                // TODO receive char by char
        }
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
        .hangup       = ttyhub_hangup,
        .receive_buf  = ttyhub_receive_buf,
        .write_wakeup = ttyhub_write_wakeup
};

static int ttyhub_init(void)
{
        int status;

        if (max_subsys < 2)
                max_subsys = 2;

        if (probe_buf_size < 16)
                probe_buf_size = 16;

        printk(KERN_INFO "TTYHUB: version %s, max. subsystems = %d, probe bufsize"
                " = %d\n", TTYHUB_VERSION, max_subsys, probe_buf_size);
        status = tty_register_ldisc(N_TTYHUB, &ttyhub_ldisc);
        return 0;
}

static void ttyhub_exit(void)
{
        int status;

        printk(KERN_INFO "TTYHUB exiting\n");
        status = tty_unregister_ldisc(N_TTYHUB);
        if (status != 0)
                printk(KERN_ERR "TTYHUB: can't unregister line "
                        "discipline (err = %d)\n", status);
}

module_init(ttyhub_init);
module_exit(ttyhub_exit);


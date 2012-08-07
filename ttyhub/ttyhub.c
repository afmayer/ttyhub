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

static int maxsubsys = 16;
module_param(maxsubsys, int, 0);
MODULE_PARM_DESC(maxsubsys, "Maximum number of TTYHUB subsystems");

struct ttyhub
{
        char txbuf[32];
        char rxbuf[32]; // TODO alloc buffers, size configurable
};

static int ttyhub_open(struct tty_struct *tty)
{
        struct ttyhub *data;
        int err = -ENOBUFS;

        data = kmalloc(sizeof(*data), GFP_KERNEL);
        if (data == NULL)
                goto error_exit;

        /* success */
        return 0;

error_exit:
        return err;
}

static void ttyhub_close(struct tty_struct *tty)
{
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

        if (maxsubsys < 2)
                maxsubsys = 2;

        printk(KERN_INFO "TTYHUB: version %s, max. subsystems = %d\n",
                TTYHUB_VERSION, maxsubsys);
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


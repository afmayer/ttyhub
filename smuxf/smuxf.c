#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/slab.h>
MODULE_LICENSE("GPL");

#define N_SMUXF 29
#if N_SMUXF >= NR_LDISCS
#error N_SMUXF is larger than the maximum allowed value
#endif

struct smuxf
{
        char txbuf[32];
        char rxbuf[32]; // TODO alloc buffers, size configurable
};

static int smuxf_open(struct tty_struct *tty)
{
        struct smuxf *data;
        int err = -ENOBUFS;

        data = kmalloc(sizeof(*data), GFP_KERNEL);
        if (data == NULL)
                goto error_exit;

        /* success */
        return 0;

error_exit:
        return err;
}

static void smuxf_close(struct tty_struct *tty)
{
}

static int smuxf_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

static int smuxf_hangup(struct tty_struct *tty)
{
        smuxf_close(tty);
        return 0;
}

static void smuxf_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                        char *fp, int count)
{
        while (count--)
        {
                // TODO receive char by char
        }
}

static void smuxf_write_wakeup(struct tty_struct *tty)
{
        int written;

        //written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
        // TODO update buffer pointer, bytes_in_buffer
}

struct tty_ldisc_ops smuxf_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "smuxf",
        .open         = smuxf_open,
        .close        = smuxf_close,
        .ioctl        = smuxf_ioctl,
        .hangup       = smuxf_hangup,
        .receive_buf  = smuxf_receive_buf,
        .write_wakeup = smuxf_write_wakeup
};

static int smuxf_init(void)
{
        int status;

        printk(KERN_INFO "SMUXF initialization start\n");
        status = tty_register_ldisc(N_SMUXF, &smuxf_ldisc);
        return 0;
}

static void smuxf_exit(void)
{
        int status;

        printk(KERN_INFO "SMUXF exiting\n");
        status = tty_unregister_ldisc(N_SMUXF);
        if (status != 0)
                printk(KERN_ERR "SERHUB: can't unregister line "
                        "discipline (err = %d)\n", status);
}

module_init(smuxf_init);
module_exit(smuxf_exit);


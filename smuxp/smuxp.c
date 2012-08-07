#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/slab.h>
MODULE_LICENSE("GPL");

#define N_SMUXP 29
#if N_SMUXP >= NR_LDISCS
#error N_SMUXP is larger than the maximum allowed value
#endif

struct smuxp
{
        char txbuf[32];
        char rxbuf[32]; // TODO alloc buffers, size configurable
};

static int smuxp_open(struct tty_struct *tty)
{
        struct smuxp *data;
        int err = -ENOBUFS;

        data = kmalloc(sizeof(*data), GFP_KERNEL);
        if (data == NULL)
                goto error_exit;

        /* success */
        return 0;

error_exit:
        return err;
}

static void smuxp_close(struct tty_struct *tty)
{
}

static int smuxp_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

static int smuxp_hangup(struct tty_struct *tty)
{
        smuxp_close(tty);
        return 0;
}

static void smuxp_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                        char *fp, int count)
{
        while (count--)
        {
                // TODO receive char by char
        }
}

static void smuxp_write_wakeup(struct tty_struct *tty)
{
        int written;

        //written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
        // TODO update buffer pointer, bytes_in_buffer
}

struct tty_ldisc_ops smuxp_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "smuxp",
        .open         = smuxp_open,
        .close        = smuxp_close,
        .ioctl        = smuxp_ioctl,
        .hangup       = smuxp_hangup,
        .receive_buf  = smuxp_receive_buf,
        .write_wakeup = smuxp_write_wakeup
};

static int smuxp_init(void)
{
        int status;

        printk(KERN_INFO "SMUXP initialization start\n");
        status = tty_register_ldisc(N_SMUXP, &smuxp_ldisc);
        return 0;
}

static void smuxp_exit(void)
{
        printk(KERN_INFO "SMUXP exiting\n");
}

module_init(smuxp_init);
module_exit(smuxp_exit);


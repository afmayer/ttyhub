#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/slab.h>
MODULE_LICENSE("GPL");

#define N_SERHUB 29
#if N_SERHUB >= NR_LDISCS
#error N_SERHUB is larger than the maximum allowed value
#endif

struct serhub
{
        char txbuf[32];
        char rxbuf[32]; // TODO alloc buffers, size configurable
};

static int serhub_open(struct tty_struct *tty)
{
        struct serhub *data;
        int err = -ENOBUFS;

        data = kmalloc(sizeof(*data), GFP_KERNEL);
        if (data == NULL)
                goto error_exit;

        /* success */
        return 0;

error_exit:
        return err;
}

static void serhub_close(struct tty_struct *tty)
{
}

static int serhub_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        return -ENOTTY;
}

static int serhub_hangup(struct tty_struct *tty)
{
        serhub_close(tty);
        return 0;
}

static void serhub_receive_buf(struct tty_struct *tty, const unsigned char *cp,
                        char *fp, int count)
{
        while (count--)
        {
                // TODO receive char by char
        }
}

static void serhub_write_wakeup(struct tty_struct *tty)
{
        int written;

        //written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
        // TODO update buffer pointer, bytes_in_buffer
}

struct tty_ldisc_ops serhub_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "serhub",
        .open         = serhub_open,
        .close        = serhub_close,
        .ioctl        = serhub_ioctl,
        .hangup       = serhub_hangup,
        .receive_buf  = serhub_receive_buf,
        .write_wakeup = serhub_write_wakeup
};

static int serhub_init(void)
{
        int status;

        printk(KERN_INFO "SERHUB initialization start\n");
        status = tty_register_ldisc(N_SERHUB, &serhub_ldisc);
        return 0;
}

static void serhub_exit(void)
{
        int status;

        printk(KERN_INFO "SERHUB exiting\n");
        status = tty_unregister_ldisc(N_SERHUB);
        if (status != 0)
                printk(KERN_ERR "SERHUB: can't unregister line "
                        "discipline (err = %d)\n", status);
}

module_init(serhub_init);
module_exit(serhub_exit);


#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
MODULE_LICENSE("GPL");

#define N_SMUXP 29
#if N_SMUXP >= NR_LDISCS
#error N_SMUXP is larger than the maximum allowed value
#endif

static int smuxp_open(struct tty_struct *tty)
{
        return 0;
}

static void smuxp_close(struct tty_struct *tty)
{
}

static int smuxp_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
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
        //.hangup       = smuxp_hangup,
        .ioctl        = smuxp_ioctl,
        .receive_buf  = smuxp_receive_buf,
        .write_wakeup = smuxp_write_wakeup
};

static int smuxp_init(void)
{
        int status;

        status = tty_register_ldisc(N_SMUXP, &smuxp_ldisc);
        printk(KERN_INFO "SMUXP initialization start.\n");
        return 0;
}

static void smuxp_exit(void)
{
        printk(KERN_INFO "SMUXP exiting.\n");
}

module_init(smuxp_init);
module_exit(smuxp_exit);


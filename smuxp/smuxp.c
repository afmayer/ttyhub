#include <linux/init.h>
#include <linux/module.h>
MODULE_LICENSE("GPL");

struct tty_ldisc_ops smuxp_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "smuxp",
        .open         = smuxp_open,
        .close        = smuxp_close,
        .hangup       = smuxp_hangup,
        .ioctl        = smuxp_ioctl,
        .receive_buf  = smuxp_receive_buf,
        .write_wakeup = smuxp_write_wakeup
};

static int smuxp_init(void)
{
        int status;

        status = tty_register_ldisc(N_, &smuxp_ldisc);
        printk(KERN_ALERT "SMUXP initialization start.\n");
        return 0;
}

static void smuxp_exit(void)
{
        printk(KERN_ALERT "SMUXP exiting.\n");
}

module_init(smuxp_init);
module_exit(smuxp_exit);


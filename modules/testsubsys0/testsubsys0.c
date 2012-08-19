#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include "ttyhub.h"
MODULE_LICENSE("GPL");

struct testsubsys0_data {
        int dummy;
};

int subsys_number = -1;
struct ttyhub_subsystem subs;

int testsubsys0_attach(void **data, struct tty_struct *tty)
{
        struct testsubsys0_data **d = (struct testsubsys0_data **)data;
        (void)d;

        printk(KERN_INFO "testsubsys0: invoked attach()\n");

        return 0;
}

void testsubsys0_detach(void *data)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        (void)d;
}

int testsubsys0_probe_data(void *data, const unsigned char *cp, int count)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked probe_data() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        if (cp[0] == 'a' && cp[1] == 'b' && cp[2] == 'c' && cp[3] == 'd') {
                /* data recognized */
                printk("testsubsys0: data recognized\n");
                return 1;
        }

        /* data not recognized */
        return 0;
}

int testsubsys0_probe_size(void *data, const unsigned char *cp, int count)
{
        int size = 0;
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked probe_size() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        if (count > 16) {
                /* size recognized - use 17th char in buf for size calc */
                size = cp[16] - 'A';
                if (size <= 0)
                        size = count;
                printk("testsubsys0: size recognized as %d\n", size);
        }

        return size;
}

int testsubsys0_do_receive(void *data, const unsigned char *cp, int count)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked do_receive() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        return count;
}

/* module init/exit functions */
static int __init testsubsys0_init(void)
{
        int status;
        printk(KERN_INFO "testsubsys0: initializing\n");

        subs.name = "testsubsys0";
        subs.owner = THIS_MODULE;
        subs.attach = testsubsys0_attach;
        subs.detach = testsubsys0_detach;
        subs.probe_data = testsubsys0_probe_data;
        subs.probe_size = testsubsys0_probe_size;
        subs.do_receive = testsubsys0_do_receive;
        subs.probe_data_minimum_bytes = 4;

        status = ttyhub_register_subsystem(&subs);
        if (status < 0) {
                printk(KERN_ERR "testsubsys0: could not register subsystem\n");
                return -EINVAL;
        }
        subsys_number = status;
        return 0;
}

static void __exit testsubsys0_exit(void)
{
        int status = 0;

        if (subsys_number >= 0)
                status = ttyhub_unregister_subsystem(subsys_number);

        if (status != 0)
                printk("testsubsys0: could not unregister subsystem\n");
}

module_init(testsubsys0_init);
module_exit(testsubsys0_exit);


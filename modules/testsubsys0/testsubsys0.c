#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
MODULE_LICENSE("GPL");

// TODO BEGIN OF DATA THAT SHOULD GO TO A HEADER FILE

struct ttyhub_subsystem { // TODO move to header file
        char *name;
        struct module *owner;

        /* subsystem operations called by ttyhub */
        int (*attach)(void **, struct tty_struct *);
        void (*detach)(void *);
        int (*probe_data)(void *,const unsigned char *, int);
        int (*probe_size)(void *, const unsigned char *, int);
        int (*do_receive)(void *, const unsigned char *, int);

        /* minimum bytes received before probing the submodule */
        int probe_data_minimum_bytes;

        /* nonzero while subsystem may not be unregistered - counts how many
           ttys have this subsystem enabled */
        int enabled_refcount;

        /* nonzero while the attach() operation is called - prevents races when
           enabling a subsystem multiple times at once */
        int enable_in_progress;
};

int ttyhub_register_subsystem(struct ttyhub_subsystem *);
int ttyhub_unregister_subsystem(int);

// TODO END OF DATA THAT SHOULD GO TO A HEADER FILE

struct testsubsys0_data {
        int dummy;
};

int subsys_number = -1;
struct ttyhub_subsystem subs;

int testsubsys0_attach(void **data, struct tty_struct *tty)
{
        struct testsubsys0_data **d = (struct testsubsys0_data **)data;
        (void)d;

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

        if (cp[0] == 'a' && cp[1] == 'b' && cp[2] == 'c' && cp[3] == 'd')
                /* data recognized */
                return 1;

        /* data not recognized */
        return 0;
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


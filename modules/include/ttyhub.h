#ifndef _TTYHUB_H
#define _TTYHUB_H

struct ttyhub_subsystem {
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

extern int ttyhub_register_subsystem(struct ttyhub_subsystem *subs);
extern int ttyhub_unregister_subsystem(int index);

#endif /* _TTYHUB_H */


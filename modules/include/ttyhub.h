#ifndef _TTYHUB_H
#define _TTYHUB_H
/* ttyhub
 * Copyright (c) 2013 Alexander F. Mayer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/tty.h>

struct ttyhub_subsystem {
        const char *name;
        struct module *owner;

        /* subsystem operations called by ttyhub */
        int (*attach)(void **, struct tty_struct *);
        void (*detach)(void *);
        int (*probe_data)(void *, const unsigned char *, int);
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


/* ttyhub test subsystem 0
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include "ttyhub.h"
MODULE_AUTHOR("Alexander F. Mayer");
MODULE_LICENSE("GPL");

struct testsubsys0_data {
        struct tty_struct *tty;
        int receive_remain;
        int receive_until_marker_mode;
};

static int subsys_number = -1;
static struct ttyhub_subsystem subs;

int testsubsys0_attach(void **data, struct tty_struct *tty)
{
        struct testsubsys0_data **d = (struct testsubsys0_data **)data;
        (void)d;

        printk("testsubsys0: allocate %d bytes for state\n", sizeof(**d));
        *d = kmalloc(sizeof(**d), GFP_KERNEL);
        if (*d == NULL) {
                printk(KERN_ERR "testsubsys0: can't allocate memory for state\n");
                return -ENOMEM;
        }

        (*d)->tty = tty;
        (*d)->receive_remain = 0;
        (*d)->receive_until_marker_mode = 0;

        return 0;
}

void testsubsys0_detach(void *data)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        printk("testsubsys0: detach() invoked\n");
        if (d)
                kfree(d);
        else
                printk(KERN_WARNING "testsubsys0: something's wrong - state pointer points to NULL\n");
}

int testsubsys0_probe_data(void *data, const unsigned char *cp, int count)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        int recognized = 0;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked probe_data() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        /* data recognition rules:
         *      1) when the first 4 bytes are "!AAA" -> size = 4
         *      2) when the first 2 bytes are "!B" -> next 2 bytes are size, decimal
         *      3) when the first 4 bytes are "!CCC" -> receive everything until '$'
         */

        if (cp[0] != '!')
                goto exit;

        if (cp[1] == 'A' && cp[2] == 'A' && cp[3] == 'A') {
                recognized = 1;
                d->receive_remain = 4;
        }
        else if (cp[1] == 'B') {
                if (cp[2] < '0' || cp[2] > '9' || cp[3] < '0' || cp[3] > '9')
                        d->receive_remain = 4; /* non-numeric chars... */
                else
                        d->receive_remain = (cp[2]-'0') * 10 + (cp[3]-'0');
                recognized = 1;
        }
        else if (cp[1] == 'C' && cp[2] == 'C' && cp[3] == 'C') {
                printk("testsubsys0: receive_until_marker_mode on\n");
                d->receive_until_marker_mode = 1;
                recognized = 1;
        }

exit:
        if (recognized)
                printk("testsubsys0: data recognized\n");

        return recognized;
}

int testsubsys0_probe_size(void *data, const unsigned char *cp, int count)
{
        int size = 0;
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked probe_size() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        // TODO recognize size of every packet beginning with ! <lowcase letter> similarly to !B

        if (count > 16 && cp[16] != ' ') {
                /* size recognized - use 17th char in buf for size calc - except when [space] */
                size = cp[16] - '@';
                if (size <= 0)
                        size = count;
                printk("testsubsys0: size recognized as %d ('%c')\n", size,
                        cp[16]);
        }

        return size;
}

int testsubsys0_do_receive(void *data, const unsigned char *cp, int count)
{
        struct testsubsys0_data *d = (struct testsubsys0_data *)data;
        int ret;
        (void)d;

        print_hex_dump_bytes("testsubsys0: invoked do_receive() - ",
                        DUMP_PREFIX_OFFSET, cp, count);

        printk("testsubsys0:    receive_until_marker_mode=%d, "
                "receive_remain=%d, count=%d\n", d->receive_until_marker_mode,
                d->receive_remain, count);
        if (d->receive_until_marker_mode) {
                /* receive everything until and including '$' character */
                int n = 0;
                while (count--) {
                        if (cp[n++] == '$') {
                                printk("testsubsys0: found '$' --> "
                                        "receive_until_marker_mode off\n");
                                d->receive_until_marker_mode = 0;
                                ret = n;
                                goto exit;
                        }
                }
                ret = -1;
        }
        else {
                if (d->receive_remain > count) {
                        ret = -1;
                        d->receive_remain -= count;
                }
                else {
                        ret = d->receive_remain;
                        d->receive_remain = 0;
                }
        }

exit:
        printk("testsubsys0: exit do_receive() with %d\n", ret);
        return ret;
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


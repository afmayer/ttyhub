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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include "ttyhub.h"
#include "ttyhub_ioctl.h"
MODULE_AUTHOR("Alexander F. Mayer");
MODULE_LICENSE("GPL");

#define TTYHUB_VERSION "0.20"

#define N_TTYHUB 29
#if N_TTYHUB >= NR_LDISCS
#error N_TTYHUB is larger than the maximum allowed value
#endif

static int max_subsys = 16;
module_param(max_subsys, int, 0);
MODULE_PARM_DESC(max_subsys, "Maximum number of TTYHUB subsystems");

static int probe_buf_size = 32;
module_param(probe_buf_size, int, 0);
MODULE_PARM_DESC(probe_buf_size, "Size of the TTYHUB receive probe buffer");

#ifdef DEBUG
static unsigned int debug = 0;
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, "Each bit controls a debug output category");

#define TTYHUB_DEBUG_LDISC_OPS_USER                     1
#define TTYHUB_DEBUG_RECV_STATE_MACHINE                 2
#define TTYHUB_DEBUG_RECV_STATE_MACHINE_DUMP_PROBE_BUF  4
#endif /* DEBUG */

// TODO List what is protected by ttyhub_subsystems_lock
//      e.g. state->enabled_subsystems, subsystems list,
//           subsys->enabled_refcount, subs->enable_in_progress

struct ttyhub_state {
        struct tty_struct *tty;
        void **subsys_data;
        unsigned long timed_discard_min_silence; // TODO make this configurable in ioctl()

        int recv_subsys;
        unsigned char *probed_subsystems;
        unsigned char *enabled_subsystems;
        int discard_bytes_remaining;
        unsigned long timed_discard_upto;
        unsigned long timed_discard_count;

        unsigned char *probe_buf;
        int probe_buf_consumed;
        int probe_buf_count;

        int cp_consumed;
};

static struct ttyhub_subsystem **ttyhub_subsystems;
static spinlock_t ttyhub_subsystems_lock;

#ifdef DEBUG
const char *ttyhub_debug_state_to_string(struct ttyhub_state *state)
{
        if (state->recv_subsys >= 0) {
                return ttyhub_subsystems[state->recv_subsys]->name;
        }
        else {
                switch (state->recv_subsys) {
                case -1:
                        return "PROBE_DATA";
                case -2:
                        return "PROBE_SIZE";
                case -3:
                        return "DISCARD_DATA";
                case -4:
                        return "TIMED_DISCARD";
                }
        }

        return "";
}

void ttyhub_debug_dump_probe_buf(struct ttyhub_state *state)
{
        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE_DUMP_PROBE_BUF) {
                printk(KERN_INFO "ttyhub: receive_buf() probe_buf: count=%d, "
                        "consumed=%d\n", state-> probe_buf_count,
                        state->probe_buf_consumed);
                print_hex_dump(KERN_INFO, "ttyhub: receive_buf()    |",
                        DUMP_PREFIX_OFFSET, 16, 1, state->probe_buf,
                        probe_buf_size, true);
        }
}
#endif /* DEBUG */

/*
 * Register a new subsystem.
 * The subsystem structure passed to this function is owned by the caller
 * and may not be modified from the outside once it is registered.
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while searching
 *      for a free index and inserting the subsystem to the list.
 *
 * Returns:
 *      On success the index of the registered subsystem is returned. When
 *      an error occurs -1 is returned.
 */
int ttyhub_register_subsystem(struct ttyhub_subsystem *subs)
{
        unsigned long flags;
        int i;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                if (ttyhub_subsystems[i] == NULL)
                        break;
        }
        if (i == max_subsys)
                /* no more space for this subsystem */
                goto error_unlock;

        ttyhub_subsystems[i] = subs;
        subs->enabled_refcount = 0;
        subs->enable_in_progress = 0;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        printk(KERN_INFO "ttyhub: registered subsystem '%s' as #%d\n",
                subs->name, i);
        return i;

error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return -1;
}
EXPORT_SYMBOL_GPL(ttyhub_register_subsystem);

/*
 * Unregister a subsystem.
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while removing
 *      the subsystem from the list.
 *
 * Returns:
 *      On success zero is returned. When an error occurs -1 is returned.
 */
int ttyhub_unregister_subsystem(int index)
{
        unsigned long flags;
        struct ttyhub_subsystem *subs;

        if (index >= max_subsys || index < 0)
                return -1;

        subs = ttyhub_subsystems[index];
        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        if (subs == NULL)
                goto error_unlock;
        if (subs->enabled_refcount != 0)
                goto error_unlock;
        ttyhub_subsystems[index] = NULL;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        printk(KERN_INFO "ttyhub: unregistered subsystem '%s'\n", subs->name);
        return 0;

error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return -1;
}
EXPORT_SYMBOL_GPL(ttyhub_unregister_subsystem);

/*
 * Enable a subsystem on a given tty.
 * This is a helper function for ttyhub_ldisc_ioctl().
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while actually
 *      enabling a subsystem, but not while the call to the subsystem's
 *      attach() operation.
 *
 * Returns:
 *      The return value can be directly used as the return value to the
 *      line discipline ioctl() operation. It is either a negative error code
 *      or a nonnegative value on success. The subsystem's attach() operation
 *      directly decides the return code if everything went well until that
 *      point. If it doesn't exist, zero is returned on success.
 */
static int ttyhub_subsystem_enable(struct ttyhub_state *state, int index)
{
        unsigned long flags;
        int err = 0;
        struct ttyhub_subsystem *subs = ttyhub_subsystems[index];

        if (index >= max_subsys || index < 0)
                return -EINVAL;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);

        if (subs == NULL) {
                err = -EINVAL;
                goto error_unlock;
        }
        if (!try_module_get(subs->owner)) {
                err = -EBUSY;
                goto error_unlock;
        }

        if (subs->enable_in_progress) { // TODO HERE IS A BUG: enable_in_progress is never reset to 0 when the attach() operation returns an error!
                err = -EBUSY;           //      This also happens when the subsystem is already enabled... simply move enable_in_progress=1 after setting bit
                goto error_putmodule;
        }
        subs->enable_in_progress = 1;
        if (state->enabled_subsystems[index/8] & 1 << index%8) {
                err = -EINVAL;
                goto error_putmodule;
        }

        /* prevent subsystem unregistering while enable is in progress */
        subs->enabled_refcount++;

        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);

        /* invoking the subsystem's attach() operation must happen before
           the bit in the enabled_subsystems array is set */
        if (subs->attach)
                err = subs->attach(&state->subsys_data[index], state->tty);
        if (err < 0)
                goto error_decr_refcount;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        state->enabled_subsystems[index/8] |= 1 << index%8;
        subs->enable_in_progress = 0;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);

        return err;

error_decr_refcount:
        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        subs->enabled_refcount--;
error_putmodule:
        module_put(ttyhub_subsystems[index]->owner);
error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return err;
}

// TODO doc - THIS MAY NOT FAIL FOR ACTUALLY ENABLED SUBSYSTEMS!
static int ttyhub_subsystem_disable(struct ttyhub_state *state, int index)
{
        // TODO how to respect subs->enable_in_progress?
        unsigned long flags;
        struct ttyhub_subsystem *subs = ttyhub_subsystems[index];

        if (index >= max_subsys || index < 0)
                return -1;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        if (!(state->enabled_subsystems[index/8] & 1 << index%8))
                goto error_unlock;
        state->enabled_subsystems[index/8] &= ~(1 << index%8);

        /* if the active subsystem happens to be the one we want to disable
           we must wait until the receive state machine finished receiving data
           with the current subsystem */
        // TODO implement waiting for (recv_subsys < 0) - UNLOCK before waiting!

        subs->enabled_refcount--;
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);

        if (subs->detach)
                subs->detach(state->subsys_data[index]);

        module_put(subs->owner);

        return 0;

error_unlock:
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        return -1;
}

/*
 * Probe subsystems if they can identify a received data chunk.
 * The recv_subsys field and the array pointed to by probed_subsystems
 * of the ttyhub_state structure is changed according to the probe results,
 * but the probe buffer is not filled in case more data is needed.
 * This is a helper function for ttyhub_ldisc_receive_buf().
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while searching
 *      for an entry to probe, but not while probing the subsystem.
 *
 * Returns:
 *  0   either a subsystem has identified the data or all subsystems have
 *      already been probed - state machine must continue in this call
 *  1   the received data has not been identified - wait for more data
 */
static int ttyhub_probe_subsystems(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        unsigned long flags;
        int i, j, subsys_remaining = 0;
        struct ttyhub_subsystem *subs;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                subs = ttyhub_subsystems[i];
                if (subs == NULL)
                        continue;
                if (!(state->enabled_subsystems[i/8] & 1 << i%8))
                        continue;
                if (state->probed_subsystems[i/8] & 1 << i%8)
                        continue;
                if (subs->probe_data_minimum_bytes > count) {
                        subsys_remaining = 1;
                        continue;
                }
                spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
                if (subs->probe_data(state->subsys_data[i], cp, count)) {
                        /* data identified by subsystem */
                        state->recv_subsys = i;
                        for (j=0; j < (max_subsys-1)/8 + 1; j++)
                                state->probed_subsystems[j] = 0;
                        return 0;
                }
                state->probed_subsystems[i/8] |= 1 << i%8;
                spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        }

        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
        if (!subsys_remaining) {
                state->recv_subsys = -2;
                for (j=0; j < (max_subsys-1)/8 + 1; j++)
                        state->probed_subsystems[j] = 0;
        }

        return subsys_remaining;
}

/*
 * Probe subsystems if they can identify the size of a received data chunk.
 * The recv_subsys and discard_bytes_remaining fields of the ttyhub_state 
 * structure are changed according to the probe results, but the probe buffer
 * is not filled in case more data is needed.
 * This is a helper function for ttyhub_ldisc_receive_buf().
 *
 * Locks:
 *      The subsystems lock (ttyhub_subsystems_lock) is held while searching
 *      for an entry to probe, but not while probing the subsystem.
 *
 * Returns:
 *  0   either a subsystem has identified the size or the probe buffer is
 *      full - state machine must continue in this call
 *  1   the received data has not been identified and there is still space
 *      in the probe buffer left - wait for more data
 */
// TODO when timed drop packet mode is implemented change documentation
//      (an additional field in the state is changed)
static int ttyhub_probe_subsystems_size(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
        unsigned long flags;
        int i, status, probe_buf_room;
        struct ttyhub_subsystem *subs;

        spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        for (i=0; i < max_subsys; i++) {
                subs = ttyhub_subsystems[i];
                if (subs == NULL)
                        continue;
                if (!(state->enabled_subsystems[i/8] & 1 << i%8))
                        continue;
                spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);
                if (subs->probe_size)
                        status = subs->probe_size(state->subsys_data[i],
                                        cp, count);
                else
                        status = 0;
                if (status > 0) {
                        /* size recognized */
                        state->recv_subsys = -3;
                        state->discard_bytes_remaining = status;
                        return 0;
                }
                else if (status < 0) {
                        // TODO size not recognized but subsystem can identify
                        //      end of data - implement! (set recv_subsys to i)
                }
                spin_lock_irqsave(&ttyhub_subsystems_lock, flags);
        }
        spin_unlock_irqrestore(&ttyhub_subsystems_lock, flags);

        probe_buf_room = probe_buf_size - state->probe_buf_count +
                state->probe_buf_consumed;
        if ((state->probe_buf_count - state->probe_buf_consumed == 0 &&
                        count >= probe_buf_size) || probe_buf_room == 0) {
                /* no more space in the probe buffer to wait for more data (or
                   probe buffer is unused but received data is larger than
                   the probe buffer) --> timed discard mode */
                state->recv_subsys = -4;
                state->timed_discard_upto = jiffies +
                        state->timed_discard_min_silence;
                return 0;
        }

        /* wait for more data - there is still space in the probe buffer */
        return 1;
}

/*
 * Append data to probe buffer.
 * Buffer is packed before appending when parts of the data
 * have already been consumed.
 * This is a helper function for ttyhub_ldisc_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 *
 * Returns the number of bytes that were copied to the probe buffer.
 */
static int ttyhub_probebuf_push(struct ttyhub_state *state,
                        const unsigned char *cp, int count)
{
#ifdef DEBUG
        int debug_packed = 0;
#endif

        int room, n;
        if (state->probe_buf_consumed) {
                /* probe buffer in use and partly consumed - pack */
                int i;
                int offset = state->probe_buf_consumed;
                int max = state->probe_buf_count - offset;
                for (i=0; i < max; i++)
                        state->probe_buf[i] = state->probe_buf[i + offset];
                state->probe_buf_count -= offset;
                state->probe_buf_consumed = 0;
#ifdef DEBUG
                if (max != 0)
                        debug_packed = 1;
#endif
        }
        room = probe_buf_size - state->probe_buf_count;
        n = count > room ? room : count;
        memcpy(state->probe_buf + state->probe_buf_count, cp, n);
        state->probe_buf_count += n;
        state->cp_consumed += n;

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE) {
                printk(KERN_INFO "ttyhub: receive_buf() pushed %d bytes to "
                        "probe buffer%s (%d requested)\n", n, debug_packed ?
                        " (after pack)" : "", count);
                ttyhub_debug_dump_probe_buf(state);
        }
#endif

        return n;
}

/*
 * Get pointer to and length of received data.
 * This gets either a pointer to the probe buffer read head (when it is at
 * least partially filled) or a pointer to unread data in cp. The amount of
 * consumed data is considered for both situations.
 * This is a helper function for ttyhub_ldisc_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 */
static void ttyhub_get_recvd_data_head(struct ttyhub_state *state,
                        const unsigned char *cp, int count,
                        const unsigned char **out_cp, int *out_count)
{
        int probe_buf_fillstate = state->probe_buf_count -
                state->probe_buf_consumed;
        if (probe_buf_fillstate) {
                /* probe buffer in use */
                *out_cp = state->probe_buf + state->probe_buf_consumed;
                *out_count = probe_buf_fillstate;
        }
        else {
                /* probe buffer not in use */
                *out_cp = cp + state->cp_consumed;
                *out_count = count - state->cp_consumed;
        }
}

/*
 * Mark received data as consumed - advance pointers for the next read access.
 * This is a helper function for ttyhub_ldisc_receive_buf().
 *
 * All locks to involved data structures are asssumed to be held already.
 */
static void ttyhub_recvd_data_consumed(struct ttyhub_state *state, int count)
{
#ifdef DEBUG
        int debug_probe_buf_in_use = 0;
#endif

        if (state->probe_buf_count - state->probe_buf_consumed) {
                /* probe buffer in use */
#ifdef DEBUG
                debug_probe_buf_in_use = 1;
#endif
                state->probe_buf_consumed += count;
        }
        else {
                /* probe buffer not in use */
                state->cp_consumed += count;
        }

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE) {
                printk(KERN_INFO "ttyhub: receive_buf() consumed %d bytes from"
                        " %s\n", count, debug_probe_buf_in_use ? "probe buffer"
                        : "cp");
                if (debug_probe_buf_in_use)
                        ttyhub_debug_dump_probe_buf(state);
        }
#endif
}

/* Line discipline open() operation */
static int ttyhub_ldisc_open(struct tty_struct *tty)
{
        struct ttyhub_state *state;
        int err = -ENOBUFS;

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                printk(KERN_INFO "ttyhub: ldisc open(tty=%s) enter\n",
                                tty->name);
#endif

        state = kmalloc(sizeof(*state), GFP_KERNEL);
        if (state == NULL)
                goto error_exit;

        state->tty = tty;

        /* allocate space for one pointer for every possible subsystem */
        state->subsys_data = kzalloc(sizeof(void *) * max_subsys, GFP_KERNEL);
        if (state->subsys_data == NULL)
                goto error_cleanup_state;

        state->timed_discard_min_silence = 5*HZ; // TODO choose a reasonable default time!
        state->recv_subsys = -1;
        state->discard_bytes_remaining = 0;
        state->timed_discard_upto = 0;
        state->timed_discard_count = 0;

        /* allocate probe buffer */
        state->probe_buf = kmalloc(probe_buf_size, GFP_KERNEL);
        if (state->probe_buf == NULL)
                goto error_cleanup_subsys_data;
        state->probe_buf_consumed = 0;
        state->probe_buf_count = 0;

        /* allocate 2x char array with 1 bit per subsystem each */
        state->probed_subsystems = kzalloc(2*((max_subsys-1)/8+1), GFP_KERNEL);
        if (state->probed_subsystems == NULL)
                goto error_cleanup_probebuf;
        state->enabled_subsystems = state->probed_subsystems +
                (max_subsys-1)/8 + 1;

        /* success */
        tty->disc_data = state;
        tty->receive_room = 65536;
        err = 0;
        goto error_exit;

error_cleanup_probebuf:
        kfree(state->probe_buf);
error_cleanup_subsys_data:
        kfree(state->subsys_data);
error_cleanup_state:
        kfree(state);
error_exit:

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                printk(KERN_INFO "ttyhub: ldisc open() exit = %d\n", err);
#endif

        return err;
}

/* Line discipline close() operation */
static void ttyhub_ldisc_close(struct tty_struct *tty)
{
        struct ttyhub_state *state = tty->disc_data;
        int i;

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                printk(KERN_INFO "ttyhub: ldisc close(tty=%s) enter\n",
                                tty->name);
#endif

        if (state == NULL)
                goto exit;

        /* disable all subsystems that are enabled on this tty */
        for (i=0; i < max_subsys; i++)
                ttyhub_subsystem_disable(state, i);

        kfree(state->probed_subsystems);
        kfree(state->probe_buf);
        kfree(state->subsys_data);
        kfree(state);
exit:
#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                printk(KERN_INFO "ttyhub: ldisc close() exit\n");
#endif
        return;
}

/* Line discipline ioctl() operation */
static int ttyhub_ldisc_ioctl(struct tty_struct *tty, struct file *filp,
                        unsigned int cmd, unsigned long arg)
{
        int err = 0;
        struct ttyhub_state *state = tty->disc_data;
        unsigned int direction = _IOC_DIR(cmd);
        unsigned int type = _IOC_TYPE(cmd);
        unsigned int size = _IOC_SIZE(cmd);
        unsigned char arg_buf[16];

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER) {
                unsigned int nr = _IOC_NR(cmd);
                char *debug_dir = (direction==_IOC_NONE) ? "none" :
                                (direction==_IOC_READ) ? "read" :
                                (direction==_IOC_WRITE) ? "write" : "r+w";
                printk(KERN_INFO "ttyhub: ldisc ioctl(tty=%s, "
                                "cmd=%s/0x%02x/%u/%ubytes, arg=0x%lx) enter\n",
                                tty->name, debug_dir, type, nr, size, arg);
        }
#endif

        if (type != TTYHUB_IOCTL_TYPE_ID || size > sizeof(arg_buf)) {
                /* ioctl commands with incorrect type as well as commands that
                   have a larger payload than fits in the buffer are ignored */
                err = -ENOTTY;
                goto copy_and_exit;
        }

        if (direction & _IOC_READ) {
                /* read or read+write */
                if (!access_ok(VERIFY_WRITE, (void __user *)arg, size)) {
                        err = -EFAULT;
                        goto copy_and_exit;
                }
        }
        else if (direction & _IOC_WRITE) {
                /* write only */
                if (!access_ok(VERIFY_READ, (void __user *)arg, size)) {
                        err = -EFAULT;
                        goto copy_and_exit;
                }
        }

        if (direction & _IOC_WRITE) {
                /* write or read+write */
                if (copy_from_user(arg_buf, (void __user *)arg, size)) {
                        err = -EFAULT;
                        goto copy_and_exit;
                }
#ifdef DEBUG
                if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                        print_hex_dump(KERN_INFO, "ttyhub: ldisc ioctl() arg "
                                        "from user: ", DUMP_PREFIX_OFFSET, 16,
                                        1, arg_buf, size, true);
#endif
        }

        switch (cmd) {
        case TTYHUB_SUBSYS_ENABLE:
                /* enable subsystem */
                err = ttyhub_subsystem_enable(state, *((int *)arg_buf));
                goto copy_and_exit;
        default:
                err = -ENOTTY;
                goto copy_and_exit;
        }

copy_and_exit:
        if (err >= 0 && direction & _IOC_READ) {
                /* read or read+write */
#ifdef DEBUG
                if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                        print_hex_dump(KERN_INFO, "ttyhub: ldisc ioctl() arg "
                                        "to user:   ", DUMP_PREFIX_OFFSET, 16,
                                        1, arg_buf, size, true);
#endif
                if (copy_to_user((void __user *)arg, arg_buf, size)) {
                        err = -EFAULT;
                }
        }

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_LDISC_OPS_USER)
                printk(KERN_INFO "ttyhub: ldisc ioctl() exit = %d\n", err);
#endif
        return err;
}

/*
 * Line discipline receive_buf() operation
 * Called by the hardware driver when new data arrives.
 *
 * Locks:
 *      Functions called here may lock the subsystems lock.
 */
static void ttyhub_ldisc_receive_buf(struct tty_struct *tty,
                        const unsigned char *cp, char *fp, int count)
{
        struct ttyhub_state *state = tty->disc_data;
        const unsigned char *r_cp;
        int r_count, wait = 0;

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE) {
                printk(KERN_INFO "ttyhub: receive_buf(tty=%s, cp=0x%p, "
                                "fp=0x%p, count=%d) enter\n", tty->name, cp,
                                fp, count);
                print_hex_dump(KERN_INFO, "ttyhub: receive_buf() cp: ",
                                DUMP_PREFIX_OFFSET, 16, 1, cp, count, true);
                if (fp)
                        print_hex_dump(KERN_INFO, "ttyhub: receive_buf()"
                                        " fp: ", DUMP_PREFIX_OFFSET, 16, 1, fp,
                                        count, true);
        }
#endif

        /* Receive state machine:
         * The relevant fields in the ttyhub_state struct are:
         *   1) recv_subsys
         *        values from 0 to (max_subsys-1):
         *              addressed subsystem is known, proceed with receiving
         *              data immediately
         *        value -1:
         *              addressed subsystem is unknown and has to be probed
         *        value -2:
         *              addressed subsystem is unknown and all registered
         *              subsystems have already been probed - probe for size
         *        value -3:
         *              addressed subsystem is unknown, size of packet has
         *              been recognized
         *   2) probed_subsystems
         *        This is a pointer to an unsigned char array containing one
         *        bit for every possible subsystem. When the recv_subsys is
         *        -1 a set bit indicates that the subsystem has already been
         *        probed and should not be probed again.
         *   3) discard_bytes_remaining
         *        When recv_subsys is -3 this stores the number of bytes to
         *        be discarded. Decremented after data has been received.
         *   4) timed_discard_upto
         *        // TODO describe timed_discard_upto, timed_discard_count, timed_discard_min_silence
         *   TODO describe probe_buf management related fields
         * All data from cp must be either consumed by a subsystem or go to
         * the probe buffer before returning from the call.
         * The state machine continues until either...
         *   ...more data is needed for probing
         *      --> received data is kept in the probe buffer between calls
         *    OR
         *   ...the probe buffer and cp are completely consumed
         */

        /* when cp is read partially, this is used as an offset */
        state->cp_consumed = 0;

#ifdef DEBUG
        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE)
                printk(KERN_INFO "ttyhub: receive_buf() initial recv_subsys "
                                "= %d (%s)\n", state->recv_subsys,
                                ttyhub_debug_state_to_string(state));
#endif

        while (1) {
#ifdef DEBUG
                int debug_old_recv_subsys = state->recv_subsys;
#endif

                if (state->probe_buf_count - state->probe_buf_consumed == 0 &&
                                count - state->cp_consumed == 0) {
                        /* all data consumed */
#ifdef DEBUG
                        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE)
                                printk(KERN_INFO "ttyhub: receive_buf() "
                                                "exit (all data consumed)\n");
#endif
                        return;
                }

                /* if there is data remaining in the probe buffer as well as in
                   cp fill up the buffer (this is relevant after data has been
                   consumed and before probing) */
                if (state->probe_buf_count - state->probe_buf_consumed != 0 &&
                                count - state->cp_consumed != 0)
                        ttyhub_probebuf_push(state, cp +
                                state->cp_consumed, count -
                                state->cp_consumed);

                ttyhub_get_recvd_data_head(state, cp, count, &r_cp, &r_count);
                if (state->recv_subsys == -1) {
                        wait = ttyhub_probe_subsystems(state, r_cp, r_count);
                }
                else if (state->recv_subsys == -2) {
                        wait = ttyhub_probe_subsystems_size(state, r_cp,
                                        r_count);
                }
                else if (state->recv_subsys == -3) {
                        int n;
                        n = r_count > state->discard_bytes_remaining ?
                                state->discard_bytes_remaining : r_count;
                        ttyhub_recvd_data_consumed(state, n);
                        state->discard_bytes_remaining -= n;
#ifdef DEBUG
                        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE)
                                printk(KERN_INFO "ttyhub: receive_buf() "
                                        "discard_bytes_remaining = %d",
                                        state->discard_bytes_remaining);
#endif
                        if (state->discard_bytes_remaining == 0)
                                state->recv_subsys = -1;
                }
                else if (state->recv_subsys == -4) {
                        if (time_after(jiffies, state->timed_discard_upto)) {
                                printk(KERN_WARNING "ttyhub: discarded %lu " // TODO make this warning configurable and rewrite 'based on time'
                                        "unrecognized bytes based on time\n",
                                        state->timed_discard_count);
                                state->timed_discard_count = 0;
                                state->recv_subsys = -1;
                        }
                        else {
                                /* not enough time elapsed  - discard all
                                   data and reset timeout */
                                ttyhub_recvd_data_consumed(state, r_count);
                                state->timed_discard_count += r_count;
                                state->timed_discard_upto = jiffies +
                                        state->timed_discard_min_silence;
                        }
                }
                else if (state->recv_subsys >= 0) {
                        int n;
                        struct ttyhub_subsystem *subs =
                                ttyhub_subsystems[state->recv_subsys];
                        n = subs->do_receive(
                                        state->subsys_data[state->recv_subsys],
                                        r_cp, r_count);
                        if (n < 0) {
                                /* subsystem expects more data */
                                ttyhub_recvd_data_consumed(state, r_count);
                        }
                        else {
                                /* subsystem finished receiving */
                                ttyhub_recvd_data_consumed(state, n);
                                state->recv_subsys = -1;
                        }
                }

#ifdef DEBUG
                if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE &&
                                state->recv_subsys != debug_old_recv_subsys)
                        printk(KERN_INFO "ttyhub: receive_buf() new recv_"
                                "subsys = %d (%s)\n", state->recv_subsys,
                                ttyhub_debug_state_to_string(state));
#endif

                if (wait) {
                        /* wait for data to probe more subsystems */
                        if (count - state->cp_consumed != 0)
                                ttyhub_probebuf_push(state, cp +
                                        state->cp_consumed, count -
                                        state->cp_consumed);
                                // TODO check num of bytes actually copied?
#ifdef DEBUG
                        if (debug & TTYHUB_DEBUG_RECV_STATE_MACHINE)
                                printk(KERN_INFO "ttyhub: receive_buf() "
                                                "exit (more data needed)\n");
#endif
                        return;
                }
        }
}

static void ttyhub_ldisc_write_wakeup(struct tty_struct *tty)
{
        //int written = tty->ops->write(tty, pointer_to_first_buffer_byte, bytes_in_buffer);
        // TODO update buffer pointer, bytes_in_buffer
}

struct tty_ldisc_ops ttyhub_ldisc =
{
        .owner        = THIS_MODULE,
        .magic        = TTY_LDISC_MAGIC,
        .name         = "ttyhub",
        .open         = ttyhub_ldisc_open,
        .close        = ttyhub_ldisc_close,
        .ioctl        = ttyhub_ldisc_ioctl,
        .receive_buf  = ttyhub_ldisc_receive_buf,
        .write_wakeup = ttyhub_ldisc_write_wakeup
};

/* module init/exit functions */
static int __init ttyhub_init(void)
{
        int status;

        if (max_subsys < 2)
                max_subsys = 2;

        if (probe_buf_size < 16)
                probe_buf_size = 16;

        printk(KERN_INFO "ttyhub: version %s, max. subsystems = %d, probe "
                "bufsize = %d"
#ifdef DEBUG
                " (debug build)"
#endif
                "\n", TTYHUB_VERSION, max_subsys, probe_buf_size);

        /* allocate space for pointers to subsystems and init lock */
        ttyhub_subsystems = kzalloc(
                sizeof(struct ttyhub_subsystem *) * max_subsys,
                GFP_KERNEL);
        if (ttyhub_subsystems == NULL)
                return -ENOMEM;
        spin_lock_init(&ttyhub_subsystems_lock);

        /* register line discipline */
        status = tty_register_ldisc(N_TTYHUB, &ttyhub_ldisc); // TODO dynamic LDISC nr
        if (status != 0) {
                kfree(ttyhub_subsystems);
                printk(KERN_ERR "ttyhub: can't register line discipline "
                        "(err = %d)\n", status);
        }
        return status;
}

static void __exit ttyhub_exit(void)
{
        int status;

        status = tty_unregister_ldisc(N_TTYHUB);
        if (status != 0)
                printk(KERN_ERR "ttyhub: can't unregister line "
                        "discipline (err = %d)\n", status);

        kfree(ttyhub_subsystems);
}

module_init(ttyhub_init);
module_exit(ttyhub_exit);


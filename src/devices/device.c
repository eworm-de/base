/***
  This file is part of bus1. See COPYING for details.

  bus1 is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  bus1 is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bus1; If not, see <http://www.gnu.org/licenses/>.
***/

#include <c-rbtree.h>
#include <org.bus1/c-macro.h>
#include <string.h>

#include "device.h"
#include "uevent.h"

static int devices_compare(CRBTree *t, void *k, CRBNode *n) {
        struct device *device = c_container_of(n, struct device, rb);
        const char *devpath = k;

        return strcmp(devpath, device->devpath);
}

static void device_unlink(CRBTree *devices, struct device *device) {
        c_rbtree_remove(devices, &device->rb);
}

static struct device *device_get_by_devpath(CRBTree *devices, const char *devpath) {
        CRBNode *n;

        assert(devices);
        assert(devpath);

        n = c_rbtree_find_node(devices, devices_compare, devpath);
        if (!n)
                return NULL;

        return c_container_of(n, struct device, rb);
}

struct device *device_free(struct device *device) {
        if (!device)
                return NULL;

        free(device);

        return NULL;
}

static void device_freep(struct device **devicep) {
        device_free(*devicep);
}

static int device_new(Manager *m, struct device **devicep, const char *devpath,
                      const char *subsystem, const char *devtype,
                      const char *devname, const char *modalias) {
        _c_cleanup_(device_freep) struct device *device = NULL;
        size_t n_devpath, n_subsystem, n_devtype, n_devname, n_modalias;

        assert(devicep);
        assert(devpath);
        assert(subsystem);

        n_devpath = strlen(devpath) + 1;
        n_subsystem = strlen(subsystem) + 1;
        n_devtype = devtype ? strlen(devtype) + 1 : 0;
        n_devname = devname ? strlen(devname) + 1 : 0;
        n_modalias = modalias ? strlen(modalias) + 1 : 0;
        device = malloc(sizeof(*device) + n_devpath + n_subsystem +
                        n_devtype + n_devname + n_modalias);
        if (!device)
                return -ENOMEM;

        device->manager = m;
        c_rbnode_init(&device->rb);
        device->devpath = memcpy((void*)(device + 1), devpath, n_devpath);
        device->subsystem = memcpy((char*)device->devpath + n_devpath, subsystem, n_subsystem);
        device->devtype = memcpy((char*)device->subsystem + n_subsystem, devtype, n_devtype);
        device->devname = memcpy((char*)device->devtype + n_devtype, devname, n_devname);
        device->modalias = memcpy((char*)device->devname + n_devname, modalias, n_modalias);

        if (!devtype)
                device->devtype = NULL;
        if (!devname)
                device->devname = NULL;
        if (!modalias)
                device->modalias = NULL;

        *devicep = device;
        device = NULL;
        return 0;
}

static int device_change(Manager *m, struct device **devicep, const char *devpath,
                         const char *subsystem, const char *devtype,
                         const char *devname, const char *modalias) {
        struct device *device;

        device = device_get_by_devpath(&m->devices, devpath);
        if (!device)
                return -EIO;

        *devicep = device;

        return 0;
}

int device_add(Manager *m, struct device **devicep, const char *devpath,
               const char *subsystem, const char *devtype,
               const char *devname, const char *modalias) {
        struct device *device;
        CRBNode **slot, *p;
        int r;

        slot = c_rbtree_find_slot(&m->devices, devices_compare, devpath, &p);
        if (!slot) {
                if (m->settled)
                        return -EIO;
                else
                        /* This can happen if an ADD event races /sys enumeration, let
                         * the uevent be authoritative and treat it like a CHANGE. */
                        return device_change(m, devicep, devpath, subsystem, devtype, devname, modalias);
        }

        r = device_new(m, &device, devpath, subsystem, devtype, devname, modalias);
        if (r < 0)
                return r;

        c_rbtree_add(&m->devices, p, slot, &device->rb);

        *devicep = device;

        return 0;
}

static int device_remove(Manager *m, const char *devpath) {
        struct device *device;

        device = device_get_by_devpath(&m->devices, devpath);
        if (!device) {
                if (m->settled)
                        return -EIO;
                        /* The device could be NULL if a REMOVE event races /sys
                         * enumeration, simply drop the event.  */
                else
                        return 0;
        }

        device_unlink(&m->devices, device);
        device_free(device);

        return 0;
}

static int device_move(Manager *m, struct device **devicep, const char *devpath_old, const char *devpath) {
        _c_cleanup_(device_freep) struct device *device = NULL;
        int r;

        /* A MOVE event is empty apart from the DEVPATH change, simply treat
         * this as a REMOVE followed with an ADD containing the same data as the
         * original device. */

        device = device_get_by_devpath(&m->devices, devpath);
        if (!device) {
                if (m->settled)
                        return -EIO;
                else
                        return 0;
        }

        device_unlink(&m->devices, device);

        r = device_add(m, devicep, device->devpath, device->subsystem, device->devtype,
                       device->devname, device->modalias);
        if (r < 0)
                return r;

        return 0;
}

static ssize_t uevent_get_value(char *buf, size_t n_buf, const char *key, const char **valuep, char **nextp) {
        char *end, *value;

        end = memchr(buf, '\0', n_buf);
        if (!end)
                return -EBADMSG;

        value = strchr(buf, '=');
        if (!value)
                return -EBADMSG;

        if (strncmp(buf, key, value - buf) != 0)
                return -EBADMSG;

        value++;

        *valuep = value;
        *nextp = ++ end;

        return n_buf - (end - buf);
}

static ssize_t uevent_get_last_value(char *buf, size_t n_buf, const char *key, uint64_t *nump) {
        const char *k, *value;
        uint64_t num;

        if (n_buf == 0)
                return -EBADMSG;

        if (buf[n_buf - 1] != '\0')
                return -EBADMSG;

        value = memrchr(buf, '=', n_buf);
        if (!value)
                return -EBADMSG;

        value ++;

        n_buf = value - buf;
        buf[n_buf - 1] = '\0';

        k = memrchr(buf, '\0', n_buf - 1);
        if (k)
                k ++;
        else
                k = buf;

        if (strncmp(k, key, value - k) != 0)
                return -EBADMSG;

        errno = 0;
        num = strtoull(value, NULL, 10);
        if (errno != 0)
                return -errno;

        *nump = num;

        return k - buf;
}

static ssize_t uevent_get_key_value(char *buf, size_t n_buf, const char **keyp, const char **valuep, char **nextp) {
        char *end, *value;

        end = memchr(buf, '\0', n_buf);
        if (!end)
                return -EBADMSG;

        value = strchr(buf, '=');
        if (!value)
                return -EBADMSG;

        *value = '\0';
        value++;

        *keyp = buf;
        *valuep = value;
        *nextp = ++ end;

        return n_buf - (end - buf);
}


int device_from_nulstr(Manager *m, struct device **devicep, int *actionp,
                       uint64_t *seqnump, char *buf, size_t n_buf) {
        struct device *device = NULL;
        ssize_t buflen = n_buf;
        const char *action_string;
        const char *devpath;
        const char *subsystem;
        const char *devpath_old;
        const char *devtype = NULL;
        const char *devname = NULL;
        const char *modalias = NULL;
        int action;
        uint64_t seqnum;
        int r;

        assert(m);
        assert(devicep);
        assert(actionp);
        assert(seqnump);
        assert(buf);

        /* the first three and the last properties are fixed */
        buflen = uevent_get_value(buf, buflen, "ACTION", &action_string, &buf);
        if (buflen < 0)
                return buflen;
        else {
                action = uevent_action_from_string(action_string);
                if (action < 0)
                        return action;
        }

        buflen = uevent_get_value(buf, buflen, "DEVPATH", &devpath, &buf);
        if (buflen < 0)
                return buflen;

        /* store path relative to /sys, so drop the leading slash */
        if (devpath[0] != '/')
                return -EBADMSG;

        devpath ++;

        buflen = uevent_get_value(buf, buflen, "SUBSYSTEM", &subsystem, &buf);
        if (buflen < 0)
                return buflen;

        if (action == UEVENT_ACTION_MOVE) {
                /* a MOVE event only contains one other property */
                buflen = uevent_get_value(buf, buflen, "DEVPATH_OLD", &devpath_old, &buf);
                if (buflen < 0)
                        return buflen;
        }

        buflen = uevent_get_last_value(buf, buflen, "SEQNUM", &seqnum);
        if (buflen < 0)
                return buflen;

        if ((action == UEVENT_ACTION_REMOVE || action == UEVENT_ACTION_MOVE) && buflen > 0)
                return -EBADMSG;

        while (buflen > 0) {
                const char *key, *value;

                buflen = uevent_get_key_value(buf, buflen, &key, &value, &buf);
                if (buflen < 0)
                        return buflen;

                if (strcmp(key, "DEVTYPE") == 0)
                        devtype = value;
                else if (strcmp(key, "DEVNAME") == 0)
                        devname = value;
                else if (strcmp(key, "MODALIAS") == 0)
                        modalias = value;
        }

        /* We assume that /sys has been enumerated before any uevents are being
         * processed. We also assume that no spurious event take place (udevadm
         * trigger), which would require CAP_SYS_ADMIN. */

        switch (action) {
        case UEVENT_ACTION_CHANGE:
        /* XXX: decide what to do with these two events */
        case UEVENT_ACTION_ONLINE:
        case UEVENT_ACTION_OFFLINE:
                r = device_change(m, &device, devpath, subsystem, devtype, devname, modalias);
                if (r < 0)
                        return r;
                break;
        case UEVENT_ACTION_ADD:
                r = device_add(m, &device, devpath, subsystem, devtype, devname, modalias);
                if (r < 0)
                        return r;
                break;
        case UEVENT_ACTION_REMOVE:
                r = device_remove(m, devpath);
                if (r < 0)
                        return r;
                break;
        case UEVENT_ACTION_MOVE:
                r = device_move(m, &device, devpath_old, devpath);
                if (r < 0)
                        return r;
                break;
        }

        *devicep = device;
        *actionp = action;
        *seqnump = seqnum;
        return 0;
}

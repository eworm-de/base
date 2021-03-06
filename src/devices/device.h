#pragma once
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

#include <c-macro.h>
#include <c-rbtree.h>
#include "manager.h"
#include "uevent.h"

typedef int (*device_callback_t)(struct device *device, int sysfd, void *userdata);

struct device {
        Manager *manager;

        CRBNode rb;
        const char *devpath;

        CListEntry le;
        struct devtype *devtype;

        const char *devname;
        const char *modalias;

        int sysfd;
        struct uevent_subscription sysfd_subscription;
        CList sysfd_callbacks;
};

struct subsystem {
        Manager *manager;
        const char *name;
        CRBNode rb;

        CRBTree devtypes;
};

struct devtype {
        struct subsystem *subsystem;
        const char *name;
        CRBNode rb;

        CList devices;
};

struct device_slot {
        struct device *device;
        CListEntry le;
        device_callback_t cb;
        void *userdata;
};

int device_call_with_sysfd(struct device *device, struct device_slot **slot, device_callback_t cb, void *userdata);
int device_from_nulstr(Manager *m, struct device **devicep, int *action, uint64_t *seqnum, char *buf, size_t n_buf);
void device_unlink(struct device *device);
struct device *device_free(struct device *device);

int device_add(Manager *m, struct device **devicep, const char *devpath,
               const char *subsystem, const char *devtype,
               const char *devname, const char *modalias);

int devtype_add(struct subsystem *subsystem, struct devtype **devtypep, const char *name);

int subsystem_add(Manager *m, struct subsystem **subsystemp, const char *name);
struct subsystem *subsystem_free(struct subsystem *subsystem);

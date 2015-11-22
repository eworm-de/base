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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "bus1/identity.h"
#include "permissions.h"

#define ELEMENTSOF(x) (sizeof(x) / sizeof((x)[0]))

static const struct permissions {
        const char *subsystem;
        const char *devtype;
        const char *devname;
        mode_t mode;
        uid_t uid;
        gid_t gid;
} device_permissions[] = {
        { "input",        NULL,           NULL, 0600, BUS1_IDENTITY_INPUT,  0 },
        { "sound",        NULL,           NULL, 0600, BUS1_IDENTITY_AUDIO,  0 },
        { "video4linux",  NULL,           NULL, 0600, BUS1_IDENTITY_VIDEO,  0 },
        { "block",        NULL,           NULL, 0600, BUS1_IDENTITY_DISK,   0 },
        { "usb",          "usb_device",   NULL, 0600, BUS1_IDENTITY_USB,    0 },
};

int permissions_apply(int devfd, const char *devname, const char *subsystem, const char *devtype) {
        unsigned i;

        for (i = 0; i < ELEMENTSOF(device_permissions); i++) {
                if (subsystem && device_permissions[i].subsystem)
                        if (strcmp(subsystem, device_permissions[i].subsystem) != 0)
                                continue;

                if (devtype && device_permissions[i].devtype)
                        if (strcmp(devtype, device_permissions[i].devtype) != 0)
                                continue;

                if (devname && device_permissions[i].devname)
                        if (strcmp(devname, device_permissions[i].devname) != 0)
                                continue;

                if (device_permissions[i].uid > 0 || device_permissions[i].gid > 0)
                        if (fchownat(devfd, devname, device_permissions[i].uid, device_permissions[i].gid, AT_SYMLINK_NOFOLLOW) < 0)
                                return -errno;

                if (device_permissions[i].mode > 0)
                        if (fchmodat(devfd, devname, device_permissions[i].mode & 07777, 0) < 0)
                                return -errno;
        }

        return 0;
}

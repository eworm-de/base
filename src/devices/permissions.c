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
#include <string.h>
#include <sys/stat.h>
#include <org.bus1/b1-identity.h>
#include "permissions.h"

static const struct permissions {
        const char *subsystem;
        const char *devtype;
        mode_t mode;
        uid_t uid;
        gid_t gid;
} device_permissions[] = {
        { "input",        NULL,           0660, BUS1_IDENTITY_INPUT, BUS1_IDENTITY_INPUT },
        { "sound",        NULL,           0660, BUS1_IDENTITY_AUDIO, BUS1_IDENTITY_AUDIO },
        { "video4linux",  NULL,           0660, BUS1_IDENTITY_VIDEO, BUS1_IDENTITY_VIDEO },
        { "block",        NULL,           0660, BUS1_IDENTITY_DISK,  BUS1_IDENTITY_DISK },
        { "usb",          "usb_device",   0660, BUS1_IDENTITY_USB,   BUS1_IDENTITY_USB },
};

int permissions_apply(int devfd, struct device *device) {
        for (size_t i = 0; i < C_ARRAY_SIZE(device_permissions); i++) {
                uid_t uid = device_permissions[i].uid;
                gid_t gid = device_permissions[i].gid;
                mode_t mode = device_permissions[i].mode;

                if (device_permissions[i].subsystem)
                        if (strcmp(device->devtype->subsystem->name, device_permissions[i].subsystem) != 0)
                                continue;

                if (device->devtype->name && device_permissions[i].devtype)
                        if (strcmp(device->devtype->name, device_permissions[i].devtype) != 0)
                                continue;

                if (mode > 0)
                        if (fchmodat(devfd, device->devname, mode & 07777, 0) < 0)
                                return -errno;

                if (uid > 0 || gid > 0)
                        if (fchownat(devfd, device->devname, uid, gid, AT_SYMLINK_NOFOLLOW) < 0)
                                return -errno;

                break;
        }

        return 0;
}

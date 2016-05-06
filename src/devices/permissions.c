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

#include <string.h>
#include <sys/stat.h>

#include <org.bus1/b1-identity.h>
#include <org.bus1/c-macro.h>

#include "permissions.h"
#include "permissions-usb.h"

static const struct permissions {
        const char *subsystem;
        const char *devtype;
        bool (*match)(int sysfd,
                     int devfd,
                     const char *devname,
                     const char *subsystem,
                     const char *devtype,
                     mode_t *modep,
                     uid_t *uidp,
                     gid_t *gidp);
        mode_t mode;
        uid_t uid;
        gid_t gid;
} device_permissions[] = {
        { "input",        NULL,           NULL,            0660, BUS1_IDENTITY_INPUT, BUS1_IDENTITY_INPUT },
        { "sound",        NULL,           NULL,            0660, BUS1_IDENTITY_AUDIO, BUS1_IDENTITY_AUDIO },
        { "video4linux",  NULL,           NULL,            0660, BUS1_IDENTITY_VIDEO, BUS1_IDENTITY_VIDEO },
        { "block",        NULL,           NULL,            0660, BUS1_IDENTITY_DISK,  BUS1_IDENTITY_DISK },
        { "usb",          "usb_device",   permissions_usb, 0,    0,                   0 },
        { "usb",          "usb_device",   NULL,            0660, BUS1_IDENTITY_USB,   BUS1_IDENTITY_USB },
};

int permissions_apply(int sysfd,
                      int devfd,
                      const char *devname,
                      const char *subsystem,
                      const char *devtype) {
        for (size_t i = 0; i < C_ARRAY_SIZE(device_permissions); i++) {
                uid_t uid = device_permissions[i].uid;
                gid_t gid = device_permissions[i].gid;
                mode_t mode = device_permissions[i].mode;

                if (subsystem && device_permissions[i].subsystem)
                        if (strcmp(subsystem, device_permissions[i].subsystem) != 0)
                                continue;

                if (devtype && device_permissions[i].devtype)
                        if (strcmp(devtype, device_permissions[i].devtype) != 0)
                                continue;

                if (device_permissions[i].match && !device_permissions[i].match(sysfd,
                                                                                devfd,
                                                                                devname,
                                                                                subsystem,
                                                                                devtype,
                                                                                &mode,
                                                                                &uid,
                                                                                &gid))
                        continue;

                if (mode > 0)
                        if (fchmodat(devfd, devname, mode & 07777, 0) < 0)
                                return -errno;

                if (uid > 0 || gid > 0)
                        if (fchownat(devfd, devname, uid, gid, AT_SYMLINK_NOFOLLOW) < 0)
                                return -errno;

                break;
        }

        return 0;
}

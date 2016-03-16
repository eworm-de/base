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

#include "permissions-usb.h"

static const struct {
        uint8_t class;
        uint8_t subclass;
        uint8_t protocol;
        mode_t mode;
        uid_t uid;
        gid_t gid;
} usb_classes[] = {
        { 0x0b, 0x00, 0x00, 0600, BUS1_IDENTITY_SMARTCARD, BUS1_IDENTITY_SMARTCARD },
};

static bool match_usb_class(uint8_t class,
                            int8_t subclass,
                            uint8_t protocol,
                            mode_t *modep,
                            uid_t *uidp,
                            gid_t *gidp) {
        for (size_t i = 0; i < C_ARRAY_SIZE(usb_classes); i++) {
                if (usb_classes[i].class != class ||
                    usb_classes[i].subclass != subclass ||
                    usb_classes[i].protocol != protocol)
                        continue;

                *modep = usb_classes[i].mode;
                *uidp = usb_classes[i].uid;
                *gidp = usb_classes[i].gid;

                return true;
        }

        return false;
}

bool permissions_usb(int sysfd,
                     int devfd,
                     const char *devname,
                     const char *subsystem,
                     const char *devtype,
                     mode_t *modep,
                     uid_t *uidp,
                     gid_t *gidp) {
        struct stat st;
        _c_cleanup_(c_freep) char *descriptor = NULL;
        _c_cleanup_(c_closep) int fd = -1;
        uint8_t buf[18 + 65535];
        ssize_t n;
        size_t o = 0;
        struct usb_interface_descriptor {
                uint8_t bLength;
                uint8_t bDescriptorType;
                uint8_t bInterfaceNumber;
                uint8_t bAlternateSetting;
                uint8_t bNumEndpoints;
                uint8_t bInterfaceClass;
                uint8_t bInterfaceSubClass;
                uint8_t bInterfaceProtocol;
                uint8_t iInterface;
        } _c_packed_;
        struct usb_interface_descriptor *desc;

        if (fstatat(devfd, devname, &st, 0) < 0)
                return false;

        if (!S_ISCHR(st.st_mode))
                return false;

        if (asprintf(&descriptor, "dev/char/%d:%d/descriptors", major(st.st_rdev), minor(st.st_rdev)) < 0)
                return false;

        fd = openat(sysfd, descriptor, O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0)
                return false;

        n = read(fd, buf, sizeof(buf));
        if (n < 18 || n == sizeof(buf))
                return false;

        while (o + sizeof(struct usb_interface_descriptor) < (size_t)n) {
                desc = (struct usb_interface_descriptor *)(buf + o);
                if (desc->bLength < 3)
                        return false;

                o += desc->bLength;

                if (desc->bDescriptorType != 0x04) /* USB_DT_INTERFACE */
                        continue;

                if (match_usb_class(desc->bInterfaceClass,
                                    desc->bInterfaceSubClass,
                                    desc->bInterfaceProtocol,
                                    modep,
                                    uidp,
                                    gidp))
                        return true;
        }

        return false;
}

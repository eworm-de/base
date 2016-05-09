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
#include "permissions.h"

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

struct usb_descriptor {
        struct usb_descriptor_header {
                uint8_t bLength;
                uint8_t bDescriptorType;
        } header;
        union {
                struct usb_descriptor_interface {
                        uint8_t bInterfaceNumber;
                        uint8_t bAlternateSetting;
                        uint8_t bNumEndpoints;
                        uint8_t bInterfaceClass;
                        uint8_t bInterfaceSubClass;
                        uint8_t bInterfaceProtocol;
                        uint8_t iInterface;
                } interface;
        };
} _c_packed_;

static int permissions_usb_apply(struct device *device, int sysfd, void *userdata) {
        _c_cleanup_(c_closep) int fd = -1;
        int devfd = (intptr_t)userdata;
        uint8_t buf[0xffff];
        ssize_t n;
        struct usb_descriptor *desc;
        mode_t mode;
        uid_t uid;
        gid_t gid;

        if (sysfd < 0)
                return 0;

        fd = openat(sysfd, "descriptors", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0)
                return -errno;

        n = read(fd, buf, sizeof(buf));
        if (n < 3 || n == sizeof(buf))
                return -EIO;

        for (ssize_t o = 0; o < n;) {
                desc = (struct usb_descriptor *)(buf + o);
                if (desc->header.bLength < 3)
                        return -EIO;

                o += desc->header.bLength;

                if (desc->header.bDescriptorType != 0x04) /* interface */
                        continue;

                if (match_usb_class(desc->interface.bInterfaceClass,
                                    desc->interface.bInterfaceSubClass,
                                    desc->interface.bInterfaceProtocol,
                                    &mode,
                                    &uid,
                                    &gid)) {
                        return permissions_apply(devfd, device->devname, mode, uid, gid);
                }
        }

        return 0;
}

bool permissions_usb(struct device *device,
                     int devfd,
                     mode_t *modep,
                     uid_t *uidp,
                     gid_t *gidp) {
        (void) device_call_with_sysfd(device, NULL, permissions_usb_apply, (void*)(intptr_t) devfd);

        return false;
}

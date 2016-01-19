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

#include <bus1/c-macro.h>
#include <bus1/c-shared.h>
#include <bus1/b1-encrypt-info.h>
#include <linux/dm-ioctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "encrypt-setup.h"
#include "file-util.h"
#include "util.h"

static int encrypt_get_info(FILE *f, uint64_t *offsetp, uint64_t *sizep, char **crypt_typep) {
        Bus1EncryptInfo info;
        static const char super_uuid[] = BUS1_SUPER_INFO_UUID;
        static const char info_uuid[] = BUS1_ENCRYPT_INFO_UUID;
        _c_cleanup_(c_freep) char *crypt_type = NULL;
        uint64_t size;
        int r;

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        if (size < sizeof(Bus1EncryptInfo))
                return -EINVAL;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.super.super_uuid, super_uuid, sizeof(super_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.super.type_uuid, info_uuid, sizeof(info_uuid)) != 0)
                return -EINVAL;

        if (asprintf(&crypt_type, "%s-%s-%-s", info.encrypt.cypher, info.encrypt.chain_mode, info.encrypt.iv_mode) < 0)
                return -ENOMEM;

        *crypt_typep = crypt_type;
        crypt_type = NULL;
        *offsetp = le64toh(info.data.offset);
        *sizep = le64toh(info.data.size);

        return 0;
}

static int dm_ioctl_new(uint64_t device, unsigned int flags, size_t data_size, struct dm_ioctl **iop) {
        struct dm_ioctl *io;

        io = calloc(1, sizeof(struct dm_ioctl) + data_size);
        if (!io)
                return -ENOMEM;

        io->version[0] = 4;
        io->version[1] = 0;
        io->version[2] = 0;

        io->data_size = sizeof(struct dm_ioctl) + data_size;
        io->data_start = sizeof(struct dm_ioctl);

        io->dev = device;
        io->flags = flags;

        *iop = io;

        return 0;
};

static int encrypt_setup_device(const char *device, const char *name, uint64_t offset, uint64_t size,
                                const char *crypt_type, const char *key,
                                char **devicep) {
        _c_cleanup_(c_freep) struct dm_ioctl *io = NULL;
        _c_cleanup_(c_closep) int fd = -1;
        uint64_t dm_dev;
        unsigned int minor;
        struct dm_target_spec *target;
        _c_cleanup_(c_freep) char *target_parameter = NULL;
        int target_parameter_len;
        int r;

        fd = open("/dev/mapper/control", O_RDWR|O_CLOEXEC);
        if (fd < 0)
                return -errno;

        /* Create new map device. */
        r = dm_ioctl_new(0, 0, 0, &io);
        if (r < 0)
                return r;

        strncpy(io->name, name, sizeof(io->name) - 1);
        if (ioctl(fd, DM_DEV_CREATE, io) < 0)
                return -errno;

        /* Remember the device for the following ioctls. */
        dm_dev = io->dev;
        io = c_free(io);

        /* Load crypt target:
             <cipher>-<chain mode>-<iv mode> <key> <iv_offset> <device path> <offset> <options>
             aes-xts-plain64 0bdcc7f8a1794b92bfa5f9b39cd9b6c63458f47b8520440eab8d61c394fee62a 0 /dev/sda2 0
         */
        target_parameter_len = asprintf(&target_parameter, "%s %s 0 %s %" PRIu64,
                                        crypt_type, key, device, offset / 512);
        if (target_parameter_len < 0)
                return -ENOMEM;

        r = dm_ioctl_new(dm_dev, DM_STATUS_TABLE_FLAG, sizeof(struct dm_target_spec) + target_parameter_len + 1, &io);
        if (r < 0)
                return r;

        io->target_count = 1;
        target = (struct dm_target_spec *)((uint8_t *)io + sizeof(struct dm_ioctl));
        target->sector_start = 0;
        target->length = size / 512;
        strcpy(target->target_type, "crypt");
        memcpy((uint8_t *)target + sizeof(struct dm_target_spec), target_parameter, target_parameter_len);
        if (ioctl(fd, DM_TABLE_LOAD, io) < 0)
                return -errno;

        io = c_free(io);

        /* Start the device. */
        r = dm_ioctl_new(dm_dev, 0, 0, &io);
        if (r < 0)
                return r;

        if (ioctl(fd, DM_DEV_SUSPEND, io) < 0)
                return -errno;

        /* Get device name (we need to extract the minor from the kernel internal dev_t format). */
        minor = (dm_dev & 0xff) | ((dm_dev >> 12) & 0xfff00);
        if (asprintf(devicep, "/dev/dm-%u", minor) < 0)
                return -ENOMEM;

        return 0;
}

int encrypt_setup(const char *device, const char *name, const char *key, char **devicep) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset;
        uint64_t size;
        _c_cleanup_(c_freep) char *crypt_type = NULL;
        _c_cleanup_(c_freep) char *dev = NULL;
        int r;

        f = fopen(device, "r+e");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        r = encrypt_get_info(f, &offset, &size, &crypt_type);
        if (r < 0)
                return r;

        r = encrypt_setup_device(device, name, offset, size,
                                 crypt_type, key,
                                 &dev);
        if (r < 0)
                return r;

        *devicep = dev;
        dev = NULL;

        return 0;
}

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
#include <linux/dm-ioctl.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "bus1/b1-disk-sign-header.h"
#include "disk-sign-util.h"
#include "file-util.h"
#include "string-util.h"
#include "kmsg-util.h"

static int disk_sign_attach_loop(FILE *f, uint64_t offset, char **devicep) {
        _c_cleanup_(c_closep) int fd_loopctl = -1;
        _c_cleanup_(c_closep) int fd_loop = -1;
        _c_cleanup_(c_freep) char *device = NULL;
        struct loop_info64 info = {
                .lo_offset = offset,
        };
        int n;

        fd_loopctl = open("/dev/loop-control", O_RDWR|O_CLOEXEC);
        if (fd_loopctl < 0)
                return -errno;

        n = ioctl(fd_loopctl, LOOP_CTL_GET_FREE);
        if (n < 0)
                return -errno;

        if (asprintf(&device, "/dev/loop%d", n) < 0)
                return -ENOMEM;

        fd_loop = open(device, O_RDWR|O_CLOEXEC);
        if (fd_loop < 0)
                return -errno;

        if (ioctl(fd_loop, LOOP_SET_FD, fileno(f)) < 0)
                return -errno;

        if (ioctl(fd_loop, LOOP_SET_STATUS64, &info) < 0)
                return -errno;

        *devicep = device;
        device = NULL;

        return 0;
}

static int dm_ioctl_new(uint64_t device, unsigned int flags, size_t data_size, struct dm_ioctl **io) {
        struct dm_ioctl *dm;

        dm = calloc(1, sizeof(struct dm_ioctl) + data_size);
        if (!dm)
                return -ENOMEM;

        dm->version[0] = 4;

        dm->data_size = sizeof(struct dm_ioctl) + data_size;
        dm->data_start = sizeof(struct dm_ioctl);

        dm->dev = device;
        dm->flags = flags | DM_READONLY_FLAG;

        *io = dm;

        return 0;
};

static int dm_setup_device(const char *device,
                           const char *name,
                           uint64_t data_size,
                           uint64_t hash_offset,
                           unsigned int data_block_size,
                           unsigned int hash_block_size,
                           const char *hash_name,
                           const char *salt,
                           const char *root_hash,
                           char **map_device) {
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

        /* Load verity target:
             <target version> <data device> <hash device> <data block size> <hash block size> <number of data blocks> <hash offset> <hash algorithm> <root hash> <salt>
             1 /dev/loop0 /dev/loop 4096 4096 46207 1 sha256 bde126215de2ce8d706b1b8117ba4f463ae1a329b547167457eb220d6d83fa85 dc1d34bde3c80c579b8a1fd30d3b1d860160ee44bfd8e37cd0dd7b406353779f
         */
        target_parameter_len = asprintf(&target_parameter, "1 %s %s %u %u %" PRIu64 " %" PRIu64 " %s %s %s",
                                        device, device,
                                        data_block_size, hash_block_size,
                                        data_size / data_block_size, hash_offset / hash_block_size,
                                        hash_name, root_hash, salt);
        if (target_parameter_len < 0)
                return -ENOMEM;

        r = dm_ioctl_new(dm_dev, DM_STATUS_TABLE_FLAG, sizeof(struct dm_target_spec) + target_parameter_len + 1, &io);
        if (r < 0)
                return r;

        io->target_count = 1;
        target = (struct dm_target_spec *)((uint8_t *)io + sizeof(struct dm_ioctl));
        target->length = data_size / 512;
        strcpy(target->target_type, "verity");
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
        if (asprintf(map_device, "/dev/dm-%u", minor) < 0)
                return -ENOMEM;

        return 0;
}

int disk_sign_get_info(FILE *f,
                       char **image_typep,
                       char **image_namep,
                       uint8_t *image_uuidp,
                       char **data_typep,
                       uint64_t *data_offsetp,
                       uint64_t *data_sizep,
                       uint64_t *hash_offsetp,
                       uint64_t *hash_sizep,
                       char **hash_algorithmp,
                       uint64_t *hash_digest_sizep,
                       uint64_t *hash_block_sizep,
                       uint64_t *data_block_sizep,
                       char **saltp,
                       char **root_hashp) {
        Bus1DiskSignHeader info;
        static const char meta_uuid[] = BUS1_META_HEADER_UUID;
        static const char info_uuid[] = BUS1_DISK_SIGN_HEADER_UUID;
        _c_cleanup_(c_freep) char *image_type = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        _c_cleanup_(c_freep) char *algorithm = NULL;
        _c_cleanup_(c_freep) char *salt_str = NULL;
        _c_cleanup_(c_freep) char *hash_str = NULL;
        size_t l;
        int r;

        if (fseeko(f, 0, SEEK_SET) < 0)
                return -errno;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.meta.meta_uuid, meta_uuid, sizeof(meta_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.meta.type_uuid, info_uuid, sizeof(info_uuid)) != 0)
                return -EINVAL;

        image_type = strdup(info.meta.type_tag);
        if (!image_type)
                return -ENOMEM;

        image_name = strdup(info.meta.object_label);
        if (!image_name)
                return -ENOMEM;

        data_type = strdup(info.data.type);
        if (!data_type)
                return -ENOMEM;

        algorithm = strdup(info.hash.algorithm);
        if (!algorithm)
                return -ENOMEM;

        l = le64toh(info.hash.salt_size) / 8;
        if (l >= 256)
                return -EINVAL;

        r = hexstr_from_bytes(info.hash.salt, l, &salt_str);
        if (r < 0)
                return r;

        l = le64toh(info.hash.digest_size) / 8;
        if (l >= 256)
                return -EINVAL;

        r = hexstr_from_bytes(info.hash.root_hash, l, &hash_str);
        if (r < 0)
                return r;

        if (image_typep) {
                *image_typep = image_type;
                image_type = NULL;
        }

        if (image_namep) {
                *image_namep = image_name;
                image_name = NULL;
        }

        if (image_uuidp)
                memcpy(image_uuidp, info.meta.object_uuid, 16);

        if (data_typep) {
                *data_typep = data_type;
                data_type = NULL;
        }

        if (data_offsetp)
                *data_offsetp = le64toh(info.data.offset);

        if (data_sizep)
                *data_sizep = le64toh(info.data.size);

        if (hash_offsetp)
                *hash_offsetp = le64toh(info.hash.offset);

        if (hash_sizep)
                *hash_sizep = le64toh(info.hash.size);

        if (hash_algorithmp) {
                *hash_algorithmp = algorithm;
                algorithm = NULL;
        }

        if (hash_digest_sizep)
                *hash_digest_sizep = le64toh(info.hash.digest_size);

        if (hash_block_sizep)
                *hash_block_sizep = le64toh(info.hash.hash_block_size);

        if (data_block_sizep)
                *data_block_sizep = le64toh(info.hash.data_block_size);

        if (saltp) {
                *saltp = salt_str;
                salt_str = NULL;
        }

        if (root_hashp) {
                *root_hashp = hash_str;
                hash_str = NULL;
        }

        return 0;
}

int disk_sign_setup_device(const char *image, char **devicep, char **image_typep) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *image_type = NULL;
        uint64_t data_offset;
        uint64_t data_size;
        uint64_t hash_offset;
        _c_cleanup_(c_freep) char *hash_algorithm = NULL;
        uint64_t hash_digest_size;
        uint64_t hash_block_size;
        uint64_t data_block_size;
        _c_cleanup_(c_freep) char *salt = NULL;
        _c_cleanup_(c_freep) char *root_hash = NULL;
        _c_cleanup_(c_freep) char *loopdev = NULL;
        _c_cleanup_(c_freep) char *device = NULL;
        int r;

        f = fopen(image, "re");
        if (!f)
                return -errno;

        r = disk_sign_get_info(f,
                               NULL,
                               &image_name,
                               NULL,
                               &image_type,
                               &data_offset,
                               &data_size,
                               &hash_offset,
                               NULL,
                               &hash_algorithm,
                               &hash_digest_size,
                               &hash_block_size,
                               &data_block_size,
                               &salt,
                               &root_hash);
        if (r < 0)
                return r;

        if (data_offset % 4096 > 0 ||
            data_size % 4096 > 0 ||
            hash_offset % 4096 > 0 ||
            hash_digest_size % 128 > 0 || hash_digest_size > 4096 ||
            hash_block_size % 4096 > 0 ||
            data_block_size % 4096 > 0 ||
            data_offset > hash_offset)
                return -EINVAL;

        r = disk_sign_attach_loop(f, data_offset, &loopdev);
        if (r < 0)
                return r;

        r = dm_setup_device(loopdev,
                            image_name,
                            data_size,
                            hash_offset - data_offset,
                            hash_block_size,
                            data_block_size,
                            hash_algorithm,
                            salt,
                            root_hash,
                            &device);
        if (r < 0)
                return r;

        if (devicep) {
                *devicep = device;
                device = NULL;
        }

        if (image_typep) {
                *image_typep = image_type;
                image_type = NULL;
        }

        return 0;
}

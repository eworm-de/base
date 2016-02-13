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

#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include <org.bus1/b1-disk-encrypt-header.h>
#include <linux/dm-ioctl.h>
#include <linux/random.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "aeswrap.h"
#include "disk-encrypt.h"
#include "disk.h"
#include "file.h"
#include "missing.h"
#include "string.h"
#include "uuid.h"

int disk_encrypt_get_info(FILE *f,
                          char **image_typep,
                          char **image_namep,
                          uint8_t *image_uuidp,
                          char **data_typep,
                          uint64_t *data_offsetp,
                          uint64_t *data_sizep,
                          char **encryptionp,
                          char **master_key_encryptionp,
                          uint8_t *master_keyp,
                          uint64_t *master_key_sizep,
                          uint64_t *n_key_slotsp,
                          uint8_t *key0_type_uuidp,
                          char **key0_encryptionp,
                          uint8_t *key0p,
                          uint64_t *key0_sizep) {
        Bus1DiskEncryptHeader info;
        Bus1DiskEncryptKeySlot keys[1];
        static const char meta_uuid[] = BUS1_META_HEADER_UUID;
        static const char info_uuid[] = BUS1_DISK_ENCRYPT_HEADER_UUID;
        _c_cleanup_(c_freep) char *image_type = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        _c_cleanup_(c_freep) char *encryption = NULL;
        _c_cleanup_(c_freep) char *master_key_encryption = NULL;
        _c_cleanup_(c_freep) char *key0_encryption = NULL;
        uint64_t master_key_size;
        uint64_t key0_size;
        uint64_t size;
        int r;

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        if (size < sizeof(info))
                return -EINVAL;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.meta.meta_uuid, meta_uuid, sizeof(meta_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.meta.type_uuid, info_uuid, sizeof(info_uuid)) != 0)
                return -EINVAL;

        master_key_size = le64toh(info.master_key.key_size);
        if (master_key_size < 128 || master_key_size > 2048)
                return -EINVAL;

        if (le64toh(info.n_key_slots) < 1)
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

        if (asprintf(&encryption, "%s-%s-%-s", info.encrypt.cypher, info.encrypt.chain_mode, info.encrypt.iv_mode) < 0)
                return -ENOMEM;

        master_key_encryption = strdup(info.master_key.encryption);
        if (!master_key_encryption)
                return -ENOMEM;

        if (fread(&keys, sizeof(keys), 1, f) != 1)
                return -EIO;

        key0_size = le64toh(keys[0].key_size);
        if (key0_size < 128 || key0_size > 2048)
                return -EINVAL;

        key0_encryption = strdup(keys[0].encryption);
        if (!key0_encryption)
                return -ENOMEM;

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

        if (encryptionp) {
                *encryptionp = encryption;
                encryption = NULL;
        }

        if (master_key_encryptionp) {
                *master_key_encryptionp = master_key_encryption;
                master_key_encryption = NULL;
        }

        if (master_keyp)
                memcpy(master_keyp, info.master_key.key, master_key_size / 8);

        if (master_key_sizep)
                *master_key_sizep = master_key_size;

        if (n_key_slotsp)
                *n_key_slotsp = le64toh(info.n_key_slots);

        if (key0_type_uuidp)
                memcpy(key0_type_uuidp, keys[0].type_uuid, 16);

        if (key0_encryptionp) {
                *key0_encryptionp = key0_encryption;
                key0_encryption = NULL;
        }

        if (key0p)
                memcpy(key0p, keys[0].key, key0_size / 8);

        if (key0_sizep)
                *key0_sizep = key0_size;

        return 0;
}

static int dm_ioctl_new(uint64_t device, unsigned int flags, uint64_t data_size, struct dm_ioctl **iop) {
        struct dm_ioctl *io;

        io = calloc(1, sizeof(struct dm_ioctl) + data_size);
        if (!io)
                return -ENOMEM;

        io->version[0] = 4;

        io->data_size = sizeof(struct dm_ioctl) + data_size;
        io->data_start = sizeof(struct dm_ioctl);

        io->dev = device;
        io->flags = flags;

        *iop = io;

        return 0;
};

static int dm_setup_device(const char *device, const char *name,
                                uint64_t offset, uint64_t size,
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
        target_parameter_len = asprintf(&target_parameter, "%s %s 0 %s %" PRIu64 " 1 allow_discards",
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

int disk_encrypt_setup_device(const char *device, char **devicep, char **image_namep, char **data_typep) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset;
        uint64_t size;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        _c_cleanup_(c_freep) char *encryption = NULL;
        _c_cleanup_(c_freep) char *master_key_encryption = NULL;
        uint8_t master_key_encrypted[256];
        uint64_t master_key_encrypted_size;
        uint8_t key0_type_uuid[16];
        _c_cleanup_(c_freep) char *key0_encryption = NULL;
        uint8_t key0_encrypted[256];
        uint64_t key0_size;
        static const char key_clear_uuid[] = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID;
        _c_cleanup_(c_freep) char *hexkey = NULL;
        _c_cleanup_(c_freep) char *dev = NULL;
        static const uint8_t null_key[256] = {};
        uint64_t master_key_size;
        uint8_t master_key_unlock[32];
        uint8_t master_key[32];
        int r;

        f = fopen(device, "r+e");
        if (!f)
                return -errno;

        r = disk_encrypt_get_info(f,
                                  NULL,
                                  &image_name,
                                  NULL,
                                  &data_type,
                                  &offset,
                                  &size,
                                  &encryption,
                                  &master_key_encryption,
                                  master_key_encrypted,
                                  &master_key_encrypted_size,
                                  NULL,
                                  key0_type_uuid,
                                  &key0_encryption,
                                  key0_encrypted,
                                  &key0_size);
        if (r < 0)
                return r;

        if (strcmp(master_key_encryption, "aes-wrap") != 0)
                return -EINVAL;

        if (memcmp(key0_type_uuid, key_clear_uuid, sizeof(key_clear_uuid)) != 0)
                return -EINVAL;

        if (strcmp(key0_encryption, "aes-wrap") != 0)
                return -EINVAL;

        /* AES-WRAP adds 64 bits to the output. */
        master_key_size = master_key_encrypted_size - 64;

        /* Decrypt the clear key. */
        r = aeswrap_decrypt_data(null_key,
                                 master_key_size,
                                 key0_encrypted,
                                 key0_size,
                                 master_key_unlock);
        if (r < 0)
                return r;

        /* Decrypt the master volume key. */
        r = aeswrap_decrypt_data(master_key_unlock,
                                 master_key_size,
                                 master_key_encrypted,
                                 master_key_encrypted_size,
                                 master_key);
        if (r < 0)
                return r;

        r = hexstr_from_bytes(master_key, master_key_size / 8, &hexkey);
        if (r < 0)
                return r;

        r = dm_setup_device(device,
                            image_name,
                            offset,
                            size,
                            encryption,
                            hexkey,
                            &dev);
        if (r < 0)
                return r;

        if (devicep) {
                *devicep = dev;
                dev = NULL;
        }

        if (data_typep) {
                *data_typep = data_type;
                data_type = NULL;
        }

        if (image_namep) {
                *image_namep = image_name;
                image_name = NULL;
        }

        memwipe(hexkey, strlen(hexkey));
        memwipe(master_key, sizeof(master_key));
        memwipe(master_key_unlock, sizeof(master_key_unlock));

        return 0;
}

int disk_encrypt_format_volume(const char *device,
                               const char *image_name,
                               const char *data_type) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset, size = 0;
        uint8_t master_key[32];
        uint64_t master_key_size = 256;
        uint8_t master_key_unlock[32];
        static const uint8_t null_key[32] = {};
        Bus1DiskEncryptKeySlot keys[8] = {
                {
                        .type_uuid = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID,
                        .encryption = "aes-wrap",
                },
        };
        Bus1DiskEncryptHeader info = {
                .meta.meta_uuid = BUS1_META_HEADER_UUID,
                .meta.type_uuid = BUS1_DISK_ENCRYPT_HEADER_UUID,
                .meta.type_tag = "org.bus1.encrypt",

                .encrypt.cypher = "aes",
                .encrypt.chain_mode = "xts",
                .encrypt.iv_mode = "plain64",

                .master_key.key_size = htole64(master_key_size),
                .master_key.encryption = "aes-wrap",

                .n_key_slots = htole64(C_ARRAY_SIZE(keys)),
        };
        int r;

        assert(device);
        assert(image_name);
        assert(data_type);

        strncpy(info.meta.object_label, image_name, sizeof(info.meta.object_label) - 1);
        strncpy(info.data.type, data_type, sizeof(info.data.type) - 1);

        r = uuid_set_random(info.meta.object_uuid);
        if (r < 0)
                return r;

        f = fopen(device, "r+");
        if (!f)
                return -errno;

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        block_discard_range(f, 0, 1024ULL * 1024ULL, true);
        block_discard_range(f, 1024ULL * 1024ULL, size, false);

        size -= size  % 4096;
        offset = sizeof(info) + sizeof(keys);

        info.data.offset = htole64(offset);
        info.data.size = htole64(size - offset);

        if (getrandom(master_key, master_key_size / 8, 0) < 0)
                return -errno;

        if (getrandom(master_key_unlock, master_key_size / 8, 0) < 0)
                return -errno;

        /* Encrypt the master volume key. */
        r = aeswrap_encrypt_data(master_key_unlock,
                                 master_key_size,
                                 master_key,
                                 info.master_key.key,
                                 &info.master_key.key_size);
        if (r < 0)
                return r;

        /* Encrypt the clear key. */
        r = aeswrap_encrypt_data(null_key,
                                 master_key_size,
                                 master_key_unlock,
                                 keys[0].key,
                                 &keys[0].key_size);
        if (r < 0)
                return r;

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (fwrite(&keys, sizeof(keys), 1, f) != 1)
                return -EIO;

        if (fflush(f) < 0)
                return -errno;

        memwipe(master_key, sizeof(master_key));
        memwipe(master_key_unlock, sizeof(master_key_unlock));

        return 0;
}

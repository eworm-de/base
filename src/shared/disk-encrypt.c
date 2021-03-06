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
#include <org.bus1/b1-disk-encrypt-header.h>
#include <linux/dm-ioctl.h>
#include <linux/random.h>
#include <string.h>
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
                          uint64_t *n_keysp,
                          Bus1DiskEncryptKeySlot **keysp) {
        Bus1DiskEncryptHeader info;
        _c_cleanup_(c_freep) Bus1DiskEncryptKeySlot *key_slots = NULL;
        uint64_t n_key_slots;
        static const char meta_uuid[] = BUS1_META_HEADER_UUID;
        static const char info_uuid[] = BUS1_DISK_ENCRYPT_HEADER_UUID;
        _c_cleanup_(c_freep) char *image_type = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        _c_cleanup_(c_freep) char *encryption = NULL;
        _c_cleanup_(c_freep) char *master_key_encryption = NULL;
        uint64_t n_keys;
        _c_cleanup_(c_freep) Bus1DiskEncryptKeySlot *keys = NULL;
        _c_cleanup_(c_freep) char *key_clear_encryption = NULL;
        uint64_t master_key_size;
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
        if (master_key_size < 16 || master_key_size > 256)
                return -EINVAL;

        n_key_slots = le64toh(info.n_key_slots);
        if (n_key_slots < 1 || n_key_slots > 256)
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

        key_slots = calloc(n_key_slots, sizeof(Bus1DiskEncryptKeySlot));
        if (!key_slots)
                return -ENOMEM;

        if (fread(key_slots, sizeof(Bus1DiskEncryptKeySlot), n_key_slots, f) != n_key_slots)
                return -EIO;

        /* Count the used key slots. */
        n_keys = 0;
        for (uint64_t i = 0; i < n_key_slots; i++) {
                static const char null_uuid[16] = {};

                if (memcmp(key_slots[i].type_uuid, null_uuid, sizeof(null_uuid)) != 0)
                        n_keys++;
        }

        keys = calloc(n_keys, sizeof(Bus1DiskEncryptKeySlot));
        if (!keys)
                return -ENOMEM;

        n_keys = 0;
        for (uint64_t i = 0; i < n_key_slots; i++) {
                static const char null_uuid[16] = {};
                static const char clear_uuid[] = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID;
                static const char recovery_uuid[] = BUS1_DISK_ENCRYPT_KEY_RECOVERY_UUID;

                if (memcmp(key_slots[i].type_uuid, null_uuid, 16) == 0)
                        continue;

                if (memcmp(key_slots[i].type_uuid, clear_uuid, 16) == 0) {
                        uint64_t key_size;

                        memcpy(keys[n_keys].type_uuid, key_slots[i].type_uuid, 16);
                        memcpy(keys[n_keys].object_uuid, key_slots[i].object_uuid, 16);
                        strncpy(keys[n_keys].clear.encryption, key_slots[i].clear.encryption, sizeof(keys[i].clear.encryption) - 1);

                        key_size = le64toh(key_slots[i].clear.key_size);
                        if (key_size < 16 || key_size > 256)
                                continue;

                        keys[n_keys].clear.key_size = key_size;
                        memcpy(keys[n_keys].clear.key, key_slots[i].clear.key, key_size);
                        n_keys++;

                        continue;
                }

                if (memcmp(key_slots[i].type_uuid, recovery_uuid, 16) == 0) {
                        uint64_t key_size;

                        memcpy(keys[n_keys].type_uuid, key_slots[i].type_uuid, 16);
                        memcpy(keys[n_keys].object_uuid, key_slots[i].object_uuid, 16);
                        strncpy(keys[n_keys].recovery.encryption, key_slots[i].recovery.encryption, sizeof(keys[i].recovery.encryption) - 1);

                        key_size = le64toh(key_slots[i].recovery.key_size);
                        if (key_size < 16 || key_size > 256)
                                continue;

                        keys[n_keys].recovery.key_size = key_size;
                        memcpy(keys[n_keys].recovery.key, key_slots[i].recovery.key, key_size);
                        n_keys++;

                        continue;
                }
        }

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
                memcpy(master_keyp, info.master_key.key, master_key_size);

        if (master_key_sizep)
                *master_key_sizep = master_key_size;

        if (n_keysp)
                *n_keysp = n_keys;

        if (keysp) {
                *keysp = keys;
                keys = NULL;
        }

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
        uint64_t n_keys;
        _c_cleanup_(c_freep) Bus1DiskEncryptKeySlot *keys = NULL;
        static const char key_clear_uuid[] = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID;
        Bus1DiskEncryptKeySlot *key_clear = NULL;
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
                                  &n_keys,
                                  &keys);
        if (r < 0)
                return r;

        if (strcmp(master_key_encryption, "aes-wrap") != 0)
                return -EINVAL;

        /* Find the clear key. */
        for (uint64_t i = 0; i < n_keys; i++)
                if (memcmp(keys[i].type_uuid, key_clear_uuid, sizeof(key_clear_uuid)) == 0) {
                        key_clear = &keys[i];
                        break;
                }

        if (!key_clear)
                return -EINVAL;

        if (strcmp(key_clear->clear.encryption, "aes-wrap") != 0)
                return -EINVAL;

        /* AES-WRAP adds 8 bytes to the output. */
        master_key_size = master_key_encrypted_size - 8;

        /* Decrypt the clear key. */
        r = aeswrap_decrypt_data(null_key,
                                 master_key_size,
                                 key_clear->clear.key,
                                 key_clear->clear.key_size,
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

        r = hexstr_from_bytes(master_key, master_key_size, &hexkey);
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
                               const char *data_type,
                               uint8_t *recovery_keyp,
                               uint64_t *recovery_key_sizep) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset, size = 0;
        uint8_t master_key[32];
        uint64_t master_key_size = 32;
        uint8_t master_key_unlock[32];
        static const uint8_t null_key[32] = {};
        uint8_t recovery_key[32];
        uint64_t key_size;
        Bus1DiskEncryptKeySlot keys[8] = {
                {
                        .type_uuid = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID,
                        .clear.encryption = "aes-wrap",
                },
                {
                        .type_uuid = BUS1_DISK_ENCRYPT_KEY_RECOVERY_UUID,
                        .recovery.encryption = "aes-wrap",
                },
        };
        Bus1DiskEncryptHeader info = {
                .meta.meta_uuid = BUS1_META_HEADER_UUID,
                .meta.type_uuid = BUS1_DISK_ENCRYPT_HEADER_UUID,
                .meta.type_tag = "org.bus1.disk.encrypt",

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

        /* Get and encrypt the master volume key. */
        if (getrandom(master_key, master_key_size, 0) < 0)
                return -errno;

        if (getrandom(master_key_unlock, master_key_size, 0) < 0)
                return -errno;

        r = aeswrap_encrypt_data(master_key_unlock,
                                 master_key_size,
                                 master_key,
                                 info.master_key.key,
                                 &key_size);
        if (r < 0)
                return r;

        info.master_key.key_size = htole64(key_size);

        if (getrandom(keys[0].object_uuid, sizeof(keys[0].object_uuid), 0) < 0)
                return -errno;

        /* Encrypt the clear key. */
        r = aeswrap_encrypt_data(null_key,
                                 master_key_size,
                                 master_key_unlock,
                                 keys[0].clear.key,
                                 &key_size);
        if (r < 0)
                return r;

        keys[0].clear.key_size = htole64(key_size);

        if (getrandom(keys[1].object_uuid, sizeof(keys[1].object_uuid), 0) < 0)
                return -errno;

        /* Get and encrypt the recovery key. */
        if (getrandom(recovery_key, master_key_size, 0) < 0)
                return -errno;

        r = aeswrap_encrypt_data(recovery_key,
                                 master_key_size,
                                 master_key_unlock,
                                 keys[1].recovery.key,
                                 &key_size);
        if (r < 0)
                return r;

        keys[1].recovery.key_size = htole64(key_size);

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (fwrite(&keys, sizeof(keys), 1, f) != 1)
                return -EIO;

        if (fflush(f) < 0)
                return -errno;

        if (recovery_keyp)
                memcpy(recovery_keyp, recovery_key, master_key_size);

        if (recovery_key_sizep)
                *recovery_key_sizep = master_key_size;

        memwipe(master_key, sizeof(master_key));
        memwipe(master_key_unlock, sizeof(master_key_unlock));
        memwipe(recovery_key, sizeof(master_key_unlock));

        return 0;
}

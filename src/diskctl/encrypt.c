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
#include <bus1/b1-disk-encrypt-header.h>
#include <linux/random.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "disk-encrypt-util.h"
#include "encrypt.h"
#include "file-util.h"
#include "missing.h"
#include "string-util.h"
#include "uuid-util.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

int disk_encrypt_print_info(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        _c_cleanup_(c_freep) char *image_type = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        uint8_t image_uuid[16];
        _c_cleanup_(c_freep) char *image_uuid_str = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        uint64_t data_offset;
        uint64_t data_size;
        _c_cleanup_(c_freep) char *encryption = NULL;
        uint64_t key_size;
        uint64_t n_key_slots;
        uint8_t key_type_uuid[16];
        _c_cleanup_(c_freep) char *key_type_uuid_str = NULL;
        _c_cleanup_(c_freep) char *key = NULL;
        int r;

        f = fopen(data, "re");
        if (!f)
                return -errno;

        r = disk_encrypt_get_info(f,
                                  &image_type,
                                  &image_name,
                                  image_uuid,
                                  &data_type,
                                  &data_offset,
                                  &data_size,
                                  &encryption,
                                  &key_size,
                                  &n_key_slots,
                                  key_type_uuid,
                                  &key);
        if (r < 0)
                return r;

        r = uuid_to_string(image_uuid, &image_uuid_str);
        if (r < 0)
                return r;

        r = uuid_to_string(key_type_uuid, &key_type_uuid_str);
        if (r < 0)
                return r;

        printf("====================================================================================\n");
        printf("Info for:           %s\n", data);
        printf("Image type:         %s\n", image_type);
        printf("Image name:         %s\n", image_name);
        printf("Image UUID:         %s\n", image_uuid_str);
        printf("Data type:          %s\n", data_type);
        printf("Data offset:        %" PRIu64 " bytes\n", data_offset);
        printf("Data size:          %" PRIu64 " bytes\n", data_size);
        printf("Encryption:         %s\n", encryption);
        printf("Key size:           %" PRIu64 " bits\n", key_size);
        printf("Key slots:          %" PRIu64 "\n", n_key_slots);
        printf("Key[00] type:       %s\n", key_type_uuid_str);
        printf("Key[00] key:        %s\n", key);
        printf("====================================================================================\n");

        return 0;
}

static int block_discard_range(FILE *f, uint64_t start, uint64_t len) {
        while (len > 0) {
                uint64_t range[2];
                uint64_t chunk;

                chunk = c_min(len, 1024ULL * 1024ULL * 1024ULL);
                range[0] = start;
                range[1] = chunk;

                if (ioctl(fileno(f), BLKDISCARD, &range) < 0)
                        return -errno;

                len -= chunk;
                start += chunk;
        }

        return 0;
}

int disk_encrypt_format_volume(const char *data_file, const char *image_name, const char *data_type) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset, size = 0;
        uint64_t key_size = 256;
        Bus1DiskEncryptKeySlot keys[8] = {
                {
                        .type_uuid = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID,
                        .clear.encryption = "fixme",
                },
        };
        Bus1DiskEncryptHeader info = {
                .meta.meta_uuid = BUS1_META_HEADER_UUID,
                .meta.type_uuid = BUS1_DISK_ENCRYPT_HEADER_UUID,
                .meta.type_tag = "org.bus1.encrypt",
                .encrypt.cypher = "aes",
                .encrypt.chain_mode = "xts",
                .encrypt.iv_mode = "plain64",
                .key.size = htole64(key_size),
                .key.n_slots = htole64(C_ARRAY_SIZE(keys)),
        };
        int r;

        assert(data_file);
        assert(image_name);
        assert(data_type);

        strncpy(info.meta.object_label, image_name, sizeof(info.meta.object_label) - 1);
        strncpy(info.data.type, data_type, sizeof(info.data.type) - 1);

        r = uuid_set_random(info.meta.object_uuid);
        if (r < 0)
                return EXIT_FAILURE;

        f = fopen(data_file, "r+");
        if (!f)
                return -errno;

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        block_discard_range(f, 0, size);

        size -= size  % 4096;
        offset = sizeof(info) + sizeof(keys);

        info.data.offset = htole64(offset);
        info.data.size = htole64(size - offset);

        /* FIXME: use Authenticated Encryption method also for NULL key */
        if (getrandom(keys[0].key, key_size / 8, GRND_NONBLOCK) < 0)
                return -errno;

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (fwrite(&keys, sizeof(keys), 1, f) != 1)
                return -EIO;

        if (fflush(f) < 0)
                return -errno;

        return 0;
}

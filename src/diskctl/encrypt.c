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

#include "encrypt.h"
#include "file-util.h"
#include "uuid-util.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

int encrypt_print_info(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t size;
        Bus1DiskEncryptHeader info;
        static const char meta_uuid[] = BUS1_META_HEADER_UUID;
        static const char encrypt_uuid[] = BUS1_DISK_ENCRYPT_HEADER_UUID;
        _c_cleanup_(c_freep) char *uuid = NULL;
        int r;

        f = fopen(data, "re");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        if (size <= (off_t)sizeof(Bus1DiskEncryptHeader))
                return -EINVAL;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.meta.meta_uuid, meta_uuid, sizeof(meta_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.meta.type_uuid, encrypt_uuid, sizeof(encrypt_uuid)) != 0)
                return -EINVAL;

        r = uuid_to_string(info.meta.object_uuid, &uuid);
        if (r < 0)
                return r;

        printf("=================================================\n");
        printf("Info for:    %s\n", data);
        printf("Volume Name: %s\n", info.meta.object_label);
        printf("Volume UUID: %s\n", uuid);
        printf("Data type:   %s\n", info.data.type);
        printf("Data offset: %" PRIu64 " bytes\n", le64toh(info.data.offset));
        printf("Data size:   %" PRIu64 " bytes\n", le64toh(info.data.size));
        printf("Encryption:  %s-%s-%s\n", info.encrypt.cypher, info.encrypt.chain_mode, info.encrypt.iv_mode);
        printf("=================================================\n");

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

#define ENCRYPT_HEADER_SPACE 4096

int encrypt_setup_volume(const char *data_file,
                         const char *image_name, const char *data_type) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset, size = 0;
        Bus1DiskEncryptHeader info = {
                .meta.meta_uuid = BUS1_META_HEADER_UUID,
                .meta.type_uuid = BUS1_DISK_ENCRYPT_HEADER_UUID,
                .meta.type_tag = "org.bus1.encrypt",
                .encrypt.cypher = "aes",
                .encrypt.chain_mode = "xts",
                .encrypt.iv_mode = "plain64",
        };
        int r;

        assert(data_file);
        assert(image_name);
        assert(data_type);
        assert(sizeof(Bus1DiskEncryptHeader) < ENCRYPT_HEADER_SPACE);

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
        offset = ENCRYPT_HEADER_SPACE;

        info.data.offset = htole64(offset);
        info.data.size = htole64(size - offset);

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (fflush(f) < 0)
                return -errno;

        return 0;
}

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
#include <linux/random.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "encrypt.h"
#include "file-util.h"
#include "util.h"
#include "uuid-util.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

int encrypt_print_info(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t size;
        Bus1EncryptInfo info;
        static const char super_uuid[] = BUS1_SUPER_INFO_UUID;
        static const char encrypt_uuid[] = BUS1_ENCRYPT_INFO_UUID;
        _c_cleanup_(c_freep) char *uuid = NULL;
        int r;

        f = fopen(data, "re");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        if (size <= (off_t)sizeof(Bus1EncryptInfo))
                return -EINVAL;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.super.super_uuid, super_uuid, sizeof(super_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.super.type_uuid, encrypt_uuid, sizeof(encrypt_uuid)) != 0)
                return -EINVAL;

        r = uuid_to_string(info.super.object_uuid, &uuid);
        if (r < 0)
                return r;

        printf("=================================================\n");
        printf("Info for:    %s\n", data);
        printf("Volume Name: %s\n", info.super.object_label);
        printf("Volume UUID: %s\n", uuid);
        printf("Data offset: %" PRIu64 " bytes\n", le64toh(info.data.offset));
        printf("Data size:   %" PRIu64 " bytes\n", le64toh(info.data.size));
        printf("Encryption:  %s-%s-%s\n", info.encrypt.cypher, info.encrypt.chain_mode, info.encrypt.iv_mode);
        printf("==================================================\n");

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

int encrypt_setup_volume(const char *data_file, const char *name) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint64_t offset, size = 0;
        Bus1EncryptInfo info = {
                .super.super_uuid = BUS1_SUPER_INFO_UUID,
                .super.type_uuid = BUS1_ENCRYPT_INFO_UUID,
                .super.type_tag = "org.bus1.encrypt",
                .encrypt.cypher = "aes",
                .encrypt.chain_mode = "xts",
                .encrypt.iv_mode = "plain64",
        };
        int r;

        assert(data_file);
        assert(name);

        strncpy(info.super.object_label, name, sizeof(info.super.object_label) - 1);

        r = uuid_set_random(info.super.object_uuid);
        if (r < 0)
                return EXIT_FAILURE;

        f = fopen(data_file, "r+");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        r = file_get_size(f, &size);
        if (r < 0)
                return r;

        block_discard_range(f, 0, size);

        size -= size  % 4096;
        offset = 4096;

        info.data.offset = htole64(offset);
        info.data.size = htole64(size - offset);

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        return 0;
}

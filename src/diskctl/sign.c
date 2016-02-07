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
#include <org.bus1/b1-disk-sign-header.h>
#include <linux/random.h>
#include <sys/stat.h>

#include "disk-sign-util.h"
#include "file-util.h"
#include "uuid-util.h"
#include "missing.h"

#include "sign.h"

int disk_sign_print_info(const char *filename) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *image_type = NULL;
        uint8_t image_uuid[16];
        _c_cleanup_(c_freep) char *data_type = NULL;
        uint64_t data_offset;
        uint64_t data_size;
        uint64_t hash_offset;
        uint64_t hash_size;
        _c_cleanup_(c_freep) char *hash_algorithm = NULL;
        uint64_t hash_digest_size;
        uint64_t hash_block_size;
        uint64_t data_block_size;
        _c_cleanup_(c_freep) char *uuid = NULL;
        _c_cleanup_(c_freep) char *salt = NULL;
        _c_cleanup_(c_freep) char *root_hash = NULL;
        int r;

        f = fopen(filename, "re");
        if (!f)
                return -errno;

        r = disk_sign_get_info(f,
                                &image_type,
                                &image_name,
                                image_uuid,
                                &data_type,
                                &data_offset,
                                &data_size,
                                &hash_offset,
                                &hash_size,
                                &hash_algorithm,
                                &hash_digest_size,
                                &hash_block_size,
                                &data_block_size,
                                &salt,
                                &root_hash);
        if (r < 0)
                return r;

        r = uuid_to_string(image_uuid, &uuid);
        if (r < 0)
                return r;

        printf("==================================================================================\n");
        printf("Info for:         %s\n", filename);
        printf("Image type:       %s\n", image_type);
        printf("Image Name:       %s\n", image_name);
        printf("Image UUID:       %s\n", uuid);
        printf("Data type:        %s\n", data_type);
        printf("Data offset:      %" PRIu64 " bytes\n", data_offset);
        printf("Data size:        %" PRIu64 " bytes\n", data_size);
        printf("Hash tree offset: %" PRIu64 " bytes\n", hash_offset);
        printf("Hash tree size:   %" PRIu64 " bytes\n", hash_size);
        printf("Hash algorithm:   %s\n", hash_algorithm);
        printf("Hash digest size: %" PRIu64 " bits\n", hash_digest_size);
        printf("Data block size:  %" PRIu64 " bytes (%" PRIu64 " blocks)\n", data_block_size, data_size / data_block_size);
        printf("Hash Block size:  %" PRIu64 " bytes (%" PRIu64 " blocks)\n", hash_block_size, hash_size / hash_block_size);
        printf("Salt:             %s\n", salt);
        printf("Root hash:        %s\n", root_hash);
        printf("==================================================================================\n");

        return 0;
}

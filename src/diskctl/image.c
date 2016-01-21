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
#include <bus1/b1-disk-image-header.h>
#include <linux/random.h>
#include <sys/stat.h>

#include "disk-image-util.h"
#include "image.h"
#include "hash-tree.h"
#include "util.h"
#include "uuid-util.h"

int image_print_info(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        uint8_t image_uuid[16];
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

        f = fopen(data, "re");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        r = disk_image_get_info(f, &image_name,
                                image_uuid,
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
        printf("Info for:         %s\n", data);
        printf("Image Name:       %s\n", image_name);
        printf("Image UUID:       %s\n", uuid);
        printf("Data offset:      %" PRIu64 " bytes\n", data_offset);
        printf("Data size:        %" PRIu64 " bytes\n", data_size);
        printf("Hash tree offset: %" PRIu64 " bytes\n", hash_offset);
        printf("Hash tree size:   %" PRIu64 " bytes\n", hash_size);
        printf("Hash algorithm:   %s\n", hash_algorithm);
        printf("Hash digest size: %" PRIu64 " bits\n", hash_digest_size);
        printf("Hash Block size:  %" PRIu64 " bits\n", hash_block_size);
        printf("Data block size:  %" PRIu64 " bits\n", data_block_size);
        printf("Salt:             %s\n", salt);
        printf("Root hash:        %s\n", root_hash);
        printf("==================================================================================\n");

        return 0;
}

int image_append_hash(const char *data_file, const char *name) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        struct stat sb;
        uint64_t digest_size = 256;
        uint64_t data_size;
        uint64_t hash_size;
        uint64_t hash_block_size = 4096;
        uint64_t data_block_size = 4096;
        uint64_t salt_size = 256;
        Bus1DiskImageHeader info = {
                .meta.meta_uuid = BUS1_META_HEADER_UUID,
                .meta.type_uuid = BUS1_DISK_IMAGE_HEADER_UUID,
                .meta.type_tag = "org.bus1.image",
                .hash.algorithm = "sha256",
                .hash.digest_size = htole64(digest_size),
                .hash.hash_block_size = htole64(hash_block_size),
                .hash.data_block_size = htole64(data_block_size),
                .hash.salt_size = htole64(salt_size),
        };
        int r;

        assert(data_file);
        assert(name);

        strncpy(info.meta.object_label, name, sizeof(info.meta.object_label) - 1);

        r = uuid_set_random(info.meta.object_uuid);
        if (r < 0)
                return EXIT_FAILURE;

        f = fopen(data_file, "r+");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        if (fstat(fileno(f), &sb) < 0)
                return -errno;

        data_size = sb.st_size;
        info.data.size = htole64(data_size);
        info.hash.offset = htole64(data_size);

        if (getrandom(info.hash.salt, info.hash.salt_size / 8, GRND_NONBLOCK) < 0)
                return EXIT_FAILURE;

        r  = hash_tree_create(info.hash.algorithm,
                              digest_size / 8,
                              data_file,
                              data_block_size,
                              data_size / data_block_size,
                              data_file,
                              hash_block_size,
                              data_size / data_block_size,
                              info.hash.salt,
                              salt_size / 8,
                              info.hash.root_hash,
                              &hash_size);
        if (r < 0)
                return EXIT_FAILURE;

        info.hash.size = htole64(hash_size);

        if (fseeko(f, 0, SEEK_END) < 0)
                return -errno;

        if (fwrite(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        return 0;
}

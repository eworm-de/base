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
#include "file-util.h"
#include "util.h"
#include "uuid-util.h"

#include "hash-tree.h"
#include "image.h"

int image_print_info(const char *filename) {
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

        f = fopen(filename, "re");
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
        printf("Info for:         %s\n", filename);
        printf("Image Name:       %s\n", image_name);
        printf("Image UUID:       %s\n", uuid);
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

#define IMAGE_HEADER_SPACE 4096

int image_write(const char *filename_data, const char *filename_image, const char *name) {
        _c_cleanup_(c_fclosep) FILE *f_data = NULL;
        _c_cleanup_(c_fclosep) FILE *f_image = NULL;
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
        uint64_t hashes_per_block;
        int r;

        assert(filename_data);
        assert(filename_image);
        assert(name);
        assert(sizeof(Bus1DiskImageHeader) < IMAGE_HEADER_SPACE);

        f_data = fopen(filename_data, "re");
        if (!f_data)
                return -errno;

        r = file_get_size(f_data, &data_size);
        if (r < 0)
                return r;

        /* We expect at least one stored hash block. */
        hashes_per_block = hash_block_size / (digest_size / 8);
        if (data_size < data_block_size * hashes_per_block)
                return -EINVAL;

        f_image = fopen(filename_image, "w+e");
        if (!f_image)
                return -errno;

        if (fseeko(f_image, IMAGE_HEADER_SPACE, SEEK_SET) < 0)
                return -errno;

        r = file_copy(f_data, f_image, NULL);
        if (r < 0)
                return r;

        strncpy(info.meta.object_label, name, sizeof(info.meta.object_label) - 1);

        r = uuid_set_random(info.meta.object_uuid);
        if (r < 0)
                return r;

        info.data.offset = htole64(IMAGE_HEADER_SPACE);
        info.data.size = htole64(data_size);
        info.hash.offset = htole64(IMAGE_HEADER_SPACE + data_size);

        if (getrandom(info.hash.salt, info.hash.salt_size / 8, GRND_NONBLOCK) < 0)
                return -errno;

        r  = hash_tree_write(filename_image,
                             info.hash.algorithm,
                             digest_size / 8,
                             data_block_size,
                             IMAGE_HEADER_SPACE / data_block_size,
                             data_size / data_block_size,
                             hash_block_size,
                             (IMAGE_HEADER_SPACE + data_size) / hash_block_size,
                             info.hash.salt,
                             salt_size / 8,
                             info.hash.root_hash,
                             &hash_size);
        if (r < 0)
                return r;

        info.hash.size = htole64(hash_size);

        if (fseeko(f_image, 0, SEEK_SET) < 0)
                return -errno;

        if (fwrite(&info, sizeof(info), 1, f_image) != 1)
                return -EIO;

        if (fflush(f_image) < 0)
                return -errno;

        return 0;
}

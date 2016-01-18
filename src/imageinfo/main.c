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
#include <bus1/b1-image-info.h>
#include <linux/random.h>
#include <sys/stat.h>

#include "util.h"
#include "hash-tree.h"

static int image_info_print(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        struct stat sb;
        Bus1ImageInfo info;
        static const char super_uuid[] = BUS1_SUPER_HEADER_UUID;
        static const char info_uuid[] = BUS1_IMAGE_INFO_UUID;
        _c_cleanup_(c_freep) char *salt = NULL;
        _c_cleanup_(c_freep) char *root_hash = NULL;
        int r;

        f = fopen(data, "re");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        if (fstat(fileno(f), &sb) < 0)
                return -errno;

        if (sb.st_size <= (off_t)sizeof(Bus1ImageInfo))
                return -EINVAL;

        if (fseeko(f, sb.st_size - sizeof(Bus1ImageInfo), SEEK_SET) < 0)
                return -errno;

        if (fread(&info, sizeof(info), 1, f) != 1)
                return -EIO;

        if (memcmp(info.super.super_uuid, super_uuid, sizeof(super_uuid)) != 0)
                return -EINVAL;

        if (memcmp(info.super.type_uuid, info_uuid, sizeof(info_uuid)) != 0)
                return -EINVAL;

        printf("\n");
        printf("=================================================================================\n");
        printf("Info for:         %s \n", data);
        printf("Image Name:       %s\n", info.super.object_label);
        printf("Data offset:      %" PRIu64 " bytes\n", le64toh(info.data.offset));
        printf("Data size:        %" PRIu64 " bytes\n", le64toh(info.data.size));
        printf("Hash tree offset: %" PRIu64 " bytes\n", le64toh(info.hash.offset));
        printf("Hash tree size:   %" PRIu64 " bytes\n", le64toh(info.hash.size));
        printf("Hash algorithm:   %s\n", info.hash.algorithm);
        printf("Hash digest size: %" PRIu64 " bits\n", le64toh(info.hash.digest_size));
        printf("Hash Block size:  %" PRIu64 " bits\n", le64toh(info.hash.hash_block_size));
        printf("Data block size:  %" PRIu64 " bits\n", le64toh(info.hash.data_block_size));

        r = bytes_to_hexstr(info.hash.salt, info.hash.salt_size / 8, &salt);
        if (r < 0)
                return r;

        printf("Salt:             %s\n", salt);
        printf("Salt size:        %" PRIu64 " bits\n", le64toh(info.hash.salt_size));

        r = bytes_to_hexstr(info.hash.root_hash, info.hash.digest_size / 8, &root_hash);
        if (r < 0)
                return r;

        printf("Root hash:        %s\n", root_hash);
        printf("=================================================================================\n");

        return 0;
}

static int image_append_hash(const char *data_file, const char *name) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        struct stat sb;
        uint64_t digest_size = 256;
        uint64_t data_size;
        uint64_t hash_size;
        uint64_t hash_block_size = 4096;
        uint64_t data_block_size = 4096;
        uint64_t salt_size = 256;
        Bus1ImageInfo info = {
                .super.super_uuid = BUS1_SUPER_HEADER_UUID,
                .super.type_uuid = BUS1_IMAGE_INFO_UUID,
                .super.type_tag = "org.bus1.image",
                .hash.algorithm = "sha256",
                .hash.digest_size = htole64(digest_size),
                .hash.hash_block_size = htole64(hash_block_size),
                .hash.data_block_size = htole64(data_block_size),
                .hash.salt_size = htole64(salt_size),
        };
        int r;

        if (name)
                strncpy(info.super.object_label, name, sizeof(info.super.object_label) - 1);

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

int main(int argc, char **argv) {
        const char *data_file = argv[1];
        int r;

        if (argc < 2 || argc > 3) {
                fprintf(stderr, "Usage: %s <image> <name>\n", program_invocation_short_name);
                return EXIT_FAILURE;
        }

        if (image_info_print(data_file) >= 0)
                return EXIT_SUCCESS;

        r = image_append_hash(data_file, argv[2]);
        if (r < 0) {
                fprintf(stderr, "Error writing %s: %s\n", data_file, strerror(-r));
                return EXIT_FAILURE;
        }

        if (image_info_print(data_file) < 0)
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}

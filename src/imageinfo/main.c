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
#include <sys/stat.h>

#include "util.h"

static int image_info_print(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        struct stat sb;
        Bus1ImageInfo info;
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

        if (memcmp(info.uuid, info_uuid, sizeof(info_uuid)) != 0)
                return -EINVAL;

        printf("\n");
        printf("=================================================================================\n");
        printf("Image Info for:  %s \n", data);
        printf("data size:       %" PRIu64 "\n", info.data.size);
        printf("hash offset:     %" PRIu64 "\n", info.hash.offset);
        printf("data block size: %" PRIu64 "\n", info.hash.data_block_size);
        printf("block size       %" PRIu64 "\n", info.hash.block_size);
        printf("hash algorithm:  %s\n", info.hash.algorithm);

        r = bytes_to_hexstr(info.hash.salt, info.hash.salt_size, &salt);
        if (r < 0)
                return r;

        printf("salt:            %s\n", salt);
        printf("salt size:       %" PRIu64 "\n", info.hash.salt_size);

        r = bytes_to_hexstr(info.hash.root_hash, info.hash.root_hash_size, &root_hash);
        if (r < 0)
                return r;

        printf("root hash        %s\n", root_hash);
        printf("root hash size:  %" PRIu64 "\n", info.hash.root_hash_size);
        printf("=================================================================================\n");

        return 0;
}

static int image_info_write(const char *data_file,
                            const char *hash_file,
                            const char *salt,
                            const char *root_hash) {
        Bus1ImageInfo info = {
                .uuid = BUS1_IMAGE_INFO_UUID,
                .name = "org.bus.base",

                .hash.algorithm = "sha256",
                .hash.data_block_size = 4096,
                .hash.block_size = 4096,
                .hash.salt_size = 32,
                .hash.root_hash_size = 32,
        };
        struct stat sb;

        if (stat(data_file, &sb) < 0)
                return -errno;

        info.data.size = sb.st_size;
        info.hash.offset = sb.st_size;

        if (stat(hash_file, &sb) < 0)
                return -errno;

        info.hash.size = sb.st_size;

        hexstr_to_bytes(salt, info.hash.salt);
        hexstr_to_bytes(root_hash, info.hash.root_hash);

        if (write(STDOUT_FILENO, &info, sizeof(info)) != sizeof(info))
                return -EIO;

        return 0;
}

int main(int argc, char **argv) {
        const char *data_file = argv[1];
        const char *hash_file = argv[2];
        const char *salt = argv[3];
        const char *root_hash = argv[4];

        if (argc == 2) {
                if (image_info_print(data_file) >= 0)
                        return EXIT_SUCCESS;

                return EXIT_FAILURE;
        }

        if (argc != 5) {
                fprintf(stderr, "Expecting %s <data> <hash> <salt> <root-hash>\n", program_invocation_short_name);
                return EXIT_FAILURE;
        }

        if (image_info_print(data_file) >= 0)
                return EXIT_SUCCESS;

        if (image_info_write(data_file, hash_file, salt, root_hash) < 0)
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}

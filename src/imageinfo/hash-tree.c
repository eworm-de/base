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
#include <gcrypt.h>

#include "hash-tree.h"
#include "util.h"

static int hash_write(FILE *f_in, FILE *f_out,
                      off_t data_block, size_t data_block_size,
                      off_t hash_block, size_t hash_block_size,
                      off_t n_data_blocks,
                      gcry_md_hd_t crypt_ctx, int crypt_hash_type,
                      const uint8_t *salt, size_t salt_size,
                      uint8_t *digest, size_t digest_size) {
        _c_cleanup_(c_freep) uint8_t *null_block = NULL;
        _c_cleanup_(c_freep) uint8_t *data_buffer = NULL;
        size_t hashes_per_block;
        off_t n_blocks;
        size_t n_bytes;
        unsigned i;

        hashes_per_block = hash_block_size / digest_size;
        n_blocks = (n_data_blocks + hashes_per_block - 1) / hashes_per_block;

        null_block = calloc(1, hash_block_size);
        data_buffer = calloc(1, data_block_size);
        if (!null_block || !data_buffer)
                return -ENOMEM;

        if (fseeko(f_in, data_block * data_block_size, SEEK_SET))
                return -errno;

        if (f_out && fseeko(f_out, hash_block * hash_block_size, SEEK_SET))
                return -errno;

        while (n_blocks--) {
                n_bytes = hash_block_size;

                for (i = 0; i < hashes_per_block; i++) {
                        if (n_data_blocks-- == 0)
                                break;

                        if (fread(data_buffer, data_block_size, 1, f_in) != 1)
                                return -EIO;

                        gcry_md_reset(crypt_ctx);
                        gcry_md_write(crypt_ctx, salt, salt_size);
                        gcry_md_write(crypt_ctx, data_buffer, data_block_size);
                        memcpy(digest, gcry_md_read(crypt_ctx, crypt_hash_type), digest_size);

                        if (!f_out)
                                break;

                        if (fwrite(digest, digest_size, 1, f_out) != 1)
                                return -EIO;

                        n_bytes -= digest_size;
                }

                if (f_out && n_bytes)
                        if (fwrite(null_block, n_bytes, 1, f_out) != 1)
                                return -EIO;
        }

        return 0;
}

C_DEFINE_CLEANUP(gcry_md_hd_t, gcry_md_close);

int hash_tree_create(const char *hash_name,
                     const char *data_device,
                     size_t data_block_size,
                     off_t n_data_blocks,
                     const char *hash_device,
                     size_t hash_block_size,
                     off_t hash_offset,
                     const uint8_t *salt,
                     size_t salt_size,
                     uint8_t *root_hash) {
        _c_cleanup_(gcry_md_closep) gcry_md_hd_t crypt_ctx = NULL;
        int crypt_hash_type;
        size_t digest_size;
        int n_level;
        struct hash_level {
                off_t offset;
                off_t size;
        };
        size_t hash_per_block_bits;
        _c_cleanup_(c_freep) struct hash_level *levels = NULL;
        _c_cleanup_(c_freep) uint8_t *digest = NULL;
        _c_cleanup_(c_fclosep) FILE *f_data = NULL;
        _c_cleanup_(c_fclosep) FILE *f_hash = NULL;
        off_t s;
        int i;
        int r;

        assert(hash_name);
        assert(hash_device);
        assert(data_device);
        assert(hash_block_size > 0);
        assert(data_block_size > 0);
        assert(n_data_blocks > 2);
        assert(root_hash);
        assert(salt);
        assert(salt_size);

        if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P) == 0) {
                gcry_control(GCRYCTL_DISABLE_SECMEM);
                gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
        }

        crypt_hash_type = gcry_md_map_name(hash_name);
        if (crypt_hash_type <= 0)
                return -EINVAL;

        digest_size = gcry_md_get_algo_dlen(crypt_hash_type);

        if (gcry_md_open(&crypt_ctx, crypt_hash_type, 0) != 0)
                return -EINVAL;

        hash_per_block_bits = log2u(hash_block_size / digest_size);
        for (n_level = 0; hash_per_block_bits * n_level < 64 && (n_data_blocks - 1) >> (hash_per_block_bits * n_level);)
                n_level++;

        levels = calloc(n_level, sizeof(struct hash_level));
        for (i = n_level - 1; i >= 0; i--) {
                levels[i].offset = hash_offset;
                s = (n_data_blocks + ((off_t)1 << ((i + 1) * hash_per_block_bits)) - 1) >> ((i + 1) * hash_per_block_bits);
                levels[i].size = s;

                if ((hash_offset + s) < hash_offset || (hash_offset + s) < 0)
                        return -EINVAL;

                hash_offset += s;
        }

        f_data = fopen(data_device, "re");
        if (!f_data)
                return -errno;

        f_hash = fopen(hash_device, "r+e");
        if (!f_hash)
                return -errno;

        digest = calloc(1, digest_size);
        if (!digest)
                return -ENOMEM;

        r = hash_write(f_data, f_hash,
                       0, data_block_size,
                       levels[0].offset, hash_block_size,
                       n_data_blocks,
                       crypt_ctx, crypt_hash_type,
                       salt, salt_size,
                       digest, digest_size);
        if (r < 0)
                return r;

        for (i = 1; i < n_level; i++) {
                _c_cleanup_(c_fclosep) FILE *f_hash2 = NULL;

                f_hash2 = fopen(hash_device, "re");
                if (!f_hash2)
                        return -errno;

                r = hash_write(f_hash2, f_hash,
                               levels[i - 1].offset, hash_block_size,
                               levels[i].offset, hash_block_size,
                               levels[i - 1].size,
                               crypt_ctx, crypt_hash_type,
                               salt, salt_size,
                               digest, digest_size);
                if (r < 0)
                        return r;
        }

        r = hash_write(f_hash, NULL,
                       levels[n_level - 1].offset, hash_block_size,
                       0, hash_block_size,
                       1,
                       crypt_ctx, crypt_hash_type,
                       salt, salt_size,
                       digest, digest_size);

        memcpy(root_hash, digest, digest_size);

        return r;
}

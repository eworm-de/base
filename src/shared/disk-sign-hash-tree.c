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
#include <openssl/evp.h>
#include <string.h>
#include "disk-sign-hash-tree.h"
#include "file.h"

/* Calculate and store hash blocks from n data blocks. */
static int hash_write(FILE *f_data,
                      FILE *f_hash,
                      uint64_t data_block,
                      uint64_t data_block_size,
                      uint64_t hash_block,
                      uint64_t hash_block_size,
                      uint64_t n_data_blocks,
                      const EVP_MD *md,
                      unsigned int crypt_hash_size,
                      const uint8_t *salt,
                      size_t salt_size,
                      uint8_t *result) {
        _c_cleanup_(c_freep) uint8_t *null_block = NULL;
        _c_cleanup_(c_freep) uint8_t *data_buffer = NULL;
        size_t n_hashes_per_block;
        uint64_t n_blocks;
        size_t n_bytes;

        n_hashes_per_block = hash_block_size / crypt_hash_size;
        n_blocks = (n_data_blocks + n_hashes_per_block - 1) / n_hashes_per_block;

        null_block = calloc(1, hash_block_size);
        data_buffer = calloc(1, data_block_size);
        if (!null_block || !data_buffer)
                return -ENOMEM;

        if (fflush(f_data) < 0)
                return -errno;

        if (fflush(f_hash) < 0)
                return -errno;

        if (fseeko(f_data, data_block * data_block_size, SEEK_SET))
                return -errno;

        if (f_hash && fseeko(f_hash, hash_block * hash_block_size, SEEK_SET))
                return -errno;

        while (n_blocks--) {
                n_bytes = hash_block_size;

                for (size_t i = 0; i < n_hashes_per_block; i++) {
                        EVP_MD_CTX mdctx;
                        uint8_t md_value[EVP_MAX_MD_SIZE];
                        unsigned int md_len;

                        if (n_data_blocks-- == 0)
                                break;

                        if (fread(data_buffer, data_block_size, 1, f_data) != 1)
                                return -EIO;

                        if (!EVP_DigestInit(&mdctx, md) ||
                            !EVP_DigestUpdate(&mdctx, salt, salt_size) ||
                            !EVP_DigestUpdate(&mdctx, data_buffer, data_block_size) ||
                            !EVP_DigestFinal_ex(&mdctx, md_value, &md_len) ||
                            !EVP_MD_CTX_cleanup(&mdctx))
                                return -EINVAL;

                        if (md_len != salt_size)
                                return -EINVAL;

                        if (result)
                                memcpy(result, md_value, crypt_hash_size);

                        if (!f_hash)
                                break;

                        if (fwrite(md_value, crypt_hash_size, 1, f_hash) != 1)
                                return -EIO;

                        n_bytes -= crypt_hash_size;
                }

                if (f_hash && n_bytes)
                        if (fwrite(null_block, n_bytes, 1, f_hash) != 1)
                                return -EIO;
        }

        return 0;
}

int disk_sign_hash_tree_write(const char *filename,
                              const char *hash_name,
                              uint64_t digest_size,
                              uint64_t data_block_size,
                              uint64_t data_block_nr,
                              uint64_t n_data_blocks,
                              uint64_t hash_block_size,
                              uint64_t hash_block_nr,
                              const uint8_t *salt,
                              uint64_t salt_size,
                              uint8_t *root_hash,
                              uint64_t *hash_tree_sizep) {
        _c_cleanup_(c_fclosep) FILE *f_data = NULL;
        _c_cleanup_(c_fclosep) FILE *f_hash = NULL;
        uint64_t data_size;
        const EVP_MD *md;
        int n_level;
        uint64_t hash_size = 0;
        struct hash_level {
                uint64_t block;
                uint64_t n_blocks;
        };
        size_t hash_per_block_bits;
        _c_cleanup_(c_freep) struct hash_level *levels = NULL;
        int r;

        assert(filename);
        assert(hash_name);
        assert(digest_size);
        assert(data_block_size > 0);
        assert(n_data_blocks > 2);
        assert(hash_block_size > 0);
        assert(salt);
        assert(salt_size);
        assert(root_hash);

        OpenSSL_add_all_digests();

        f_data = fopen(filename, "re");
        if (!f_data)
                return -errno;

        r = file_get_size(f_data, &data_size);
        if (r < 0)
                return r;

        if (data_size % 4096)
                return -EINVAL;

        f_hash = fopen(filename, "r+e");
        if (!f_hash)
                return -errno;

        md = EVP_get_digestbyname(hash_name);
        if(!md)
                return -EINVAL;

        if (digest_size != (uint64_t)EVP_MD_size(md))
                return -EINVAL;

        /* Calculate the number of levels. */
        hash_per_block_bits = c_log2(hash_block_size / digest_size);
        for (n_level = 0; hash_per_block_bits * n_level < 64 && (n_data_blocks - 1) >> (hash_per_block_bits * n_level);)
                n_level++;

        /* Calculate sizes and offsets for the hash block layer. */
        levels = calloc(n_level, sizeof(struct hash_level));
        for (int i = n_level - 1; i >= 0; i--) {
                uint64_t n;

                levels[i].block = hash_block_nr;
                n = (n_data_blocks + (1ULL << ((i + 1) * hash_per_block_bits)) - 1) >> ((i + 1) * hash_per_block_bits);
                levels[i].n_blocks = n;

                if ((hash_block_nr + n) < hash_block_nr)
                        return -EINVAL;

                hash_block_nr += n;
                hash_size += n * hash_block_size;
        }

        /* Calculate and store hashes for the data blocks. */
        r = hash_write(f_data, f_hash,
                       data_block_nr,
                       data_block_size,
                       levels[0].block,
                       hash_block_size,
                       n_data_blocks,
                       md,
                       digest_size,
                       salt,
                       salt_size,
                       NULL);
        if (r < 0)
                return r;


        /* Calculate and store hashes for the hash blocks layers. */
        for (int i = 1; i < n_level; i++) {
                r = hash_write(f_data,
                               f_hash,
                               levels[i - 1].block,
                               hash_block_size,
                               levels[i].block,
                               hash_block_size,
                               levels[i - 1].n_blocks,
                               md,
                               digest_size,
                               salt, salt_size,
                               NULL);
                if (r < 0)
                        return r;
        }

        /* Calculate the root hash from the single root hash block.  */
        r = hash_write(f_hash,
                       NULL,
                       levels[n_level - 1].block,
                       hash_block_size,
                       0,
                       hash_block_size,
                       1,
                       md,
                       digest_size,
                       salt,
                       salt_size,
                       root_hash);

        if (hash_tree_sizep)
                *hash_tree_sizep = hash_size;

        EVP_cleanup();

        return r;
}

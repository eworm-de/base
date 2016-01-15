#pragma once
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

/*

  ------------------------------------------------------
  | Data                                               |
  |                                                    |
  |                                                    |
  |                                                    |
  ------------------------------------------------------
  | Hash Tree (Integrity of Data)                      |
  |                                                    |
  ------------------------------------------------------
  | Signature (Authenticity of ImageInfo)              |
  ------------------------------------------------------
  | ImageInfo                                          |
  ------------------------------------------------------

 */

#include <bus1/b1-super-header.h>

#define BUS1_IMAGE_INFO_UUID { 0xb7, 0x46, 0xc4, 0xf5, 0xc3, 0xc4, 0x47, 0x37, 0x8a, 0x4c, 0x54, 0xbe, 0xe4, 0x75, 0x69, 0x2a }

struct Bus1ImageInfo {
        Bus1SuperHeader super;

        struct {
                uint64_t offset;                /* Absolute offset of filesystem image, usually 0 */
                uint64_t size;                  /* Size of filesystem image */
        } data;

        struct {
                uint64_t offset;                /* Absolute offset of hash tree blocks */
                uint64_t size;                  /* Size of hsh tree blocks */
                char algorithm[32];             /* Hash algorithm used to create hash blocks */

                uint64_t data_block_size;       /* Input chunk size */
                uint64_t block_size;            /* Hash block size */

                uint8_t salt[256];              /* Salt used while hashing input data */
                uint64_t salt_size;             /* Size of salt in bytes */

                uint8_t root_hash[256];         /* Root hash value to validate against */
                uint64_t root_hash_size;        /* Size of root hash in bytes */
        } hash;

        struct {
                uint64_t offset;                /* Absolute offset of signature */
                uint64_t size;                  /* Size of signature */
                char signature_type[32];        /* Type of signature */
        } signature;
};

typedef struct Bus1ImageInfo Bus1ImageInfo;

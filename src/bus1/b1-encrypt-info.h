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

#include <bus1/b1-super-info.h>

#define BUS1_ENCRYPT_INFO_UUID { 0x59, 0x33, 0x18, 0x42, 0x97, 0x21, 0x42, 0x37, 0xa7, 0xca, 0xff, 0x9b, 0x94, 0x5b, 0x92, 0x8b }

struct Bus1EncryptInfo {
        Bus1SuperHeader super;

        struct {
                uint64_t offset;
                uint64_t size;
        } data;

        struct {
                char cypher[32];
                char chain_mode[32];
                char iv_mode[32];
        } encrypt;
};

typedef struct Bus1EncryptInfo Bus1EncryptInfo;

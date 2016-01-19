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

int image_get_info(FILE *f,
                   char **image_name,
                   uint8_t *image_uuid,
                   uint64_t *data_offset,
                   uint64_t *data_size,
                   uint64_t *hash_offset,
                   uint64_t *hash_size,
                   char **hash_algorithm,
                   uint64_t *hash_digest_size,
                   uint64_t *hash_block_size,
                   uint64_t *data_block_size,
                   char **salt,
                   char **root_hash);

int image_setup(const char *image, const char *name, char **device);

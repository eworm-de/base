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

int disk_sign_get_info(FILE *f,
                       char **image_typep,
                       char **image_namep,
                       uint8_t *image_uuidp,
                       char **data_typep,
                       uint64_t *data_offsetp,
                       uint64_t *data_sizep,
                       uint64_t *hash_offsetp,
                       uint64_t *hash_sizep,
                       char **hash_algorithmp,
                       uint64_t *hash_digest_sizep,
                       uint64_t *hash_block_sizep,
                       uint64_t *data_block_sizep,
                       char **saltp,
                       char **root_hashp);

int disk_sign_setup_device(const char *image,
                           char **devicep,
                           char **data_typep);

int disk_sign_format_volume(const char *filename_data,
                            const char *filename_image,
                            const char *image_name,
                            const char *data_type);

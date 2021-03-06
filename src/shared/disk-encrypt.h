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

#include <org.bus1/b1-disk-encrypt-header.h>

int disk_encrypt_get_info(FILE *f,
                          char **image_typep,
                          char **image_namep,
                          uint8_t *image_uuidp,
                          char **data_typep,
                          uint64_t *data_offsetp,
                          uint64_t *data_sizep,
                          char **encryptionp,
                          char **master_key_encryptionp,
                          uint8_t *master_keyp,
                          uint64_t *master_key_sizep,
                          uint64_t *n_keysp,
                          Bus1DiskEncryptKeySlot **keysp);

int disk_encrypt_setup_device(const char *device,
                              char **devicep,
                              char **image_namep,
                              char **data_typep);

int disk_encrypt_format_volume(const char *data_file,
                               const char *image_name,
                               const char *data_type,
                               uint8_t *recovery_keyp,
                               uint64_t *recovery_key_sizep);

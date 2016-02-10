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

#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include <org.bus1/b1-disk-encrypt-header.h>
#include "shared/disk-encrypt.h"
#include "shared/string.h"
#include "shared/uuid.h"

#include "encrypt.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

int disk_encrypt_print_info(const char *data) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        _c_cleanup_(c_freep) char *image_type = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        uint8_t image_uuid[16];
        _c_cleanup_(c_freep) char *image_uuid_str = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        uint64_t data_offset;
        uint64_t data_size;
        _c_cleanup_(c_freep) char *encryption = NULL;
        _c_cleanup_(c_freep) char *master_key_encryption = NULL;
        uint8_t master_key_encrypted[256];
        _c_cleanup_(c_freep) char *master_key_str = NULL;
        uint64_t master_key_encrypted_size;
        uint64_t n_key_slots;
        uint8_t key0_type_uuid[16];
        _c_cleanup_(c_freep) char *key0_type_uuid_str = NULL;
        _c_cleanup_(c_freep) char *key0_encryption = NULL;
        uint8_t key0[256];
        size_t key0_size;
        _c_cleanup_(c_freep) char *key0_str = NULL;
        int r;

        f = fopen(data, "re");
        if (!f)
                return -errno;

        r = disk_encrypt_get_info(f,
                                  &image_type,
                                  &image_name,
                                  image_uuid,
                                  &data_type,
                                  &data_offset,
                                  &data_size,
                                  &encryption,
                                  &master_key_encryption,
                                  master_key_encrypted,
                                  &master_key_encrypted_size,
                                  &n_key_slots,
                                  key0_type_uuid,
                                  &key0_encryption,
                                  key0,
                                  &key0_size);
        if (r < 0)
                return r;

        r = uuid_to_string(image_uuid, &image_uuid_str);
        if (r < 0)
                return r;

        r = hexstr_from_bytes(master_key_encrypted, master_key_encrypted_size / 8, &master_key_str);
        if (r < 0)
                return r;

        r = uuid_to_string(key0_type_uuid, &key0_type_uuid_str);
        if (r < 0)
                return r;

        r = hexstr_from_bytes(key0, master_key_encrypted_size / 8, &key0_str);
        if (r < 0)
                return r;

        printf("========================================================================================================\n");
        printf("Info for:               %s\n", data);
        printf("Image type:             %s\n", image_type);
        printf("Image name:             %s\n", image_name);
        printf("Image UUID:             %s\n", image_uuid_str);
        printf("Data type:              %s\n", data_type);
        printf("Data offset:            %" PRIu64 " bytes\n", data_offset);
        printf("Data size:              %" PRIu64 " bytes\n", data_size);
        printf("Data encryption:        %s\n", encryption);
        printf("Master key encryption:  %s\n", master_key_encryption);
        printf("Master key (encrypted): %s\n", master_key_str);
        printf("Master key size:        %" PRIu64 " bits\n", master_key_encrypted_size);
        printf("Key slots:              %" PRIu64 "\n", n_key_slots);
        printf("Key[0] type UUID:       %s\n", key0_type_uuid_str);
        printf("Key[0] encryption:      %s\n", key0_encryption);
        printf("Key[0] key (encrypted): %s\n", key0_str);
        printf("Key[0] key size:        %" PRIu64 " bits\n", key0_size);
        printf("========================================================================================================\n");

        return 0;
}

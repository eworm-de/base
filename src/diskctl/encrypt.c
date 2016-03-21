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
        uint64_t n_keys;
        _c_cleanup_(c_freep) Bus1DiskEncryptKeySlot *keys = NULL;
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
                                  &n_keys,
                                  &keys);
        if (r < 0)
                return r;

        r = uuid_to_string(image_uuid, &image_uuid_str);
        if (r < 0)
                return r;

        r = hexstr_from_bytes(master_key_encrypted, master_key_encrypted_size, &master_key_str);
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
        printf("Master key size:        %" PRIu64 " bits\n", master_key_encrypted_size * 8);
        printf("Number of key slots:    %" PRIu64 "\n", n_keys);

        for (size_t i = 0; i < n_keys; i++) {
                static const char key_clear_uuid[] = BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID;
                static const char key_recovery_uuid[] = BUS1_DISK_ENCRYPT_KEY_RECOVERY_UUID;
                _c_cleanup_(c_freep) char *uuid_str = NULL;

                r = uuid_to_string(keys[i].type_uuid, &uuid_str);
                if (r < 0)
                        return r;

                if (memcmp(keys[i].type_uuid, key_clear_uuid, 16) == 0) {
                        _c_cleanup_(c_freep) char *key_str = NULL;

                        r = hexstr_from_bytes(keys[i].clear.key, keys[i].clear.key_size, &key_str);
                        if (r < 0)
                                return r;

                        printf("[%02zd] type:              clear\n", i);
                        printf("     type UUID:         %s\n", uuid_str);
                        printf("     encryption:        %s\n", keys[i].clear.encryption);
                        printf("     key (encrypted):   %s\n", key_str);
                        printf("     key size:          %" PRIu64 " bits\n", keys[i].clear.key_size * 8);

                        continue;
                }

                if (memcmp(keys[i].type_uuid, key_recovery_uuid, 16) == 0) {
                        _c_cleanup_(c_freep) char *key_str = NULL;

                        r = hexstr_from_bytes(keys[i].recovery.key, keys[i].recovery.key_size, &key_str);
                        if (r < 0)
                                return r;

                        printf("[%02zd] type:              recovery\n", i);
                        printf("     type UUID:         %s\n", uuid_str);
                        printf("     encryption:        %s\n", keys[i].recovery.encryption);
                        printf("     key (encrypted):   %s\n", key_str);
                        printf("     key size:          %" PRIu64 " bits\n", keys[i].recovery.key_size * 8);

                        continue;
                }

                printf("[%02zd] type:              unknown\n", i);
                printf("     type UUID:         %s\n", uuid_str);
        }

        printf("========================================================================================================\n");

        return 0;
}

/* Every 6-digit number represents 16-bit key material. This value is multiplied
   by 11, to act like a checksum which allows the validation of the typed input. */
static char *bytes_print(const uint8_t b[8], char s[28]) {
        sprintf(s, "%06d-%06d-%06d-%06d",
                (b[0] << 8 | b[1]) * 11,
                (b[2] << 8 | b[3]) * 11,
                (b[4] << 8 | b[5]) * 11,
                (b[6] << 8 | b[7]) * 11);

        return s;
}

void disk_encrypt_print_recovery(uint8_t *recovery_key, uint64_t recovery_key_size) {
        char s[28];

        printf("Recovery key:           %s\n", bytes_print(recovery_key, s));
        for (size_t i = 8; i < recovery_key_size; i += 8)
                printf("                        %s\n", bytes_print(recovery_key + i, s));
}

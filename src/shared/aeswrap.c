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

#include <openssl/crypto.h>
#include <openssl/aes.h>

#include <org.bus1/c-macro.h>
#include <shared/aeswrap.h>
#include <shared/string.h>

int aeswrap_encrypt_data(const uint8_t *key,
                         uint64_t key_size,
                         const uint8_t *data,
                         uint8_t *data_encrypted,
                         uint64_t *data_encrypted_sizep) {
        AES_KEY actx;
        int r;

        if (AES_set_encrypt_key(key, key_size, &actx))
                return -EINVAL;

        r = AES_wrap_key(&actx, NULL, data_encrypted, data, key_size / 8);
        memwipe(&actx, sizeof(actx));
        if (r < 0)
                return -EINVAL;

        if (data_encrypted_sizep)
                *data_encrypted_sizep = r * 8;

        return 0;
}

int aeswrap_decrypt_data(const uint8_t *key,
                         uint64_t key_size,
                         const uint8_t *data,
                         uint64_t data_size,
                         uint8_t *data_decrypted) {
        AES_KEY actx;
        int r;

        if (AES_set_decrypt_key(key, key_size, &actx))
                return -EINVAL;

        r = AES_unwrap_key(&actx, NULL, data_decrypted, data, data_size / 8);
        memwipe(&actx, sizeof(actx));
        if (r < 0)
                return -EKEYREJECTED;

        return r;
}

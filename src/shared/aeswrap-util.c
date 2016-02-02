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
#include <gcrypt.h>

#include <aeswrap-util.h>

C_DEFINE_CLEANUP(gcry_cipher_hd_t, gcry_cipher_close);

int aeswrap_encrypt_data(const uint8_t *key,
                         uint64_t key_size,
                         const uint8_t *data,
                         uint8_t *data_encrypted,
                         uint64_t *data_encrypted_sizep) {
        _c_cleanup_(gcry_cipher_closep) gcry_cipher_hd_t crypt_hd = NULL;
        uint64_t data_encrypted_size;
        uint8_t *buffer;
        gcry_error_t r;

        if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P) == 0) {
                gcry_control(GCRYCTL_DISABLE_SECMEM);
                gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
        }

        r = gcry_cipher_open(&crypt_hd, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_AESWRAP, 0);
        if (r != 0)
                return -EINVAL;

        r = gcry_cipher_setkey(crypt_hd, key, key_size / 8);
        if (r != 0)
                return -EINVAL;

        data_encrypted_size = (key_size / 8) + 8;
        buffer = alloca(data_encrypted_size);
        r = gcry_cipher_encrypt(crypt_hd, buffer, data_encrypted_size, data, key_size / 8);
        if (r != 0)
                return -EINVAL;

        memcpy(data_encrypted, buffer, data_encrypted_size);
        if (data_encrypted_sizep)
                *data_encrypted_sizep = data_encrypted_size * 8;

        return 0;
}

int aeswrap_decrypt_data(const uint8_t *key,
                         uint64_t key_size,
                         const uint8_t *data,
                         uint64_t data_size,
                         uint8_t *data_decrypted) {
        _c_cleanup_(gcry_cipher_closep) gcry_cipher_hd_t crypt_hd = NULL;
        uint8_t *buffer;
        gcry_error_t r;

        if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P) == 0) {
                gcry_control(GCRYCTL_DISABLE_SECMEM);
                gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
        }

        r = gcry_cipher_open(&crypt_hd, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_AESWRAP, 0);
        if (r != 0)
                return -EINVAL;

        r = gcry_cipher_setkey(crypt_hd, key, key_size / 8);
        if (r != 0)
                return -EINVAL;

        buffer = alloca(key_size / 8);
        r = gcry_cipher_decrypt(crypt_hd, buffer, key_size / 8, data, data_size / 8);
        if (r != 0)
                return -EINVAL;

        memcpy(data_decrypted, buffer, key_size / 8);

        return 0;
}

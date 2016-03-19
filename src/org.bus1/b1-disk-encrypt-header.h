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

  ┌────────────────┐
  │ Image header   │
  ├────────────────┤
  │ Key slot array │
  ├────────────────┤
  │ Encrypted data │
  │                │
  │                │
  └────────────────┘

  The image header is 4096 bytes in size.

  The key slot array follows the header. One key slot is 4096 bytes
  in size.

  The byte-order is little-endian.

  Example:
    Data encryption:       aes-xts-plain64
    Master key length:     256 bit
    Master key encryption: aes-wrap (320 bit output stored on disk)
    Slot key lenght:       256 bit
    Slot key encryption:   aes-wrap (320 bit output stored on disk)
    Secret key length:     256 bit

                       ┌────────────────────────┐
                       │ Disk                   │
                       ├────────────────────────┤
      Secret key ────► │ Slot#1 key (encrypted) │ ──► Master key encryption key ─┐
                       ├────────────────────────┤                                │
      Recovery key ──► │ Slot#2 key (encrypted) │ ──► Master key encryption key ─┤
                       ├────────────────────────┤                                │
      Password ──────► │ Slot#3 key (encrypted) │ ──► Master key encryption key ─┤
                       ├────────────────────────┤                                │
  ┌── Master key ◄──── │ Master key (encrypted) │ ◄──────────────────────────────┘
  │                    ├────────────────────────┤
  └──────────────────► │ Data (encrypted)       │ ──► Data
                       │                        │
                       │                        │
                       └────────────────────────┘

 */

#include <org.bus1/b1-meta-header.h>

#define BUS1_DISK_ENCRYPT_HEADER_UUID           { 0x59, 0x33, 0x18, 0x42, 0x97, 0x21, 0x42, 0x37, 0xa7, 0xca, 0xff, 0x9b, 0x94, 0x5b, 0x92, 0x8b }

#define BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID        { 0x54, 0xe1, 0x8c, 0x3c, 0x3f, 0x65, 0x49, 0x09, 0xa5, 0x36, 0xbf, 0x7d, 0x08, 0x0f, 0xf2, 0x82 }
#define BUS1_DISK_ENCRYPT_KEY_RECOVERY_UUID     { 0x30, 0xa4, 0x93, 0x86, 0x8b, 0xbc, 0x4e, 0x97, 0x84, 0x12, 0x03, 0xf5, 0x0a, 0xd1, 0x06, 0x21 }
#define BUS1_DISK_ENCRYPT_KEY_SMARTCARD_UUID    { 0xe2, 0x6a, 0xa0, 0x44, 0x7e, 0xac, 0x48, 0x10, 0xa4, 0xfe, 0xb0, 0x7d, 0x0c, 0x11, 0xf5, 0x81 }
#define BUS1_DISK_ENCRYPT_KEY_PASSWORD_UUID     { 0xe5, 0xb7, 0x7c, 0xc4, 0x15, 0x7b, 0x43, 0x29, 0xb3, 0xba, 0x3d, 0x8b, 0x60, 0xd6, 0x93, 0xd5 }

typedef union {
        struct {
                Bus1MetaHeader meta;

                struct {                                /* Encrypted data blocks. */
                        uint64_t offset;                /* Absolute offset of filesystem image in bytes. */
                        uint64_t size;                  /* Size of filesystem image in bytes. */
                        char type[64];                  /* Data type / filesystem format. */
                } _c_packed_ data;

                struct {                                /* Data encryption properties. */
                        char cypher[32];
                        char chain_mode[32];
                        char iv_mode[32];
                } _c_packed_ encrypt;

                struct {                                /* Encrypted master key to decrypt data. */
                        char encryption[32];            /* Encryption type used to encrypt the key. */
                        uint64_t key_size;              /* Key size in bits. */
                        uint8_t key[256];               /* Encrypted key. */
                } _c_packed_ master_key;

                uint64_t n_key_slots;                   /* Number of key encryption key slots following the header. */
        };

        uint8_t bytes[4096];
} _c_packed_ Bus1DiskEncryptHeader;

typedef union {                                         /* Key slot with encrypted master key. */
        struct {
                uint8_t type_uuid[16];                  /* Type of key slot. */

                union {
                        struct {                        /* BUS1_DISK_ENCRYPT_KEY_CLEAR_UUID */
                                char encryption[32];    /* Encryption type used to encrypt the key. */
                                uint64_t key_size;      /* Key size in bits. */
                                uint8_t key[256];       /* Encrypted key. */
                        } _c_packed_ clear;

                        struct {                        /* BUS1_DISK_ENCRYPT_KEY_RECOVERY_UUID */
                                char encryption[32];    /* Encryption type used to encrypt the key. */
                                uint64_t key_size;      /* Key size in bits. */
                                uint8_t key[256];       /* Encrypted key. */
                        } _c_packed_ recovery;

                        struct {                        /* BUS1_DISK_ENCRYPT_KEY_SMARTCARD_UUID */
                                char encryption[32];    /* Encryption type used to encrypt the key. */
                                char id_hash[32];       /* Hash type to generate the ID/fingerprint. */
                                uint64_t id_size;       /* ID size in bits.*/
                                uint8_t id[256];        /* Public key ID/fingerprint*/
                                uint64_t key_size;      /* Key size in bits. */
                                uint8_t key[1024];      /* Encrypted key. */
                        } _c_packed_ smartcard;

                        struct {                        /* BUS1_DISK_ENCRYPT_KEY_PASSWORD_UUID */
                                char encryption[32];    /* Encryption type used to encrypt the key. */
                                uint64_t key_size;      /* Key size in bits. */
                                uint8_t key[256];       /* Encrypted key. */
                                char hash[32];          /* Password hashing type. */
                                uint8_t salt[256];      /* Salt bits. */
                                uint64_t salt_size;     /* Salt size in bits. */
                                uint64_t iterations;
                                uint64_t memory;
                                uint64_t parallelism;
                        } _c_packed_ password;
                };
        };

        uint8_t bytes[4096];
} _c_packed_ Bus1DiskEncryptKeySlot;

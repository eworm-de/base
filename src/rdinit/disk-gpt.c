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

#include <c-macro.h>
#include <ctype.h>
#include <byteswap.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <org.bus1/b1-platform.h>
#include <string.h>
#include "shared/uuid.h"
#include "disk-gpt.h"

struct guid {
        uint32_t data1;
        uint16_t data2;
        uint16_t data3;
        uint8_t data4[8];
};

struct gpt_header {
        uint8_t signature[8];
        uint32_t revision;
        uint32_t header_size;
        uint32_t header_crc32;
        uint32_t reserved1;
        uint64_t my_lba;
        uint64_t alternate_lba;
        uint64_t first_usable_lba;
        uint64_t last_usable_lba;
        struct guid disk_guid;
        uint64_t partition_entries_lba;
        uint32_t n_partition_entries;
        uint32_t partition_entry_size;
        uint32_t partition_entry_array_crc32;
} _c_packed_;

struct gpt_entry {
        struct guid partition_type_guid;
        struct guid unique_partition_guid;
        uint64_t lba_start;
        uint64_t lba_end;
        uint64_t attributes;
        uint16_t partition_name[36]; /* UTF-16LE */
} _c_packed_;

static uint8_t *guid_to_uuid(const struct guid *guid, uint8_t *uuid) {
        struct guid *u = (struct guid *)(uuid);

        u->data1 = __bswap_32(guid->data1);
        u->data2 = __bswap_16(guid->data2);
        u->data3 = __bswap_16(guid->data3);
        memcpy(u->data4, guid->data4, sizeof(u->data4));

        return uuid;
}

int disk_gpt_find_partitions(const char *device,
                             const uint8_t *disk_uuid,
                             char **device_bootp,
                             char **device_datap) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        int sector_size;
        struct gpt_header header;
        uint8_t uuid[16];
        uint64_t entries_lba;
        unsigned int n_entries;
        unsigned int entry_size;
        static const uint8_t uuid_data[] = BUS1_GPT_TYPE_DATA_UUID;
        static const uint8_t uuid_boot[] = BUS1_GPT_TYPE_BOOT_UUID;
        _c_cleanup_(c_freep) void *entries = NULL;
        struct gpt_entry *entry;
        unsigned int boot_partno = 0;
        unsigned int data_partno = 0;
        _c_cleanup_(c_freep) char *device_boot = NULL;
        _c_cleanup_(c_freep) char *device_data = NULL;

        f = fopen(device, "re");
        if (!f)
                return -errno;

        setvbuf(f, NULL, _IONBF, 0);

        if (ioctl(fileno(f), BLKSSZGET, &sector_size) < 0)
                sector_size = 512;

        /* Read header. */
        if (fseeko(f, 1 * sector_size, SEEK_SET) < 0)
                return -errno;

        if (fread(&header, sizeof(header), 1, f) != 1)
                return -EIO;

        if (memcmp(header.signature, "EFI PART", sizeof(header.signature)) != 0)
                return -EINVAL;

        if (le32toh(header.header_size) < 92)
                return -EINVAL;

        if (le64toh(header.my_lba) != 1)
                return -EINVAL;

        /* Check if we found the disk we are looking for. */
        if (memcmp(guid_to_uuid(&header.disk_guid, uuid), disk_uuid, sizeof(uuid)) != 0)
                return -EINVAL;

        /* Read partition entries array. */
        entries_lba = le64toh(header.partition_entries_lba);
        if (entries_lba != 2)
                return -EINVAL;

        n_entries = le32toh(header.n_partition_entries);
        if (n_entries > 1024)
                return -EINVAL;

        entry_size = le32toh(header.partition_entry_size);
        if (entry_size > 1024 || entry_size % 128)
                return -EINVAL;

        entries = malloc(n_entries * entry_size);
        if (!entries)
                return -ENOMEM;

        if (fseeko(f, entries_lba * sector_size, SEEK_SET) < 0)
                return -errno;

        if (fread(entries, entry_size, n_entries, f) != n_entries)
                return -EIO;

        /* Search boot and data partition types. */
        entry = entries;
        for (unsigned int i = 1; i <= n_entries; i++) {
                guid_to_uuid(&entry->partition_type_guid, uuid);

                if (boot_partno == 0 && memcmp(uuid, uuid_boot, sizeof(uuid)) == 0)
                        boot_partno = i;
                else if (data_partno == 0 && memcmp(uuid, uuid_data, sizeof(uuid)) == 0)
                        data_partno = i;

                if (boot_partno > 0 && data_partno > 0)
                        break;

                entry = (struct gpt_entry *)((uint8_t *)entry + entry_size);
        }

        if (boot_partno == 0 || data_partno == 0)
                return -EINVAL;

        /* Translate device name to partition device name. */
        if (isdigit(device[strlen(device) - 1])) {
                if (asprintf(&device_boot, "%sp%d", device, boot_partno) < 0)
                        return -ENOMEM;

                if (asprintf(&device_data, "%sp%d", device, data_partno) < 0)
                        return -ENOMEM;
        } else {
                if (asprintf(&device_boot, "%s%d", device, boot_partno) < 0)
                        return -ENOMEM;

                if (asprintf(&device_data, "%s%d", device, data_partno) < 0)
                        return -ENOMEM;
        }

        if (device_bootp) {
                *device_bootp = device_boot;
                device_boot = NULL;
        }

        if (device_datap) {
                *device_datap = device_data;
                device_data = NULL;
        }

        return 0;
}

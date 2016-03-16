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
#include <linux/random.h>
#include <sys/ioctl.h>

#include "uuid.h"
#include "missing.h"

int uuid_from_string(const char *str, uint8_t *uuid) {
        int id[16];

        if (sscanf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                   &id[0], &id[1], &id[2], &id[3], &id[4], &id[5], &id[6], &id[7],
                   &id[8], &id[9], &id[10], &id[11], &id[12], &id[13], &id[14], &id[15]) != 16)
                return -EINVAL;

        for (size_t i = 0; i < 16; i++)
                uuid[i] = id[i];

        return 0;
}

int uuid_to_string(const uint8_t *uuid, char **str) {
        if (asprintf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                     uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]) < 0)
                return -ENOMEM;

        return 0;
}

int uuid_set_random(uint8_t *uuid) {
        if (getrandom(uuid, 16, 0) < 0)
                return -errno;

        /* UUID version 4 */
        uuid[6] = (uuid[6] & 0x0F) | 0x40;

        /* UUID variant DCE */
        uuid[8] = (uuid[8] & 0x3F) | 0x80;

        return 0;
}

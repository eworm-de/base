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
#include <linux/random.h>
#include <sys/ioctl.h>

#include "disk-util.h"

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif

#ifndef BLKSECDISCARD
#define BLKSECDISCARD _IO(0x12,125)
#endif

int block_discard_range(FILE *f, uint64_t start, uint64_t len, bool secure) {
        while (len > 0) {
                uint64_t range[2];
                uint64_t chunk;

                chunk = c_min(len, 1024ULL * 1024ULL * 1024ULL);
                range[0] = start;
                range[1] = chunk;

                if (!secure || ioctl(fileno(f), BLKSECDISCARD, &range) < 0)
                        if (ioctl(fileno(f), BLKDISCARD, &range) < 0)
                                return -errno;

                len -= chunk;
                start += chunk;
        }

        return 0;
}

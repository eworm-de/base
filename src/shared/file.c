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
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include "file.h"

int file_get_size(FILE *f, uint64_t *size) {
        struct stat sb;

        if (fstat(fileno(f), &sb) < 0)
                return -errno;

        if (S_ISREG(sb.st_mode)) {
                *size = sb.st_size;
                return 0;
        }

        if (S_ISBLK(sb.st_mode)) {
                uint64_t s;

                if (ioctl(fileno(f), BLKGETSIZE64, &s) < 0)
                        return -errno;

                *size = s;
                return 0;
        }

        return -EIO;
}

int file_read_line(const char *filename, char **linep) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char buf[4096];
        char *line;
        uint64_t len;

        f = fopen(filename, "re");
        if (!f)
                return -errno;

        if (fgets(buf, sizeof(buf), f) == NULL)
                return -errno;

        len = strlen(buf);
        if (len < 1)
                return -EINVAL;

        if (buf[len - 1] == '\n')
                buf[len - 1] = '\0';

        line = strdup(buf);
        if (!line)
                return -ENOMEM;

        *linep = line;

        return 0;
}

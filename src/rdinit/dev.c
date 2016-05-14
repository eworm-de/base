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
#include "dev.h"

int dev_symlinks(const char *dev) {
        _c_cleanup_(c_closep) int fd = -1;
        static const struct {
                const char *file;
                const char *target;
        } links[] = {
                { "fd",     "/proc/self/fd" },
                { "stdin",  "/proc/self/fd/0" },
                { "stdout", "/proc/self/fd/1" },
                { "stderr", "/proc/self/fd/2" },
        };

        fd = openat(AT_FDCWD, dev, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (fd < 0)
                return -errno;

        for (size_t i = 0; i < C_ARRAY_SIZE(links); i++)
                if (symlinkat(links[i].target, fd, links[i].file) < 0)
                        return -errno;

        return 0;
}

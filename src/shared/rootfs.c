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
#include <bus1/c-shared.h>
#include <sys/stat.h>

#include "rootfs.h"

int rootfs_setup(const char *rootdir) {
        _c_cleanup_(c_closep) int rootfd = -1;
        static const char *dirs[] = {
                "proc",
                "sys",
                "dev",
                "usr",
#if defined(__x86_64__)
                "lib64",
#else
                "lib",
#endif
        };
        static const struct link {
                const char *file;
                const char *target;
        } links[] = {
                { "bin",                        "usr/bin" },
                { "etc",                        "usr/etc" },
                { "lib",                        "usr/lib" },
                { "sbin",                       "usr/bin" },
#if defined(__i386__)
                { "lib/ld-linux.so.2", "../usr/lib/i386-linux-gnu/ld-linux.so.2"
#elif defined(__x86_64__)
                { "lib64/ld-linux-x86-64.so.2", "../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" },
#elif defined(__arm__)
                { "lib/ld-linux-armhf.so.3", "../usr/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3" },
#elif defined(__aarch64__)
                { "lib/ld-linux-aarch64.so.1", "../usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1"  },
#endif
        };
        unsigned int i;

        if (mkdir(rootdir, 0755) < 0)
                return -errno;

        rootfd = openat(AT_FDCWD, rootdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (rootfd < 0)
                return -errno;

        for (i = 0; i < C_ARRAY_SIZE(dirs); i++)
                if (mkdirat(rootfd, dirs[i], 0755) < 0)
                        return -errno;

        for (i = 0; i < C_ARRAY_SIZE(links); i++)
                if (symlinkat(links[i].target, rootfd, links[i].file) < 0)
                        return -errno;

        return 0;
}

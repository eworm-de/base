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
#include <sys/mount.h>
#include <sys/stat.h>
#include "tmpfs-root.h"

int tmpfs_root(const char *rootdir) {
        _c_cleanup_(c_closep) int rootfd = -1;
        static const struct {
                const char *name;
                mode_t mode;
        } dirs[] = {
                { "dev",    0755 },
                { "etc",    0755 },
#if defined(__x86_64__)
                { "lib64",  0755 },
#endif
                { "proc",   0755 },
                { "run",    0755 },
                { "sys",    0755 },
                { "tmp",   01777 },
                { "usr",    0755 },
                { "var",    0755 },
        };
        static const struct {
                const char *file;
                const char *target;
        } links[] = {
                { "bin",                        "usr/bin" },
                { "lib",                        "usr/lib" },
#if defined(__x86_64__)
                { "lib64/ld-linux-x86-64.so.2", "../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" },
#endif
                { "sbin",                       "usr/bin" },
        };

        if (mount("tmpfs", rootdir, "tmpfs", MS_NOSUID|MS_NOEXEC|MS_NODEV|MS_STRICTATIME, "mode=0755,size=5M") < 0)
                return -errno;

        rootfd = openat(AT_FDCWD, rootdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (rootfd < 0)
                return -errno;

        for (size_t i = 0; i < C_ARRAY_SIZE(dirs); i++)
                if (mkdirat(rootfd, dirs[i].name, dirs[i].mode) < 0)
                        return -errno;

        for (size_t i = 0; i < C_ARRAY_SIZE(links); i++)
                if (symlinkat(links[i].target, rootfd, links[i].file) < 0)
                        return -errno;

        return 0;
}

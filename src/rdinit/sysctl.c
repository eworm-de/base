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
#include "sysctl.h"

static const struct {
        const char *path;
        const char *value;
} sysctls[] = {
        /* Fair Queue CoDel packet scheduler */
        { "net/core/default_qdisc", "fq_codel" },

        /* Do not accept source routing */
        { "net/ipv4/conf/default/accept_source_route" ,"0" },
        { "net/ipv4/conf/all/accept_source_route", "0" },

        /* Promote secondary addresses when the primary address is removed */
        { "net/ipv4/conf/default/promote_secondaries", "1" },
        { "net/ipv4/conf/all/promote_secondaries", "1" },

        { "fs/protected_hardlinks", "1" },
        { "fs/protected_symlinks", "1" },

        /* PID, UID, GID, signal, comm */
        { "kernel/core_pattern", "|/usr/bin/org.bus1.coredump %P %u %g %s %e" },
};

int sysctl_apply(void) {
        _c_cleanup_(c_closep) int dfd = -1;

        dfd = openat(AT_FDCWD, "/proc/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (dfd < 0)
                return -errno;

        for (size_t i = 0; i < C_ARRAY_SIZE(sysctls); i++) {
                _c_cleanup_(c_closep) int fd = -1;
                ssize_t len, n;

                fd = openat(dfd, sysctls[i].path, O_WRONLY|O_NONBLOCK|O_CLOEXEC);
                if (fd < 0) {
                        if (errno == ENOENT)
                                continue;

                        return -errno;
                }

                len = strlen(sysctls[i].value);
                n = write(fd, sysctls[i].value, len);
                if (n != len && errno != ENOENT)
                        return -errno;
        }

        return 0;
}

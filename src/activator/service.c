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

#include <bus1/b1-platform.h>
#include <bus1/c-macro.h>
#include <bus1/c-shared.h>
#include <linux/sched.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "service.h"
#include "kmsg.h"
#include "tmpfs-root.h"
#include "util.h"

pid_t service_activate(Service *s) {
        pid_t p;
        static const char *mounts[] = {
                "/dev",
                "/proc",
                "/sys",
                "/usr",
        };
        unsigned int i;
        _c_cleanup_(c_freep) char *datadir = NULL;
        _c_cleanup_(c_freep) char *exe = NULL;
        const char *argv[2] = {};
        int r;

        if (s->persistent_data) {
                if (asprintf(&datadir, "/var/bus1/%s", s->name) < 0)
                        return -ENOMEM;

                if (mkdir(datadir, 0755) < 0 && errno != EEXIST)
                        return -errno;
        }

        p = c_sys_clone(SIGCHLD|CLONE_NEWNS|CLONE_NEWIPC, NULL);
        if (p < 0)
                return -errno;
        if (p > 0)
                return p;

        if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                return -errno;

        if (setsid() < 0)
                return -errno;

        r = tmpfs_root("/tmp");
        if (r < 0)
                return r;

        for (i = 0; i < C_ARRAY_SIZE(mounts); i++) {
                _c_cleanup_(c_freep) char *target = NULL;

                if (asprintf(&target, "/tmp%s", mounts[i]) < 0)
                        return -ENOMEM;

                if (mount(mounts[i], target, NULL, MS_BIND|MS_REC, NULL) < 0)
                        return -errno;
        }

        if (mount("/tmp/usr/etc", "/tmp/etc", NULL, MS_BIND, NULL) < 0)
                return -errno;

        if (s->persistent_data && mount(datadir, "/tmp/var", NULL, MS_BIND, NULL) < 0)
                return -errno;

        if (chdir("/tmp") < 0)
                return -errno;

        if (mount("/tmp", "/", NULL, MS_MOVE, NULL) < 0)
                return -errno;

        if (chroot(".") < 0)
                return -errno;

        if (chdir("/") < 0)
                return -errno;

        if (asprintf(&exe, "/usr/bin/%s", s->name) < 0)
                return -ENOMEM;

        kmsg(LOG_INFO, "Activating service %s.", s->name);
        argv[0] = exe;
        execve(argv[0], (char **)argv, NULL);

        return -errno;
}

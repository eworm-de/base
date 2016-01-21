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
#include <bus1/c-sys.h>
#include <linux/sched.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "service.h"
#include "kmsg-util.h"
#include "tmpfs-root-util.h"
#include "util.h"

int service_new(const char *name, Service **service) {
        _c_cleanup_(service_freep) Service *s = NULL;

        s = calloc(1, sizeof(Service));
        if (!s)
                return -ENOMEM;

        s->name = strdup(name);
        if (!s->name)
                return -ENOMEM;

        s->pid = -1;

        *service = s;
        s = NULL;

       return 0;
}

Service *service_free(Service *s) {
        char *p;

        for (p = s->argv[0]; p; p++)
                free(p);
        free(s->argv);

        for (p = s->envp[0]; p; p++)
                free(p);
        free(s->envp);

        free(s->name);
        free(s);

        return NULL;
}

int service_terminate(Service *s) {
        assert(s->pid >= 0);

        if (kill(s->pid, SIGTERM) < 0)
                return -errno;

        s->terminated = true;

        return 0;
}

int service_activate(Service *s) {
        pid_t p;
        static const char *mounts[] = {
                "/dev",
                "/proc",
                "/sys",
                "/usr",
        };
        unsigned int i;
        _c_cleanup_(c_freep) char *datadir = NULL;
        int r;

        assert(s->pid < 0);
        assert(!s->terminated);

        if (!s->argv) {
                _c_cleanup_(c_freep) char *exe = NULL;

                s->argv = calloc(2, sizeof(char *));
                if (!s->argv)
                        return -ENOMEM;

                if (asprintf(&s->argv[0], "/usr/bin/%s", s->name) < 0)
                        return -ENOMEM;
        }

        if (s->persistent_data) {
                if (asprintf(&datadir, "/var/bus1/%s", s->name) < 0)
                        return -ENOMEM;

                if (mkdir(datadir, 0755) < 0 && errno != EEXIST)
                        return -errno;
        }

        p = c_sys_clone(SIGCHLD|CLONE_NEWNS|CLONE_NEWIPC, NULL);
        if (p < 0)
                return -errno;

        if (p > 0) {
                s->pid = p;
                return 0;
        }

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

        if (s->gid > 0 && setresgid(s->gid, s->gid, s->gid) < 0)
                return -errno;

        if (s->uid > 0 && setresuid(s->uid, s->uid, s->uid) < 0)
                return -errno;

        kmsg(LOG_INFO, "Activating service %s.", s->name);
        execve(s->argv[0], s->argv, s->envp);

        return -errno;
}

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

#include <string.h>

#include <org.bus1/c-macro.h>

#include "sysfs.h"

static int enumerate_devices(int sysfd,
                             const char *devicesdir,
                             bool subdir,
                             const char *devtype,
                             int devfd,
                             int (*cb)(int sysfd,
                                       const char *devpath,
                                       const char *subsystem,
                                       const char *devtype,
                                       int devfd,
                                       const char *devname,
                                       const char *modalias,
                                       void *userdata),
                             void *userdata) {
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        int r;

        dfd = openat(sysfd, devicesdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return -errno;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;

        dfd = -1;

        /* /sys/bus/$SUBSYSTEM/devices/$DEVICE, /sys/class/$SUBSYSTEM/$DEVICE */
        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
                _c_cleanup_(c_closep) int dfd2 = -1;
                int fd;
                _c_cleanup_(c_fclosep) FILE *f = NULL;
                char line[4096];
                const char *prefix;
                char *s;
                ssize_t len;
                _c_cleanup_(c_freep) char *dp = NULL;
                _c_cleanup_(c_freep) char *ss = NULL;
                _c_cleanup_(c_freep) char *dt = NULL;
                _c_cleanup_(c_freep) char *dn = NULL;
                _c_cleanup_(c_freep) char *ma = NULL;

                if (d->d_name[0] == '.')
                        continue;

                len = readlinkat(dirfd(dir), d->d_name, line, sizeof(line));
                if (len < 0)
                        continue;
                if (len <= 0 || len == (ssize_t)sizeof(line))
                        continue;
                line[len] = '\0';

                if (subdir)
                        prefix = "../../../";
                else
                        prefix = "../../";

                if (strncmp(line, prefix, strlen(prefix) != 0))
                                continue;

                dp = strdup(line + strlen(prefix));
                if (!dp)
                        return -ENOMEM;

                dfd2 = openat(dirfd(dir), d->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd2 < 0)
                        continue;

                len = readlinkat(dfd2, "subsystem", line, sizeof(line));
                if (len < 0)
                        continue;
                if (len <= 0 || len == (ssize_t)sizeof(line))
                        continue;
                line[len] = '\0';

                s = strrchr(line, '/');
                if (!s)
                        continue;
                ss = strdup(s + 1);
                if (!ss)
                        return -ENOMEM;

                fd = openat(dfd2, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
                if (fd < 0)
                        continue;

                f = fdopen(fd, "re");
                if (!f)
                        continue;

                while (fgets(line, sizeof(line), f) != NULL) {
                        char *value;
                        char *end;

                        /* some broken drivers add another newline */
                        if (strcmp(line, "\n") == 0)
                                continue;

                        value = strchr(line, '=');
                        if (!value)
                                continue;
                        *value = '\0';
                        value++;

                        end = strchr(value, '\n');
                        if (!end)
                                continue;
                        *end = '\0';

                        if (strcmp(line, "DEVTYPE") == 0) {
                                dt = strdup(value);
                                if (!dt)
                                        return -ENOMEM;
                        } else if (strcmp(line, "DEVNAME") == 0) {
                                dn = strdup(value);
                                if (!dn)
                                        return -ENOMEM;
                        } else if (strcmp(line, "MODALIAS") == 0) {
                                ma = strdup(value);
                                if (!ma)
                                        return -ENOMEM;
                        }
                }

                if (devtype) {
                        if (!dt)
                                continue;
                        if (strcmp(devtype, dt) != 0)
                                continue;
                }

                r = cb(sysfd, dp, ss, dt, devfd, dn, ma, userdata);
                if (r < 0 || r == 1)
                        return r;
        }

        return 0;
}

static int enumerate_subsystems(int sysfd,
                                const char *subsystemsdir,
                                const char *subsystem,
                                const char *subdir,
                                const char *devtype,
                                int devfd,
                                int (*cb)(int sysfd,
                                          const char *devpath,
                                          const char *subsystem,
                                          const char *devtype,
                                          int devfd,
                                          const char *devname,
                                          const char *modalias,
                                          void *userdata),
                                void *userdata) {
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        int r;

        /* specific subsystem */
        if (subsystem) {
                _c_cleanup_(c_freep) char *sd = NULL;

                if (subdir) {
                        if (asprintf(&sd, "%s/%s/%s", subsystemsdir, subsystem, subdir) < 0)
                                return -ENOMEM;
                } else {
                        if (asprintf(&sd, "%s/%s", subsystemsdir, subsystem) < 0)
                                return -ENOMEM;
                }

                return enumerate_devices(sysfd, sd, !!subdir, devtype, devfd, cb, userdata);
        }

        /* all subsystems at /sys/bus/$SUBSYSTEM or /sys/class/$SUBSYSTEM */
        dfd = openat(sysfd, subsystemsdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return -errno;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;
        dfd = -1;

        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
                _c_cleanup_(c_freep) char *sd = NULL;

                if (d->d_name[0] == '.')
                        continue;

                if (subdir) {
                        if (asprintf(&sd, "%s/%s/%s", subsystemsdir, d->d_name, subdir) < 0)
                                return -ENOMEM;
                } else {
                        if (asprintf(&sd, "%s/%s", subsystemsdir, d->d_name) < 0)
                                return -ENOMEM;
                }

                /* /sys/bus/$SUBSYSTEM/devices or /sys/class/$SUBSYSTEM */
                r = enumerate_devices(sysfd, sd ?: d->d_name, !!subdir, devtype, devfd, cb, userdata);
                if (r < 0)
                        return r;
        }

        return 0;
}

int sysfs_enumerate(int sysfd,
                    const char *subsystem,
                    const char *devtype,
                    int devfd,
                    int (*cb)(int sysfd,
                              const char *devpath,
                              const char *subsystem,
                              const char *devtype,
                              int devfd,
                              const char *devname,
                              const char *modalias,
                              void *userdata),
                    void *userdata) {
        int r;

        /* /sys/bus/$SUBSYSTEM/devices/ */
        r = enumerate_subsystems(sysfd, "bus", subsystem, "devices", devtype, devfd, cb, userdata);
        if (r < 0 && r != -ENOENT)
                return r;

        /* /sys/class/$SUBSYSTEM/ */
        return enumerate_subsystems(sysfd, "class", subsystem, NULL, devtype, devfd, cb, userdata);
}

int sysfs_get_seqnum(int sysfd, uint64_t *seqnump) {
        _c_cleanup_(c_closep) int fd = -1;
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char line[32];
        uint64_t seqnum;

        assert(seqnump);

        fd = openat(sysfd, "kernel/uevent_seqnum", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0)
                return -errno;

        f = fdopen(fd, "re");
        if (!f)
                return -errno;

        if (!fgets(line, sizeof(line), f))
                return -EIO;

        errno = 0;
        seqnum = strtoull(line, NULL, 10);
        if (errno != 0)
                return -errno;

        *seqnump = seqnum;
        return 0;
}

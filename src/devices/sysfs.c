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

static ssize_t uevent_get_key_value(char *buf, size_t n_buf, const char **keyp, const char **valuep, char **nextp) {
        char *end, *value;

        end = strchr(buf, '\n');
        if (!end)
                return -EBADMSG;

        *end = '\0';
        end ++;

        value = strchr(buf, '=');
        if (!value)
                return -EBADMSG;

        *value = '\0';
        value ++;

        *keyp = buf;
        *valuep = value;
        *nextp = end;

        return n_buf - (end - buf);
}

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
                char buf[4096];
                ssize_t buflen = sizeof(buf);
                char *bufp = buf;
                const char *prefix;
                char *s;
                ssize_t len;
                const char *dp = NULL, *ss = NULL, *dt = NULL, *dn = NULL, *ma = NULL;

                if (d->d_name[0] == '.')
                        continue;

                len = readlinkat(dirfd(dir), d->d_name, buf, buflen);
                if (len < 0)
                        continue;
                if (len <= 0 || len == buflen)
                        continue;
                bufp[len] = '\0';

                if (subdir)
                        prefix = "../../../devices/";
                else
                        prefix = "../../devices/";

                if (strncmp(bufp, prefix, strlen(prefix) != 0))
                                continue;

                dp = bufp + strlen(prefix);
                bufp += len + 1;
                buflen -= len + 1;

                dfd2 = openat(dirfd(dir), d->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd2 < 0)
                        continue;

                len = readlinkat(dfd2, "subsystem", bufp, buflen);
                if (len < 0)
                        continue;
                if (len <= 0 || len == buflen)
                        continue;
                bufp[len] = '\0';

                s = strrchr(bufp, '/');
                if (!s)
                        continue;
                ss = s + 1;
                bufp += len + 1;
                buflen -= len + 1;

                fd = openat(dfd2, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
                if (fd < 0)
                        continue;

                f = fdopen(fd, "re");
                if (!f)
                        continue;

                len = fread(bufp, buflen, 1, f);
                if (ferror(f))
                        return -EIO;

                while (len > 0) {
                        const char *key, *value;

                        /* some broken drivers add another newline */
                        if (*bufp == '\n') {
                                bufp ++;
                                len --;
                                continue;
                        }

                        len = uevent_get_key_value(bufp, len, &key, &value, &bufp);
                        if (len < 0)
                                return len;

                        if (strcmp(key, "DEVTYPE") == 0)
                                dt = value;
                        else if (strcmp(key, "DEVNAME") == 0)
                                dn = value;
                        else if (strcmp(key, "MODALIAS") == 0)
                                ma = value;
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

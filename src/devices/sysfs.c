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

static int enumerate_device(int sysfd,
                            const char *sysname,
                            bool subdir,
                            int (*cb)(const char *devpath,
                                      const char *subsystem,
                                      const char *devtype,
                                      const char *devname,
                                      const char *modalias,
                                      void *userdata),
                            void *userdata) {
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_closep) int fd = -1;
        char buf[4096];
        ssize_t buflen = sizeof(buf);
        char *bufp = buf;
        const char *prefix;
        ssize_t len;
        const char *dp = NULL, *ss = NULL, *dt = NULL, *dn = NULL, *ma = NULL;
        int r;

        len = readlinkat(sysfd, sysname, buf, buflen);
        if (len < 0)
                return 0;
        if (len <= 0 || len == buflen)
                return 0;
        bufp[len] = '\0';

        if (subdir)
                prefix = "../../../devices/";
        else
                prefix = "../../devices/";

        if (strncmp(bufp, prefix, strlen(prefix) != 0))
                        return 0;

        dp = bufp + strlen(prefix);
        bufp += len + 1;
        buflen -= len + 1;

        dfd = openat(sysfd, sysname, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return 0;

        len = readlinkat(dfd, "subsystem", bufp, buflen);
        if (len < 0)
                return 0;
        if (len <= 0 || len == buflen)
                return 0;
        bufp[len] = '\0';

        ss = strrchr(bufp, '/');
        if (!ss)
                return 0;
        ss ++;
        bufp += len + 1;
        buflen -= len + 1;

        fd = openat(dfd, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0)
                return 0;

        for (;;) {
                errno = 0;
                len = read(fd, bufp, buflen);
                if (len < 0 || len == buflen) {
                        if (errno == EINTR)
                                continue;
                        return -errno ?: -EIO;
                } else {
                        buflen = len;
                        break;
                }
        }

        while (buflen > 0) {
                const char *key, *value;

                /* some broken drivers add another newline */
                if (*bufp == '\n') {
                        bufp ++;
                        buflen --;
                        continue;
                }

                buflen = uevent_get_key_value(bufp, buflen, &key, &value, &bufp);
                if (buflen < 0)
                        return buflen;

                if (strcmp(key, "DEVTYPE") == 0)
                        dt = value;
                else if (strcmp(key, "DEVNAME") == 0)
                        dn = value;
                else if (strcmp(key, "MODALIAS") == 0)
                        ma = value;
        }

        r = cb(dp, ss, dt, dn, ma, userdata);
        if (r < 0 || r == 1)
                return r;

        return 0;
}

static int enumerate_devices(int sysfd,
                             const char *devicesdir,
                             const char *subdir,
                             int (*cb)(const char *devpath,
                                       const char *subsystem,
                                       const char *devtype,
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

        if (subdir) {
                int dfd_subdir;

                dfd_subdir = openat(dfd, subdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd_subdir < 0)
                        return -errno;

                close(dfd);
                dfd = dfd_subdir;
        }

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;

        dfd = -1;

        /* /sys/bus/$SUBSYSTEM/devices/$DEVICE, /sys/class/$SUBSYSTEM/$DEVICE */
        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
                if (d->d_name[0] == '.')
                        continue;

                r = enumerate_device(dirfd(dir), d->d_name, !!subdir, cb, userdata);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int enumerate_subsystems(int sysfd,
                                const char *subsystemsdir,
                                const char *subdir,
                                int (*cb)(const char *devpath,
                                          const char *subsystem,
                                          const char *devtype,
                                          const char *devname,
                                          const char *modalias,
                                          void *userdata),
                                void *userdata) {
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        int r;

        /* all subsystems at /sys/bus/$SUBSYSTEM or /sys/class/$SUBSYSTEM */
        dfd = openat(sysfd, subsystemsdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return -errno;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;
        dfd = -1;

        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
                if (d->d_name[0] == '.')
                        continue;

                /* /sys/bus/$SUBSYSTEM/devices or /sys/class/$SUBSYSTEM */
                r = enumerate_devices(dirfd(dir), d->d_name, subdir, cb, userdata);
                if (r < 0)
                        return r;
        }

        return 0;
}

int sysfs_enumerate(int sysfd,
                    int (*cb)(const char *devpath,
                              const char *subsystem,
                              const char *devtype,
                              const char *devname,
                              const char *modalias,
                              void *userdata),
                    void *userdata) {
        int r;

        /* /sys/bus/$SUBSYSTEM/devices/ */
        r = enumerate_subsystems(sysfd, "bus", "devices", cb, userdata);
        if (r < 0 && r != -ENOENT)
                return r;

        /* /sys/class/$SUBSYSTEM/ */
        return enumerate_subsystems(sysfd, "class", NULL, cb, userdata);
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

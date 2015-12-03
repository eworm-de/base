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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include "c-shared.h"
#include "uevent.h"

enum {
        UEVENT_BROADCAST_KERNEL = 1,
};

static int sysfs_coldplug(int devfd, const char *path, const char *subdir,
                          int (* cb)(int devfd,
                                     const char *subsystem, const char *devtype,
                                     const char *devname, const char *modalias)) {
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        struct dirent *d;
        int r;

        /* /sys/bus, /sys/class */
        dfd = openat(AT_FDCWD, path, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return -errno;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;
        dfd = -1;

        /* /sys/bus/pci, /sys/class/block */
        for (d = readdir(dir); d; d = readdir(dir)) {
                _c_cleanup_(c_closep) int dfd2 = -1;
                _c_cleanup_(c_closedirp) DIR *dir2 = NULL;
                struct dirent *d2;

                if (d->d_name[0] == '.')
                        continue;

                dfd2 = openat(dirfd(dir), d->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd2 < 0)
                        continue;

                /* /sys/bus/pci/devices */
                if (subdir) {
                        _c_cleanup_(c_closep) int dfd3 = -1;

                        dfd3 = openat(dfd2, subdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (dfd3 < 0)
                                continue;

                        close(dfd2);
                        dfd2 = dfd3;
                        dfd3 = -1;
                }

                dir2 = fdopendir(dfd2);
                if (!dir2)
                        return -errno;
                dfd2 = -1;

                /* /sys/bus/pci/0000:00:00.0, /sys/class/block/sda */
                for (d2 = readdir(dir2); d2; d2 = readdir(dir2)) {
                        _c_cleanup_(c_closep) int dfd3 = -1;
                        int fd;
                        _c_cleanup_(c_fclosep) FILE *f = NULL;
                        char line[4096];
                        char *s;
                        ssize_t len;
                        _c_cleanup_(c_freep) char *subsystem = NULL;
                        _c_cleanup_(c_freep) char *devtype = NULL;
                        _c_cleanup_(c_freep) char *devname = NULL;
                        _c_cleanup_(c_freep) char *modalias = NULL;

                        if (d2->d_name[0] == '.')
                                continue;

                        dfd3 = openat(dirfd(dir2), d2->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (dfd3 < 0)
                                continue;

                        len = readlinkat(dfd3, "subsystem", line, sizeof(line));
                        if (len < 0)
                                continue;
                        if (len <= 0 || len == (ssize_t)sizeof(line))
                                continue;
                        line[len] = '\0';

                        s = strrchr(line, '/');
                        if (!s)
                                continue;
                        subsystem = strdup(s + 1);
                        if (!subsystem)
                                return -ENOMEM;

                        fd = openat(dfd3, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
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
                                        devtype = strdup(value);
                                        if (!devtype)
                                                return -ENOMEM;
                                } else if (strcmp(line, "DEVNAME") == 0) {
                                        devname = strdup(value);
                                        if (!devname)
                                                return -ENOMEM;
                                } else if (strcmp(line, "MODALIAS") == 0) {
                                        modalias = strdup(value);
                                        if (!modalias)
                                                return -ENOMEM;
                                }
                        }

                        r = cb(devfd, subsystem, devtype, devname, modalias);
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}


int uevent_coldplug(int devfd, int (*cb)(int devfd,
                                         const char *subsystem, const char *devtype,
                                         const char *devname, const char *modalias)) {
        int r;

        r = sysfs_coldplug(devfd, "/sys/bus", "devices", cb);
        if (r < 0)
                return r;

        return sysfs_coldplug(devfd, "/sys/class", NULL, cb);
}

int uevent_connect(void) {
        int sk;
        struct sockaddr_nl nl = {};
        const int on = 1;
        const int size = 16 * 1024 * 1024;

        sk = socket(PF_NETLINK, SOCK_RAW|SOCK_CLOEXEC|SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
        if (sk < 0)
                return sk;

        if (setsockopt(sk, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0)
                return -errno;

        if (setsockopt(sk, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0)
                return -errno;

        nl.nl_family = AF_NETLINK;
        nl.nl_groups = UEVENT_BROADCAST_KERNEL;
        if (bind(sk, (struct sockaddr *)&nl, sizeof(struct sockaddr_nl)) < 0)
                return -errno;

        return sk;
}

int uevent_receive(int sk, char **action, char **subsystem, char **devtype,
                           char **devname, char **modalias) {
        struct msghdr smsg = {};
        struct iovec iov = {};
        struct sockaddr_nl nl = {};
        char cred_msg[CMSG_SPACE(sizeof(struct ucred))];
        struct cmsghdr *cmsg;
        struct ucred *cred;
        char buf[4096];
        ssize_t buflen;
        const char *s;
        _c_cleanup_(c_freep) char *ac = NULL;
        _c_cleanup_(c_freep) char *ss = NULL;
        _c_cleanup_(c_freep) char *dt = NULL;
        _c_cleanup_(c_freep) char *dn = NULL;
        _c_cleanup_(c_freep) char *ma = NULL;
        unsigned int i;

        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        smsg.msg_iov = &iov;
        smsg.msg_iovlen = 1;

        smsg.msg_control = cred_msg;
        smsg.msg_controllen = sizeof(cred_msg);

        nl.nl_family = AF_NETLINK;
        nl.nl_groups = UEVENT_BROADCAST_KERNEL;
        smsg.msg_name = &nl;
        smsg.msg_namelen = sizeof(nl);

        buflen = recvmsg(sk, &smsg, 0);
        if (buflen < 32 || (smsg.msg_flags & MSG_TRUNC))
                return -EBADMSG;

        if (nl.nl_groups != UEVENT_BROADCAST_KERNEL)
                return -EIO;

        if (nl.nl_pid > 0)
                return -EIO;

        cmsg = CMSG_FIRSTHDR(&smsg);
        if (cmsg == NULL || cmsg->cmsg_type != SCM_CREDENTIALS)
                return -EBADMSG;

        cred = (struct ucred *)CMSG_DATA(cmsg);
        if (cred->uid != 0)
                return -EIO;

        /* skip header */
        s = memchr(buf, '\0', buflen);
        if (!s)
                return -EBADMSG;
        i = s + 1 - buf;

        while (i < buflen) {
                char *key;
                const char *end;
                char *value;

                key = (char*)&buf[i];
                end = memchr(key, '\0', buflen - i);
                if (!end)
                        return -EINVAL;
                i += end - key + 1;

                value = strchr(key, '=');
                if (!value)
                        return -EINVAL;

                *value = '\0';
                value++;

                if (strcmp(key, "ACTION") == 0) {
                        ac = strdup(value);
                        if (!ac)
                                return -ENOMEM;
                } else if (strcmp(key, "SUBSYSTEM") == 0) {
                        free(ss);
                        ss = strdup(value);
                        if (!ss)
                                return -ENOMEM;
                } else if (strcmp(key, "DEVTYPE") == 0) {
                        free(dt);
                        dt = strdup(value);
                        if (!dt)
                                return -ENOMEM;
                } else if (strcmp(key, "DEVNAME") == 0) {
                        free(dn);
                        dn = strdup(value);
                        if (!dn)
                                return -ENOMEM;
                } else if (strcmp(key, "MODALIAS") == 0) {
                        free(ma);
                        ma = strdup(value);
                        if (!ma)
                                return -ENOMEM;
                }
        }

        if (!ac || !ss)
                return -EBADMSG;

        if (action) {
                *action = ac;
                ac = NULL;
        }

        if (subsystem) {
                *subsystem = ss;
                ss = NULL;
        }

        if (devtype) {
                *devtype = dt;
                 dt = NULL;
        }

        if (devname) {
                *devname = dn;
                dn = NULL;
        }

        if (modalias) {
                *modalias = ma;
                ma = NULL;
        }

        return 0;
}

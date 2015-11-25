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

#include "uevent.h"

enum {
        UEVENT_BROADCAST_KERNEL = 1,
};

static int sysfs_coldplug(int devfd, const char *path, const char *subdir,
                          int (* cb)(int devfd,
                                     const char *subsystem, const char *devtype,
                                     const char *devname, const char *modalias)) {
        int dfd;
        DIR *dir;
        struct dirent *dent;
        int r;

        /* /sys/bus, /sys/class */
        dfd = openat(AT_FDCWD, path, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return dfd;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;

        /* /sys/bus/pci, /sys/class/block */
        for (dent = readdir(dir); dent; dent = readdir(dir)) {
                DIR *dir2;
                struct dirent *dent2;

                if (dent->d_name[0] == '.')
                        continue;

                dfd = openat(dirfd(dir), dent->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd < 0)
                        return dfd;

                /* /sys/bus/pci/devices */
                if (subdir) {
                        int fd;

                        fd = openat(dfd, subdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (fd < 0)
                                return fd;

                        close(dfd);
                        dfd = fd;
                }

                dir2 = fdopendir(dfd);
                if (!dir2)
                        return -errno;

                /* /sys/bus/pci/0000:00:00.0, /sys/class/block/sda */
                for (dent2 = readdir(dir2); dent2; dent2 = readdir(dir2)) {
                        int fd;
                        FILE *f;
                        char line[4096];
                        char *s;
                        ssize_t len;
                        char *subsystem = NULL;
                        char *devtype = NULL;
                        char *devname = NULL;
                        char *modalias = NULL;

                        if (dent2->d_name[0] == '.')
                                continue;

                        dfd = openat(dirfd(dir2), dent2->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (dfd < 0) {
                                if (errno == ENOTDIR)
                                        continue;

                                return dfd;
                        }

                        len = readlinkat(dfd, "subsystem", line, sizeof(line));
                        if (len < 0)
                                return -EINVAL;
                        if (len <= 0 || len == (ssize_t)sizeof(line))
                                return -EINVAL;
                        line[len] = '\0';

                        s = strrchr(line, '/');
                        if (!s)
                                return -EINVAL;
                        subsystem = strdup(s + 1);
                        if (!subsystem)
                                return -ENOMEM;

                        fd = openat(dfd, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
                        if (fd < 0)
                                return fd;

                        f = fdopen(fd, "re");
                        if (!f)
                                return -errno;

                        while (fgets(line, sizeof(line), f) != NULL) {
                                char *value;
                                char *end;

                                /* some broken drivers add another newline */
                                if (strcmp(line, "\n") == 0)
                                        continue;

                                value = strchr(line, '=');
                                if (!value)
                                        return -EINVAL;
                                *value = '\0';
                                value++;

                                end = strchr(value, '\n');
                                if (!end)
                                        return -EINVAL;
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

                        free(subsystem);
                        free(devtype);
                        free(devname);
                        free(modalias);

                        fclose(f);
                        close(dfd);
                }

                closedir(dir2);
        }

        closedir(dir);

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
        int r;

        sk = socket(PF_NETLINK, SOCK_RAW|SOCK_CLOEXEC|SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
        if (sk < 0)
                return sk;

        r = setsockopt(sk, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
        if (r < 0)
                return -errno;

        r = setsockopt(sk, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size));
        if (r < 0)
                return -errno;

        nl.nl_family = AF_NETLINK;
        nl.nl_groups = UEVENT_BROADCAST_KERNEL;
        r = bind(sk, (struct sockaddr *)&nl, sizeof(struct sockaddr_nl));
        if (r < 0)
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
        char *ac = NULL, *ss = NULL, *dt = NULL, *dn = NULL, *ma = NULL;
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

                if (strcmp(key, "ACTION") == 0)
                        ac = value;
                if (strcmp(key, "SUBSYSTEM") == 0)
                        ss = value;
                else if (strcmp(key, "DEVTYPE") == 0)
                        dt = value;
                else if (strcmp(key, "DEVNAME") == 0)
                        dn = value;
                else if (strcmp(key, "MODALIAS") == 0)
                        ma = value;
        }

        if (!ac || !ss)
                return -EBADMSG;

        if (action && ac) {
                *action = strdup(ac);
                if (!*action)
                        return -ENOMEM;
        }

        if (subsystem && ss) {
                *subsystem = strdup(ss);
                if (!*subsystem)
                        return -ENOMEM;
        }

        if (devtype && dt) {
                *devtype = strdup(dt);
                if (!*devtype)
                        return -ENOMEM;
        }

        if (devname && dn) {
                *devname = strdup(dn);
                if (!*devname)
                        return -ENOMEM;
        }

        if (modalias && ma) {
                *modalias = strdup(ma);
                if (!*modalias)
                        return -ENOMEM;
        }

        return 0;
}

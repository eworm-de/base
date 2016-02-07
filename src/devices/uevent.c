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

#include <org.bus1/c-macro.h>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <string.h>
#include <sys/socket.h>

#include "uevent.h"

enum {
        UEVENT_BROADCAST_KERNEL = 1,
};

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
        int i;

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

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

#include <linux/filter.h>
#include <linux/netlink.h>
#include <string.h>
#include <sys/socket.h>

#include <org.bus1/c-macro.h>

#include "sysfs.h"
#include "uevent.h"

enum {
        UEVENT_BROADCAST_KERNEL = 1,
};

struct uevent_subscription {
        uint64_t seqnum;
        struct uevent_subscription *previous;
        struct uevent_subscription *next;
        int (*cb)(void *userdata);
        void *userdata;
};

void uevent_subscription_unlink(struct uevent_subscriptions *uss,
                                struct uevent_subscription *us) {

        assert(uss);

        if (!us)
                return;

        if (uss->head == us)
                uss->head = us->next;
        if (uss->tail == us)
                uss->tail = us->previous;

        if (us->previous)
                us->previous->next = us->next;
        if (us->next)
                us->next->previous = us->previous;
}

struct uevent_subscription *uevent_subscription_free(struct uevent_subscription *us) {
        if (!us)
                return NULL;

        assert(!us->previous);
        assert(!us->next);

        free(us);

        return NULL;
}

int uevent_subscriptions_init(struct uevent_subscriptions *uss, int sysfd) {
        int r;

        assert(uss);

        r = sysfs_get_seqnum(sysfd, &uss->seqnum);
        if (r < 0)
                return r;

        uss->head = NULL;
        uss->tail = NULL;

        return 0;
}

void uevent_subscriptions_destroy(struct uevent_subscriptions *uss) {
        assert(uss);
        assert(!uss->head);
        assert(!uss->tail);
}

int uevent_sysfs_sync(struct uevent_subscriptions *uss,
                      int sysfd,
                      struct uevent_subscription **usd,
                      int (*cb)(void *userdata),
                      void *userdata) {
        struct uevent_subscription *us;
        uint64_t seqnum;
        int r;

        assert(uss);

        r = sysfs_get_seqnum(sysfd, &seqnum);
        if (r < 0)
                return r;

        if (seqnum < uss->seqnum)
                return -EIO;
        else if (seqnum == uss->seqnum) {
                r = cb(userdata);
                if (r < 0)
                        return r;

                *usd = NULL;

                return r;
        }

        us = malloc(sizeof(*us));
        if (!us)
                return -ENOMEM;

        us->seqnum = seqnum;
        us->cb = cb;
        us->userdata = userdata;
        us->next = NULL;
        us->previous = uss->tail;
        uss->tail = us;
        if (!uss->head)
                uss->head = us;

        *usd = us;

        return 0;
}

int uevent_subscriptions_dispatch(struct uevent_subscriptions *uss, uint64_t seqnum) {
        struct uevent_subscription *us;
        int r;

        assert(uss);

        uss->seqnum = seqnum;

        while((us = uss->head) && us->seqnum <= seqnum) {
                uevent_subscription_unlink(uss, us);
                r = us->cb(us->userdata);
                if (r < 0)
                        return r;
        }

        return 0;
}

int uevent_subscriptions_dispatch_all(struct uevent_subscriptions *uss) {
        return uevent_subscriptions_dispatch(uss, (uint64_t)-1);
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
                           char **devname, char **modalias, uint64_t *seqnum) {
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
        uint64_t sn = 0;
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
                } else if (strcmp(key, "SEQNUM") == 0) {
                        errno = 0;
                        sn = strtoull(value, NULL, 10);
                        if (errno != 0)
                                return -errno;
                }
        }

        if (!ac || !ss || !sn)
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

        if (seqnum)
                *seqnum = sn;

        return 0;
}

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
#include <linux/filter.h>
#include <linux/netlink.h>
#include <string.h>
#include <sys/socket.h>
#include "device.h"
#include "sysfs.h"
#include "uevent.h"

enum {
        UEVENT_BROADCAST_KERNEL = 1,
};

void uevent_subscription_unlink(struct uevent_subscriptions *uss,
                                struct uevent_subscription *us) {

        assert(uss);

        if (!us)
                return;

        c_list_remove(&uss->list, &us->le);
}

void uevent_subscription_init(struct uevent_subscription *us) {
        if (!us)
                return;

        *us = (struct uevent_subscription){};
        c_list_entry_init(&us->le);
}

void uevent_subscription_destroy(struct uevent_subscription *us) {
        if (!us)
                return;

        assert(!c_list_entry_prev(&us->le));
        assert(!c_list_entry_next(&us->le));

        return;
}

int uevent_subscriptions_init(struct uevent_subscriptions *uss, int sysfd) {
        int r;

        assert(uss);

        r = sysfs_get_seqnum(sysfd, &uss->seqnum);
        if (r < 0)
                return r;

        c_list_init(&uss->list);

        return 0;
}

void uevent_subscriptions_destroy(struct uevent_subscriptions *uss) {
        assert(uss);
        assert(!c_list_first(&uss->list));
        assert(!c_list_last(&uss->list));
}

int uevent_sysfs_sync(struct uevent_subscriptions *uss,
                      int sysfd,
                      struct uevent_subscription *us,
                      int (*cb)(void *userdata),
                      void *userdata) {
        uint64_t seqnum;
        int r;

        assert(uss);
        assert(us);

        r = sysfs_get_seqnum(sysfd, &seqnum);
        if (r < 0)
                return r;

        if (seqnum < uss->seqnum)
                return -EIO;

        us->seqnum = seqnum;
        us->cb = cb;
        us->userdata = userdata;
        c_list_entry_init(&us->le);
        c_list_append(&uss->list, &us->le);

        return 0;
}

int uevent_subscriptions_dispatch(struct uevent_subscriptions *uss, uint64_t seqnum) {
        CListEntry *le;
        struct uevent_subscription *us;
        int r;

        assert(uss);

        if (seqnum == 0)
                seqnum = uss->seqnum;
        else if (seqnum != (uint64_t) -1)
                uss->seqnum = seqnum;

        while ((le = c_list_first(&uss->list))) {
                us = c_container_of(le, struct uevent_subscription, le);

                if (us->seqnum > seqnum)
                        break;

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

int uevent_subscriptions_dispatch_idle(struct uevent_subscriptions *uss) {
        return uevent_subscriptions_dispatch(uss, 0);
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

int uevent_receive(Manager *m, struct device **devicep, int *actionp, uint64_t *seqnump) {
        struct device *device;
        char buf[4096];
        ssize_t buflen;
        struct iovec iov = {
                .iov_base = buf,
                .iov_len = sizeof(buf),
        };
        char cred_msg[CMSG_SPACE(sizeof(struct ucred))];
        struct sockaddr_nl nl = {
                .nl_family = AF_NETLINK,
                .nl_groups = UEVENT_BROADCAST_KERNEL,
        };
        struct msghdr smsg = {
                .msg_iov = &iov,
                .msg_iovlen = 1,
                .msg_control = cred_msg,
                .msg_controllen = sizeof(cred_msg),
                .msg_name = &nl,
                .msg_namelen = sizeof(nl),
        };
        struct cmsghdr *cmsg;
        struct ucred *cred;
        char *payload;
        uint64_t seqnum;
        int r, action;

        assert(m);
        assert(devicep);
        assert(actionp);
        assert(seqnump);

        buflen = recvmsg(m->fd_uevent, &smsg, 0);
        if (buflen < 0)
                return -errno;
        else if (buflen < 32 || (smsg.msg_flags & MSG_TRUNC))
                /* XXX: handle ENOBUFS? */
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

        /* make sure the buf is null terminated */
        if (buf[buflen - 1] != '\0')
                return -EBADMSG;

        /* skip header */
        payload = memchr(buf, '\0', buflen);
        if (!payload)
                return -EBADMSG;
        payload ++;
        buflen -= payload - buf;
        if (buflen <= 0)
                return -EBADMSG;

        /* Pass null-delimited key-value pairs, guaranteed to be
         * null-terminated. */
        r = device_from_nulstr(m, &device, &action, &seqnum, payload, buflen);
        if (r <= 0)
                return r;

        *devicep = device;
        *actionp = action;
        *seqnump = seqnum;
        return 1;
}

int uevent_action_from_string(const char *action) {
        assert(action);

        if (strcmp(action, "change") == 0)
                return UEVENT_ACTION_CHANGE;
        else if (strcmp(action, "add") == 0)
                return UEVENT_ACTION_ADD;
        else if (strcmp(action, "remove") == 0)
                return UEVENT_ACTION_REMOVE;
        else if (strcmp(action, "move") == 0)
                return UEVENT_ACTION_MOVE;
        else if (strcmp(action, "online") == 0)
                return UEVENT_ACTION_ONLINE;
        else if (strcmp(action, "offline") == 0)
                return UEVENT_ACTION_OFFLINE;

        return -EBADMSG;
}

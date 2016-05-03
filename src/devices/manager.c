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

#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>

#include <org.bus1/c-macro.h>
#include <org.bus1/b1-identity.h>

#include "device.h"
#include "manager.h"
#include "module.h"
#include "permissions.h"
#include "sysfs.h"
#include "uevent.h"

Manager *manager_free(Manager *m) {
        CRBNode *n;

        c_close(m->fd_ep);
        c_close(m->fd_uevent);
        c_close(m->fd_signal);
        uevent_subscription_unlink(&m->uevent_subscriptions, m->subscription_settle);
        uevent_subscription_free(m->subscription_settle);

        while ((n = c_rbtree_first(&m->devices))) {
                struct device *device = c_container_of(n, struct device, rb);

                c_rbtree_remove(&m->devices, n);
                device_free(device);
        }

        uevent_subscriptions_destroy(&m->uevent_subscriptions);
        free(m);

        return NULL;
}

int manager_new(Manager **manager) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;
        _c_cleanup_(c_closep) int fd_uevent = -1;
        _c_cleanup_(c_closep) int fd_signal = -1;
        _c_cleanup_(c_closep) int fd_ep = -1;
        _c_cleanup_(c_closep) int devfd = -1;
        _c_cleanup_(c_closep) int sysfd = -1;
        int r;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->fd_uevent = -1;
        m->fd_signal = -1;
        m->fd_ep = -1;
        m->devfd = -1;
        m->sysfd = -1;

        devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (devfd < 0)
                return -errno;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return -errno;

        r = uevent_subscriptions_init(&m->uevent_subscriptions, sysfd);
        if (r < 0)
                return r;

        fd_uevent = uevent_connect();
        if (fd_uevent < 0)
                return fd_uevent;

        m->ep_uevent.events = EPOLLIN;
        m->ep_uevent.data.fd = fd_uevent;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        fd_signal = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
        if (fd_signal < 0)
                return -errno;

        m->ep_signal.events = EPOLLIN;
        m->ep_signal.data.fd = fd_signal;

        fd_ep = epoll_create1(EPOLL_CLOEXEC);
        if (fd_ep < 0)
                return -errno;

        if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_uevent, &m->ep_uevent) < 0 ||
            epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_signal, &m->ep_signal) < 0)
                return -errno;

        m->fd_uevent = fd_uevent;
        fd_uevent = -1;

        m->fd_signal = fd_signal;
        fd_signal = -1;

        m->fd_ep = fd_ep;
        fd_ep = -1;

        m->devfd = devfd;
        devfd = -1;

        m->sysfd = sysfd;
        sysfd = -1;

        *manager = m;
        m = NULL;

       return 0;
}

static int sysfs_cb(int sysfd, const char *devpath, const char *subsystem,
                    const char *devtype, int devfd, const char *devname,
                    const char *modalias, void *userdata) {
        Manager *manager = userdata;
        struct device *device;
        int r;

        assert(manager);

        r = device_add(manager, &device, devpath, subsystem, devtype, devname,
                       modalias);
        if (r < 0)
                return r;

        if (devname) {
                r = permissions_apply(sysfd, devfd, devname, subsystem, devtype);
                if (r < 0)
                        return r;
        }

        if (modalias) {
                r = module_load(modalias);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int settle_cb(void *userdata) {
        Manager *m = userdata;

        assert(m);

        m->subscription_settle = uevent_subscription_free(m->subscription_settle);
        m->settled = true;

        return 0;
}

int manager_enumerate(Manager *m) {
        int r;

        r = sysfs_enumerate(m->sysfd, NULL, NULL, m->devfd, sysfs_cb, m);
        if (r < 0)
                return r;

        r = uevent_sysfs_sync(&m->uevent_subscriptions, m->sysfd, &m->subscription_settle, settle_cb, m);
        if (r < 0)
                return r;

        return 0;
}

static int manager_handle_uevent(Manager *m) {
        struct device *device;
        uint64_t seqnum;
        int r, action;

        r = uevent_receive(m, &device, &action, &seqnum);
        if (r < 0)
                return r;

        if (action == UEVENT_ACTION_ADD) {
                if (device->devname) {
                        r = permissions_apply(m->sysfd, m->devfd, device->devname,
                                              device->subsystem, device->devtype);
                        if (r < 0)
                                return r;
                }

                if (device->modalias) {
                        r = module_load(device->modalias);
                        if (r < 0)
                                return r;
                }
        }

        r = uevent_subscriptions_dispatch(&m->uevent_subscriptions, seqnum);
        if (r < 0)
                return r;

        return 0;
}

int manager_run(Manager *m) {
        struct pollfd pfd = {
                .fd = m->fd_uevent,
                .events = EPOLLIN,
        };
        int r;

        for (;;) {
                int n;
                struct epoll_event ev;

                if (m->uevent_subscriptions.head) {
                        /* If we have subscribers, check if the uevent socket is
                         * idle before waiting. */
                        r = poll(&pfd, 1, 0);
                        if (r < 0) {
                                if (errno != EINTR)
                                        return -errno;
                        }

                        if (pfd.revents & EPOLLIN) {
                                /* This would have been caught below, but let's
                                 * not enter epoll() unneccesarily. */
                                r = manager_handle_uevent(m);
                                if (r < 0)
                                        return r;
                        } else {
                                /* No pending uevents, the next one is
                                 * guaranteed to be higher than all
                                 * subscriptions, so dispatch all now */
                                r = uevent_subscriptions_dispatch_all(&m->uevent_subscriptions);
                                if (r < 0)
                                        return r;
                        }
                }

                n = epoll_wait(m->fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                if (n > 0) {
                        if (ev.data.fd == m->fd_uevent && ev.events & EPOLLIN) {
                                r = manager_handle_uevent(m);
                                if (r < 0)
                                        return r;

                                /* process all pending uevents before any signal */
                                continue;
                        }

                        if (ev.data.fd == m->fd_signal && ev.events & EPOLLIN) {
                                struct signalfd_siginfo fdsi;
                                ssize_t size;

                                size = read(m->fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                                if (size != sizeof(struct signalfd_siginfo))
                                        continue;

                                if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT)
                                        return 0;
                        }
                }
        }
}

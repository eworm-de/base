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
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <org.bus1/b1-identity.h>
#include "device.h"
#include "manager.h"
#include "module.h"
#include "permissions.h"
#include "shared/kmsg.h"
#include "sysfs.h"
#include "uevent.h"

Manager *manager_free(Manager *m) {
        CRBNode *n;
        struct work_item *w;

        c_close(m->fd_ep);
        c_close(m->fd_uevent);
        c_close(m->fd_signal);
        c_close(m->sysfd);
        c_close(m->devfd);
        c_close(m->devicesfd);
        c_close(m->sysbusfd);
        c_close(m->sysclassfd);

        while ((n = c_rbtree_first(&m->devices))) {
                struct device *device = c_container_of(n, struct device, rb);

                device_unlink(device);
                device_free(device);
        }

        while ((n = c_rbtree_first(&m->subsystems))) {
                struct subsystem *subsystem = c_container_of(n, struct subsystem, rb);

                c_rbtree_remove(&m->subsystems, n);
                subsystem_free(subsystem);
        }

        pthread_mutex_lock(&m->worker_lock);
        while ((w = work_item_pop(m)))
                work_item_free(w);
        pthread_mutex_unlock(&m->worker_lock);

        pthread_mutex_destroy(&m->worker_lock);

        uevent_subscription_unlink(&m->uevent_subscriptions, &m->subscription_settle);
        uevent_subscription_destroy(&m->subscription_settle);
        uevent_subscriptions_destroy(&m->uevent_subscriptions);
        if (m->log)
                fclose(m->log);
        free(m);

        return NULL;
}

static size_t manager_get_max_workers(void) {
        cpu_set_t cpu_set;
        int r;

        r = sched_getaffinity(0, sizeof(cpu_set), &cpu_set);
        assert(r >= 0);

        /* Arbitrarily chosen value. This should scale roughly linearly with the
         * number of CPUs, but allow a few extra threads to avoid (especially on
         * small systems) all the threads blocking on hardware access (due to
         * arguably broken modules). */
        return CPU_COUNT(&cpu_set) + 4;
}

int manager_new(Manager **manager) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;
        int r;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->fd_uevent = -1;
        m->fd_signal = -1;
        m->fd_ep = -1;
        m->devfd = -1;
        m->sysfd = -1;
        m->devicesfd = -1;
        m->sysbusfd = -1;
        m->sysclassfd = -1;

        m->log = kmsg(0, NULL);
        if (!m->log)
                return -errno;

        m->devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (m->devfd < 0)
                return -errno;

        m->sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (m->sysfd < 0)
                return -errno;

        m->devicesfd = openat(m->sysfd, "devices", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (m->devicesfd < 0)
                return -errno;

        m->sysbusfd = openat(m->sysfd, "bus", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (m->sysbusfd < 0)
                return -errno;

        m->sysclassfd = openat(m->sysfd, "class", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (m->sysclassfd < 0)
                return -errno;

        r = uevent_subscriptions_init(&m->uevent_subscriptions, m->sysfd);
        if (r < 0)
                return r;

        m->fd_uevent = uevent_connect();
        if (m->fd_uevent < 0)
                return m->fd_uevent;

        m->ep_uevent.events = EPOLLIN;
        m->ep_uevent.data.fd = m->fd_uevent;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        m->fd_signal = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
        if (m->fd_signal < 0)
                return -errno;

        m->ep_signal.events = EPOLLIN;
        m->ep_signal.data.fd = m->fd_signal;

        m->fd_ep = epoll_create1(EPOLL_CLOEXEC);
        if (m->fd_ep < 0)
                return -errno;

        if (epoll_ctl(m->fd_ep, EPOLL_CTL_ADD, m->fd_uevent, &m->ep_uevent) < 0 ||
            epoll_ctl(m->fd_ep, EPOLL_CTL_ADD, m->fd_signal, &m->ep_signal) < 0)
                return -errno;

        pthread_mutex_init(&m->worker_lock, NULL);
        c_list_init(&m->work_items);
        m->n_workers = 0;
        m->max_workers = manager_get_max_workers();

        *manager = m;
        m = NULL;

       return 0;
}

static int sysfs_cb(const char *devpath, const char *subsystem,
                    const char *devtype, const char *devname,
                    const char *modalias, void *userdata) {
        Manager *manager = userdata;
        struct device *device;
        int r;

        assert(manager);

        r = device_add(manager, &device, devpath, subsystem, devtype, devname,
                       modalias);
        if (r < 0)
                return r;

        return 0;
}

static int settle_cb(void *userdata) {
        Manager *m = userdata;
        size_t n_devices = 0;
        int r;

        assert(m);

        uevent_subscription_destroy(&m->subscription_settle);
        m->settled = true;

        for (CRBNode *n = c_rbtree_first(&m->devices); n; n = c_rbnode_next(n)) {
                struct device *device = c_container_of(n, struct device, rb);

                if (device->devname) {
                        r = permissions_apply(m->devfd, device);
                        if (r < 0)
                                return r;
                }

                if (device->modalias) {
                        r = module_load(m, device->modalias);
                        if (r < 0)
                                return r;
                }

                ++ n_devices;
        }

        kmsg(LOG_INFO, "Coldplugged %zu devices.", n_devices);

        return 0;
}

int manager_enumerate(Manager *m) {
        int r;

        kmsg(LOG_INFO, "Coldplug, adjust /dev permissions and load kernel modules for current devices.");

        r = sysfs_enumerate(m->sysfd, sysfs_cb, m);
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
        if (r <= 0)
                return r;

        if (m->settled && action == UEVENT_ACTION_ADD) {
                if (device->devname) {
                        r = permissions_apply(m->devfd, device);
                        if (r < 0)
                                return r;
                }

                if (device->modalias) {
                        r = module_load(m, device->modalias);
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
        int r;

        if (c_list_first(&m->uevent_subscriptions.list)) {
                struct pollfd pfd = {
                        .fd = m->fd_uevent,
                        .events = EPOLLIN,
                };

                /* If we have subscribers, check if the uevent socket is
                 * idle before waiting. */
                for (;;) {
                        r = poll(&pfd, 1, 0);
                        if (r < 0) {
                                if (errno == EINTR)
                                        continue;

                                return -errno;
                        } else
                                break;
                }

                if (pfd.revents & EPOLLIN) {
                        /* Pending uevents, so only dispatch subscriptions from
                         * before any uevents were queued. */
                        r = uevent_subscriptions_dispatch_idle(&m->uevent_subscriptions);
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

        for (;;) {
                int n;
                struct epoll_event ev;

                n = epoll_wait(m->fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                if (n > 0) {
                        if (ev.data.fd == m->fd_uevent && ev.events & EPOLLIN) {
                                /* process all pending uevents */
                                for (;;) {
                                        r = manager_handle_uevent(m);
                                        if (r == -EAGAIN)
                                                break;
                                        else if (r < 0) {
                                                if (r != -EINTR)
                                                        kmsg(LOG_WARNING, "Failed to handle uevent: %s\n", strerror(-r));
                                        }
                                }

                                /* No pending uevents, the next one is
                                 * guaranteed to be higher than all
                                 * subscriptions, so dispatch all now */
                                r = uevent_subscriptions_dispatch_all(&m->uevent_subscriptions);
                                if (r < 0)
                                        return r;
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

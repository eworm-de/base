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

#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>

#include <org.bus1/c-macro.h>
#include <org.bus1/b1-identity.h>

#include "manager.h"
#include "module.h"
#include "permissions.h"
#include "sysfs.h"
#include "uevent.h"

Manager *manager_free(Manager *m) {
        c_close(m->fd_ep);
        c_close(m->fd_uevent);
        c_close(m->fd_signal);
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

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->fd_uevent = -1;
        m->fd_signal = -1;
        m->fd_ep = -1;

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

        devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (devfd < 0)
                return -errno;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
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

static int sysfs_cb(int sysfd, const char *subsystem, const char *devtype,
                    int devfd, const char *devname, const char *modalias,
                    void *userdata) {
        int r;

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

int manager_enumerate(Manager *m) {
        return sysfs_enumerate(m->sysfd, NULL, NULL, m->devfd, sysfs_cb, NULL);
}

static int manager_handle_uevent(Manager *m) {
        _c_cleanup_(c_freep) char *action = NULL;
        _c_cleanup_(c_freep) char *subsystem = NULL;
        _c_cleanup_(c_freep) char *devtype = NULL;
        _c_cleanup_(c_freep) char *devname = NULL;
        _c_cleanup_(c_freep) char *modalias = NULL;
        int r;

        r = uevent_receive(m->fd_uevent, &action, &subsystem, &devtype, &devname, &modalias);
        if (r < 0)
                return r;

        if (action && strcmp(action, "add") == 0) {
                if (devname) {
                        r = permissions_apply(m->sysfd, m->devfd, devname, subsystem, devtype);
                        if (r < 0)
                                return r;
                }

                if (modalias) {
                        r = module_load(modalias);
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}

int manager_run(Manager *m) {
        int r;

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

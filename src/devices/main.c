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

#include <bus1/c-macro.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>

#include "module.h"
#include "permissions.h"
#include "sysfs.h"
#include "uevent.h"

static int sysfs_cb(int sysfd, const char *subsystem, const char *devtype,
                    int devfd, const char *devname, const char *modalias,
                    const void *in, void *out) {
        int r;

        if (devname) {
                r = permissions_apply(devfd, devname, subsystem, devtype);
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

int main(int argc, char **argv) {
        _c_cleanup_(c_closep) int fd_uevent = -1;
        _c_cleanup_(c_closep) int fd_signal = -1;
        _c_cleanup_(c_closep) int fd_ep = -1;
        sigset_t mask;
        struct epoll_event ep_monitor = { .events = EPOLLIN };
        struct epoll_event ep_signal = { .events = EPOLLIN };
        _c_cleanup_(c_closep) int sysfd = -1;
        _c_cleanup_(c_closep) int devfd = -1;
        int r;

        devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (devfd < 0)
                return EXIT_FAILURE;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return EXIT_FAILURE;

        fd_ep = epoll_create1(EPOLL_CLOEXEC);
        if (fd_ep < 0)
                return EXIT_FAILURE;

        fd_uevent = uevent_connect();
        if (fd_uevent < 0)
                return EXIT_FAILURE;
        ep_monitor.data.fd = fd_uevent;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        fd_signal = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
        if (fd_signal < 0)
                return EXIT_FAILURE;
        ep_signal.data.fd = fd_signal;

        if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_uevent, &ep_monitor) < 0 ||
            epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_signal, &ep_signal) < 0)
                return EXIT_FAILURE;

        r = sysfs_enumerate(sysfd, NULL, NULL, devfd, sysfs_cb, NULL, NULL);
        if (r < 0)
                return EXIT_FAILURE;

        for (;;) {
                int n;
                struct epoll_event ev;

                n = epoll_wait(fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return EXIT_FAILURE;
                }

                if (ev.data.fd == fd_uevent && ev.events & EPOLLIN) {
                        _c_cleanup_(c_freep) char *action = NULL;
                        _c_cleanup_(c_freep) char *subsystem = NULL;
                        _c_cleanup_(c_freep) char *devtype = NULL;
                        _c_cleanup_(c_freep) char *devname = NULL;
                        _c_cleanup_(c_freep) char *modalias = NULL;

                        r = uevent_receive(fd_uevent, &action, &subsystem, &devtype, &devname, &modalias);
                        if (r < 0)
                                return EXIT_FAILURE;

                        if (action && strcmp(action, "add") == 0) {
                                if (devname) {
                                        r = permissions_apply(devfd, devname, subsystem, devtype);
                                        if (r < 0)
                                                return EXIT_FAILURE;
                                }

                                if (modalias) {
                                        r = module_load(modalias);
                                        if (r < 0)
                                                return EXIT_FAILURE;
                                }
                        }

                        continue;
                }

                if (ev.data.fd == fd_signal && ev.events & EPOLLIN) {
                        struct signalfd_siginfo fdsi;
                        ssize_t size;

                        size = read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT)
                                break;
                }
        }

        return EXIT_SUCCESS;
}

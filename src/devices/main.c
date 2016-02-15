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
#include <sys/capability.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>

#include <org.bus1/c-macro.h>
#include <org.bus1/b1-identity.h>
#include "shared/kmsg.h"

#include "module.h"
#include "module.h"
#include "permissions.h"
#include "sysfs.h"
#include "uevent.h"

static int sysfs_cb(int sysfd, const char *subsystem, const char *devtype,
                    int devfd, const char *devname, const char *modalias,
                    void *userdata) {
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

C_DEFINE_CLEANUP(cap_t, cap_free);

static int privileges_drop(uid_t uid, const cap_value_t *caps, unsigned int n_caps) {
        _c_cleanup_(cap_freep) cap_t cap = NULL;
        static const cap_value_t setpcap = CAP_SETPCAP;
        cap_value_t c;

        assert(uid > 0);

        if (setresgid(uid, uid, uid) < 0)
                return -errno;

        /* Retain caps across setresuid(). */
        if (prctl(PR_SET_KEEPCAPS, 1) < 0)
                return -errno;

        if (setresuid(uid, uid, uid) < 0)
                return -errno;

        if (prctl(PR_SET_KEEPCAPS, 0) < 0)
                return -errno;

        cap = cap_get_proc();
        if (!cap)
                return -errno;

        /* Gain CAP_SETPCAP to be able to drop the capability bounding set. */
        if (cap_set_flag(cap, CAP_EFFECTIVE, 1, &setpcap, CAP_SET) < 0)
                return -errno;

        if (cap_set_proc(cap) < 0)
                return -errno;

        for (c = 0;; c++)
                if (cap_drop_bound(c) < 0)
                        break;

        if (cap_clear(cap) < 0)
                return -errno;

        if (cap_set_flag(cap, CAP_EFFECTIVE, n_caps, caps, CAP_SET) < 0 ||
            cap_set_flag(cap, CAP_PERMITTED, n_caps, caps, CAP_SET) < 0)
                return -errno;

        if (cap_set_proc(cap) < 0)
                return -errno;

        return 0;
}

typedef struct {
        int fd_uevent;
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_uevent;
        struct epoll_event ep_signal;
} Manager;

static Manager *manager_free(Manager *m) {
        c_close(m->fd_ep);
        c_close(m->fd_uevent);
        c_close(m->fd_signal);
        free(m);

        return NULL;
}

C_DEFINE_CLEANUP(Manager *, manager_free);
static int manager_new(Manager **manager) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;
        _c_cleanup_(c_closep) int fd_uevent = -1;
        _c_cleanup_(c_closep) int fd_signal = -1;
        _c_cleanup_(c_closep) int fd_ep = -1;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

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
        fd_signal = -1;

        m->fd_signal = fd_signal;
        fd_signal = -1;

        m->fd_ep = fd_ep;
        fd_ep = -1;

        *manager = m;
        m = NULL;

       return 0;
}

int main(int argc, char **argv) {
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
        _c_cleanup_(c_closep) int sysfd = -1;
        _c_cleanup_(c_closep) int devfd = -1;
        cap_value_t caps[] = {
                CAP_CHOWN,
                CAP_FOWNER,
                CAP_DAC_OVERRIDE,
                CAP_SYS_MODULE,
        };
        int r;

        log = kmsg(0, NULL);
        if (!log)
                return EXIT_FAILURE;

        devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (devfd < 0)
                return EXIT_FAILURE;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return EXIT_FAILURE;

        if (manager_new(&m) < 0)
                return EXIT_FAILURE;

        r = privileges_drop(BUS1_IDENTITY_DEVICES, caps, C_ARRAY_SIZE(caps));
        if (r < 0)
                return EXIT_FAILURE;

        kmsg(LOG_INFO, "Coldplug, adjust /dev permissions and load kernel modules for current devices.");
        r = sysfs_enumerate(sysfd, NULL, NULL, devfd, sysfs_cb, NULL);
        if (r < 0)
                return EXIT_FAILURE;

        for (;;) {
                int n;
                struct epoll_event ev;

                n = epoll_wait(m->fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return EXIT_FAILURE;
                }

                if (n > 0) {
                        if (ev.data.fd == m->fd_uevent && ev.events & EPOLLIN) {
                                _c_cleanup_(c_freep) char *action = NULL;
                                _c_cleanup_(c_freep) char *subsystem = NULL;
                                _c_cleanup_(c_freep) char *devtype = NULL;
                                _c_cleanup_(c_freep) char *devname = NULL;
                                _c_cleanup_(c_freep) char *modalias = NULL;

                                r = uevent_receive(m->fd_uevent, &action, &subsystem, &devtype, &devname, &modalias);
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

                        if (ev.data.fd == m->fd_signal && ev.events & EPOLLIN) {
                                struct signalfd_siginfo fdsi;
                                ssize_t size;

                                size = read(m->fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                                if (size != sizeof(struct signalfd_siginfo))
                                        continue;

                                if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT)
                                        break;
                        }
                }
        }

        return EXIT_SUCCESS;
}

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

#include <bus1/b1-platform.h>
#include <bus1/c-macro.h>
#include <bus1/c-shared.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include "kmsg.h"
#include "service.h"
#include "tmpfs-root.h"
#include "util.h"

typedef struct Manager Manager;

struct Manager {
        Service devices;

        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;
};

static Manager *manager_free(Manager *m) {
        c_close(m->fd_ep);
        c_close(m->fd_signal);
        free(m);
        return NULL;
}

C_DEFINE_CLEANUP(Manager *, manager_free);
static int manager_new(Manager **manager) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->devices.pid = -1;
        m->devices.name = "org.bus1.devices";

        m->ep_signal.events = EPOLLIN;
        m->fd_signal = -1;
        m->fd_ep = -1;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        m->fd_ep = epoll_create1(EPOLL_CLOEXEC);
        if (m->fd_ep < 0)
                return -errno;

        m->fd_signal = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
        if (m->fd_signal < 0)
                return -errno;
        m->ep_signal.data.fd = m->fd_signal;

        if (epoll_ctl(m->fd_ep, EPOLL_CTL_ADD, m->fd_signal, &m->ep_signal) < 0)
                return -errno;

       *manager = m;
       m = NULL;

       return 0;
}

static int manager_start_services(Manager *m, pid_t died_pid) {
        if (m->devices.pid > 0 && died_pid == m->devices.pid)
                return -EIO;

        if (m->devices.pid < 0) {
                pid_t pid;

                pid = service_activate(&m->devices);
                if (pid < 0)
                        return pid;

                m->devices.pid = pid;
        }

        return 0;
}

static int manager_stop_services(Manager *m) {
        if (m->devices.pid > 0) {
                if (kill(m->devices.pid, SIGTERM) < 0)
                        return -errno;

                m->devices.pid = -1;
        }

        return 0;
}

static int manager_run(Manager *m) {
        bool exit = false;
        int r;

        while (!exit) {
                int n;
                struct epoll_event ev;

                n = epoll_wait(m->fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                if (ev.data.fd == m->fd_signal && ev.events & EPOLLIN) {
                        struct signalfd_siginfo fdsi;
                        ssize_t size;

                        size = read(m->fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        switch (fdsi.ssi_signo) {
                        case SIGTERM:
                        case SIGINT:
                                exit = true;
                                break;

                        case SIGCHLD: {
                                pid_t pid = -1;

                                r = child_reap(&pid);
                                if (r < 0)
                                        return r;

                                if (pid < 0)
                                        break;

                                r = manager_start_services(m, pid);
                                if (r < 0)
                                        return r;

                                break;
                        }

                        default:
                                return -EINVAL;
                        }
                }
        }

        return 0;
}

int main(int argc, char **argv) {
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;

        log = kmsg(0, NULL);
        if (!log)
                return EXIT_FAILURE;

        if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
                return EXIT_FAILURE;

        if (mkdir("/var/bus1", 0755) < 0 && errno != EEXIST)
                return -errno;

        if (manager_new(&m) < 0)
                return EXIT_FAILURE;

        if (manager_start_services(m, -1) < 0)
                return EXIT_FAILURE;

        if (manager_run(m) < 0)
                return EXIT_FAILURE;

        if (manager_stop_services(m) < 0)
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}

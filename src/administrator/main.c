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
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <org.bus1/b1-platform.h>
#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include "shared/kmsg.h"
#include "shared/process.h"
#include "shared/kernel-cmdline.h"

static pid_t getty_start(const char *device) {
        const char *argv[] = {
                "/usr/bin/agetty",
                "--nohostname",
                "--keep-baud",
                "115200,38400,9600",
                device,
                "linux",
                NULL
        };
        pid_t p;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                if (setsid() < 0)
                        return -errno;

                execve(argv[0], (char **)argv, NULL);
                return -errno;
        }

        return p;
}

typedef struct {
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        char *serial_device;

        pid_t login_pid;        /* console login */
        pid_t serial_pid;       /* serial console login */
} Manager;

static Manager *manager_free(Manager *m) {
        c_close(m->fd_ep);
        c_close(m->fd_signal);
        free(m->serial_device);
        free(m);
        return NULL;
}

C_DEFINE_CLEANUP(Manager *, manager_free);
static int manager_new(Manager **managerp) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;
        _c_cleanup_(c_closep) int fd_signal = -1;
        _c_cleanup_(c_closep) int fd_ep = -1;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->login_pid = -1;
        m->serial_pid = -1;

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

        if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_signal, &m->ep_signal) < 0)
                return -errno;

        m->fd_signal = fd_signal;
        fd_signal = -1;

        m->fd_ep = fd_ep;
        fd_ep = -1;

        *managerp = m;
        m = NULL;

       return 0;
}

static int manager_start_getty(Manager *m, pid_t died_pid) {
        if (access("/dev/tty1", F_OK) >= 0 && (m->login_pid < 0 || died_pid == m->login_pid)) {
                pid_t pid;

                kmsg(LOG_INFO, "Starting getty on tty1.");
                pid = getty_start("tty1");
                if (pid < 0)
                        return pid;

                m->login_pid = pid;
        }

        if (m->serial_device && (m->serial_pid < 0 || died_pid == m->serial_pid)) {
                pid_t pid;

                kmsg(LOG_INFO, "Starting getty on %s.", m->serial_device);
                pid = getty_start(m->serial_device);
                if (pid < 0)
                        return pid;

                m->serial_pid = pid;
        }

       return 0;
}

static int manager_stop_getty(Manager *m) {
        if (m->serial_pid > 0) {
                if (kill(m->serial_pid, SIGTERM) < 0)
                        return -errno;

                m->serial_pid = -1;
        }

        if (m->login_pid > 0) {
                if (kill(m->login_pid, SIGTERM) < 0)
                        return -errno;

                m->login_pid = -1;
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

                if (n > 0) {
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
                                        pid_t pid;

                                        r = process_reap_children(&pid);
                                        if (r < 0)
                                                return r;

                                        if (pid < 0)
                                                break;

                                        r = manager_start_getty(m, pid);
                                        if (r < 0)
                                                return r;

                                        break;
                                }

                                default:
                                        return -EINVAL;
                                }
                        }
                }
        }

        return 0;
}

int main(int argc, char **argv) {
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
        int r;

        r = manager_new(&m);
        if (r < 0)
                goto fail;

        /* Serial console getty. */
        r = kernel_cmdline_option("console", &m->serial_device);
        if (r < 0)
                goto fail;

        r = manager_start_getty(m, -1);
        if (r < 0)
                goto fail;

        r = manager_run(m);
        if (r < 0)
                goto fail;

        r = manager_stop_getty(m);
        if (r < 0)
                goto fail;

        return EXIT_SUCCESS;

fail:
        kmsg(LOG_EMERG, "Unrecoverable failure. Exiting: %s.", strerror(-r));
        return EXIT_FAILURE;
}

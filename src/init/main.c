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
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "util.h"

static int system_reboot(int cmd) {
        unsigned int i;

        for (i = 0; i < 10; i++) {
                if (mount(NULL, "/bus1", NULL, MS_REMOUNT|MS_RDONLY, NULL) >= 0)
                        break;

                printf("killing all processes\n");
                kill(-1, SIGKILL);
                sleep(1);
        }

        sync();

        return reboot(cmd);
}

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

static int child_reap(pid_t *p) {
        bool reap = false;

        for (;;) {
                siginfo_t si = {};

                if (waitid(P_ALL, 0, &si, WEXITED|WNOHANG) < 0) {
                        if (errno == ECHILD)
                                break;

                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                reap = true;

                if (p) {
                        *p = si.si_pid;
                        break;
                }
        }

        return reap;
}

typedef struct Manager Manager;

struct Manager {
        char *release;

        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        /* org.bus1.devices */
        pid_t devices_pid;

        /* console login */
        pid_t login_pid;

        /* serial console login */
        pid_t serial_pid;
        char *serial_device;
};

static Manager *manager_free(Manager *m) {
        free(m->release);
        c_close(m->fd_ep);
        c_close(m->fd_signal);
        free(m->serial_device);
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

        m->ep_signal.events = EPOLLIN;
        m->fd_signal = -1;
        m->fd_ep = -1;

        m->devices_pid = -1;
        m->login_pid = -1;
        m->serial_pid = -1;

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
        if (m->devices_pid > 0 && died_pid == m->devices_pid)
                return -EIO;

        if (m->devices_pid < 0) {
                pid_t pid;

                pid = service_start("/usr/bin/org.bus1.devices");
                if (pid < 0)
                        return pid;

                m->devices_pid = pid;
        }

        if (access("/dev/tty1", F_OK) >= 0 && (m->login_pid < 0 || died_pid == m->login_pid)) {
                pid_t pid;

                pid = getty_start("tty1");
                if (pid < 0)
                        return pid;

                m->login_pid = pid;
        }

        if (m->serial_device && (m->serial_pid < 0 || died_pid == m->serial_pid)) {
                pid_t pid;

                pid = getty_start(m->serial_device);
                if (pid < 0)
                        return pid;

                m->serial_pid = pid;
        }

       return 0;
}

static int manager_stop_services(Manager *m) {
        if (m->devices_pid > 0) {
                if (kill(m->devices_pid, SIGTERM) < 0)
                        return -errno;

                m->devices_pid = -1;
        }

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

                                r = child_reap(&pid);
                                if (r < 0)
                                        return r;

                                if (r == 0)
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
        _c_cleanup_(manager_freep) Manager *m = NULL;

        /* clean up zombies from the initrd */
        if (child_reap(NULL) < 0)
                return EXIT_FAILURE;

        if (manager_new(&m) < 0)
                return EXIT_FAILURE;

        if (bus1_read_release(&m->release) < 0)
                return EXIT_FAILURE;

        /* serial console getty */
        if (kernel_cmdline_option("console", &m->serial_device) < 0)
                return EXIT_FAILURE;

        if (manager_start_services(m, -1) < 0)
                return EXIT_FAILURE;

        if (manager_run(m) < 0)
                return EXIT_FAILURE;

        if (manager_stop_services(m) < 0)
                return EXIT_FAILURE;

        system_reboot(RB_POWER_OFF);
        return EXIT_FAILURE;
}

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
#include <bus1/c-shared.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "kmsg.h"
#include "sysctl.h"
#include "util.h"

/* mount all remaining device filesystem read-only */
static int remount_filesystems(void) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        int r;

        f = fopen("/proc/self/mountinfo", "re");
        if (!f)
                return -errno;

        for (;;) {
                _c_cleanup_(c_freep) char *path = NULL, *source = NULL, *options = NULL;

                r = fscanf(f,
                           "%*s "       /* mount id */
                           "%*s "       /* parent id */
                           "%*s "       /* dev_t */
                           "%*s "       /* root */
                           "%ms "       /* mount point */
                           "%*s"        /* mount flags */
                           "%*[^-]"     /* optional fields */
                           "- "         /* separator */
                           "%*s "       /* file system type */
                           "%ms"        /* mount source */
                           "%ms"        /* mount options */
                           "%*[^\n]",
                           &path, &source, &options);
                if (r == EOF)
                        break;
                if (r != 3)
                        continue;

                if (!c_str_prefix(source, "/dev/"))
                        continue;

                mount(NULL, path, NULL, MS_REMOUNT|MS_RDONLY, NULL);

        }

        return 0;
}

static int system_reboot(int cmd) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        unsigned int i;

        for (i = 0; i < 10; i++) {
                if (mount(NULL, "/bus1", NULL, MS_REMOUNT|MS_RDONLY, NULL) >= 0)
                        break;

                kmsg(LOG_INFO, "Filesystem busy. Sending all process the KILL signal: %m\n");
                kill(-1, SIGKILL);
                sleep(1);
        }

        remount_filesystems();
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

typedef struct Manager Manager;

struct Manager {
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        /* org.bus1.activator */
        pid_t activator_pid;

        /* console login */
        pid_t login_pid;

        /* serial console login */
        pid_t serial_pid;
        char *serial_device;
};

static Manager *manager_free(Manager *m) {
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

        m->activator_pid = -1;
        m->login_pid = -1;
        m->serial_pid = -1;

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
        if (m->activator_pid > 0 && died_pid == m->activator_pid)
                return -EIO;

        if (m->activator_pid < 0) {
                pid_t pid;

                kmsg(LOG_INFO, "Starting org.bus1.activator.");
                pid = service_start("/usr/bin/org.bus1.activator");
                if (pid < 0)
                        return pid;

                m->activator_pid = pid;
        }

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

static int manager_stop_services(Manager *m) {
        if (m->activator_pid > 0) {
                if (kill(m->activator_pid, SIGTERM) < 0)
                        return -errno;

                m->activator_pid = -1;
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

static void dump_process(int sig) {
        pid_t pid;

        /* Fork a child process which is able to produce a core dump. */
        pid = fork();
        if (pid < 0) {
                kmsg(LOG_EMERG, "Unable to start dump process: %m");
                return;
        }

        if (pid == 0) {
                struct sigaction sa = {
                        .sa_handler = SIG_DFL,
                };

                /* Set the default handler for our signal. */
                sigaction(sig, &sa, NULL);

                /* Handle this signal in our dump process. */
                kmsg(LOG_EMERG, "Cought signal %d (%s). Dump process %d started.", sig, strsignal(sig), getpid());
                raise(sig);

                _exit(1);
        }

        /* Wait for the dump process. */
        for (;;) {
                if (waitid(P_PID, pid, NULL, WEXITED) < 0 && errno == EINTR)
                        continue;

                break;
        }

        kmsg(LOG_EMERG, "Unrecoverable failure. System rebooting.");
        sleep(15);
        system_reboot(RB_AUTOBOOT);
}

int main(int argc, char **argv) {
        static const struct sigaction sa = {
                .sa_handler = dump_process,
                .sa_flags = SA_NODEFER,
        };
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
        _c_cleanup_(c_freep) char *release = NULL;
        int r;

        /* install dump process handler */
        if (sigaction(SIGSEGV, &sa, NULL) < 0 ||
            sigaction(SIGILL, &sa, NULL) < 0 ||
            sigaction(SIGFPE, &sa, NULL) < 0 ||
            sigaction(SIGBUS, &sa, NULL) < 0 ||
            sigaction(SIGQUIT, &sa, NULL) < 0 ||
            sigaction(SIGABRT, &sa, NULL) < 0) {
                r = -errno;
                goto fail;
        }

        /* clean up zombies from the initrd */
        if (child_reap(NULL) < 0) {
                r = -errno;
                goto fail;
        }

        r = sysctl_apply();
        if (r < 0)
                goto fail;

        r = manager_new(&m);
        if (r < 0)
                goto fail;

        r = bus1_read_release(&release);
        if (r < 0)
                goto fail;

        log = kmsg(LOG_INFO, "Release %s.", release);
        if (!log) {
                r = -errno;
                goto fail;
        }

        /* serial console getty */
        r = kernel_cmdline_option("console", &m->serial_device);
        if (r < 0)
                goto fail;

        r = manager_start_services(m, -1);
        if (r < 0)
                goto fail;

        r = manager_run(m);
        if (r < 0)
                goto fail;

        r = manager_stop_services(m);
        if (r < 0)
                goto fail;

        kmsg(LOG_INFO, "Shutting down.");
        system_reboot(RB_POWER_OFF);
        r = -errno;
        kmsg(LOG_EMERG, "Failed to execute reboot().");

fail:
        kmsg(LOG_EMERG, "Unrecoverable failure. System rebooting: %s.", strerror(-r));
        sleep(5);
        system_reboot(RB_AUTOBOOT);

        return EXIT_FAILURE;
}

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
#include <c-string.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <org.bus1/b1-platform.h>
#include "shared/file.h"
#include "shared/kmsg.h"
#include "shared/mount.h"
#include "shared/process.h"
#include "shared/kernel-cmdline.h"

static bool string_has_option(const char *options, const char *option) {
        const char *s;
        size_t l;

        l = strlen(option);
        s = options;
        for (;;) {
                s = strstr(s, option);
                if (!s)
                        return false;

                if ((s == options || s[-1] == ',') && (s[l] == '\0' || s[l] == ','))
                        return true;

                s += l;
        };

        return false;
}

/* Mount all remaining device filesystem read-only. */
static int remount_filesystems(void) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        int r;

        f = fopen("/proc/self/mountinfo", "re");
        if (!f)
                return -errno;

        for (;;) {
                _c_cleanup_(c_freep) char *root = NULL, *path = NULL, *source = NULL, *options = NULL;

                r = fscanf(f,
                           "%*s "       /* mount id */
                           "%*s "       /* parent id */
                           "%*s "       /* dev_t */
                           "%ms "       /* root */
                           "%ms "       /* mount point */
                           "%*s"        /* mount flags */
                           "%*[^-]"     /* optional fields */
                           "- "         /* separator */
                           "%*s "       /* file system type */
                           "%ms"        /* mount source */
                           "%ms"        /* mount options */
                           "%*[^\n]",
                           &root, &path, &source, &options);
                if (r == EOF)
                        break;
                if (r != 4)
                        continue;

                /* Simple mounts only. */
                if (strcmp(root, "/") != 0)
                        continue;

                /* Devices only. */
                if (!c_string_prefix(source, "/dev/"))
                        continue;

                if (string_has_option(options, "ro"))
                        continue;

                if (mount(NULL, path, NULL, MS_REMOUNT|MS_RDONLY, NULL) >= 0)
                        kmsg(LOG_INFO, "Mounted filesystem at %s (%s) as read-only.", path, source);
                else
                        kmsg(LOG_WARNING, "Unable to mount filesystem at %s (%s) as read-only.", path, source);

        }

        return 0;
}

static int system_reboot(int cmd) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;

        for (size_t i = 0; i < 10; i++) {
                if (mount(NULL, "/var", NULL, MS_REMOUNT|MS_RDONLY, NULL) >= 0) {
                        kmsg(LOG_INFO, "Mounted filesystem at /var as read-only.");
                        break;
                }

                kmsg(LOG_WARNING, "Filesystem at /var is busy. Sending all processes the KILL signal: %m\n");
                kill(-1, SIGKILL);
                sleep(1);
        }

        remount_filesystems();
        sync();

        return reboot(cmd);
}

typedef struct {
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        char *release;

        char *loader_dir;               /* Boot loader directory in /boot. */
        char *loader;                   /* Boot loader file name. */
        char *loader_rename;            /* Boot loader file name after rename. */
        int boot_counter;               /* Boot candidate counter for an updated system. */

        pid_t activator_pid;            /* org.bus1.activator */
        pid_t administrator_pid;        /* org.bus1.administrator */

        char *administrator;            /* org.bus1.administrator binary */
} Manager;

static Manager *manager_free(Manager *m) {
        free(m->administrator);
        free(m->release);
        free(m->loader_dir);
        free(m->loader);
        free(m->loader_rename);
        c_close(m->fd_ep);
        c_close(m->fd_signal);
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

        m->boot_counter = -1;

        m->activator_pid = -1;
        m->administrator_pid = -1;

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

static int manager_start_services(Manager *m, pid_t died_pid) {
        if (m->administrator_pid > 0 && died_pid == m->administrator_pid)
                return -EIO;

        if (m->administrator_pid < 0) {
                pid_t pid;

                kmsg(LOG_INFO, "Starting org.bus1.administrator.");
                pid = process_start_program(m->administrator);
                if (pid < 0)
                        return pid;

                m->administrator_pid = pid;
        }

        if (m->activator_pid > 0 && died_pid == m->activator_pid)
                return -EIO;

        if (m->activator_pid < 0) {
                pid_t pid;

                kmsg(LOG_INFO, "Starting org.bus1.activator.");
                pid = process_start_program("/usr/bin/org.bus1.activator");
                if (pid < 0)
                        return pid;

                m->activator_pid = pid;
        }

       return 0;
}

static int manager_stop_services(Manager *m) {
        if (m->activator_pid > 0) {
                if (kill(m->activator_pid, SIGTERM) < 0)
                        return -errno;

                m->activator_pid = -1;
        }

        if (m->administrator_pid > 0) {
                if (kill(m->administrator_pid, SIGTERM) < 0)
                        return -errno;

                m->administrator_pid = -1;
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
                static const struct sigaction sa = {
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
        waitid(P_PID, pid, NULL, WEXITED);

        kmsg(LOG_EMERG, "Unrecoverable failure. System rebooting.");
        sleep(15);
        system_reboot(RB_AUTOBOOT);
}

static int cmdline_parse_loader(Manager *m) {
        _c_cleanup_(c_freep) char *loader_dir = NULL;
        _c_cleanup_(c_freep) char *loader = NULL;
        _c_cleanup_(c_freep) char *loader_rename = NULL;
        size_t release_len;
        char *s;
        int counter = -1;
        int r;

        r = kernel_cmdline_option("loader", &loader_dir);
        if (r < 0)
                return r;

        if (!loader_dir)
                return 0;

        if (loader_dir[0] != '/')
                return -EINVAL;

        s = strrchr(loader_dir, '/');
        if (!s)
                return -EINVAL;

        *s = '\0';

        loader = strdup(s + 1);
        if (!loader)
                return -ENOMEM;

        if (loader[0] == '\0')
                return -EINVAL;

        release_len = strlen(m->release);
        if (strncasecmp(loader, m->release, release_len) != 0)
                return -EINVAL;

        s = loader + release_len;
        if (strncasecmp(s, "-boot", 5) == 0) {
                s += 5;
                if (*s < '0' || *s > '9')
                        return -EINVAL;

                counter = *s - '0';
                s++;

                if (asprintf(&loader_rename, "%s%s", m->release, s) < 0)
                        return -ENOMEM;
        }

        m->loader_dir = loader_dir;
        loader_dir = NULL;
        m->loader = loader;
        loader = NULL;
        m->loader_rename = loader_rename;
        loader_rename = NULL;
        m->boot_counter = counter;

        return 0;
}

static int manager_parse_kernel_cmdline(Manager *m) {
        int r;

        r = cmdline_parse_loader(m);
        if (r < 0)
                return r;

        r = kernel_cmdline_option("administrator", &m->administrator);
        if (r < 0)
                return r;

        if (!m->administrator) {
                m->administrator = strdup("/usr/bin/org.bus1.administrator");
                if (!m->administrator)
                        return -ENOMEM;
        }

        return 0;
}

static int loader_reset_boot_counter(Manager *m) {
        _c_cleanup_(c_freep) char *loader_dir = NULL;
        _c_cleanup_(c_freep) char *loader = NULL;
        _c_cleanup_(c_closep) int dfd = -1;

        kmsg(LOG_INFO, "Successfully booted with loader %s. Removing boot counter.", m->loader);

        if (asprintf(&loader_dir, "/boot/%s", m->loader_dir) < 0)
                return -ENOMEM;

        dfd = openat(AT_FDCWD, loader_dir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return -errno;

        if (mount_boot(NULL, NULL, MS_REMOUNT) < 0)
                return -errno;

        if (renameat(dfd, m->loader, dfd, m->loader_rename) < 0)
                return -errno;

        if (mount_boot(NULL, NULL, MS_REMOUNT|MS_RDONLY) < 0)
                return -errno;

        return 0;
}

int main(int argc, char **argv) {
        static const struct sigaction sa = {
                .sa_handler = dump_process,
                .sa_flags = SA_NODEFER,
        };
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
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
        if (process_reap_children(NULL) < 0) {
                r = -errno;
                goto fail;
        }

        r = manager_new(&m);
        if (r < 0)
                goto fail;

        r = file_read_line("/usr/lib/org.bus1/release", &m->release);
        if (r < 0)
                goto fail;

        log = kmsg(LOG_INFO, "Release %s.", m->release);
        if (!log) {
                r = -errno;
                goto fail;
        }

        r = manager_parse_kernel_cmdline(m);
        if (r < 0)
                goto fail;

        r = manager_start_services(m, -1);
        if (r < 0)
                goto fail;

        if (m->boot_counter >= 0 && loader_reset_boot_counter(m) < 0)
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

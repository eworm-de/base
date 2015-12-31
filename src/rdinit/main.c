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

#include <blkid/blkid.h>
#include <bus1/b1-platform.h>
#include <bus1/c-macro.h>
#include <bus1/c-shared.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libkmod.h>
#include <linux/loop.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

//FIXME: use bus
#include "../devices/sysfs.h"
#include "kmsg.h"
#include "tmpfs-root.h"
#include "util.h"

typedef struct Manager Manager;

struct Manager {
        char *disk;
        char *partition;

        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        /* org.bus1.activator */
        pid_t activator_pid;
};

static Manager *manager_free(Manager *m) {
        free(m->disk);
        free(m->partition);
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

        m->ep_signal.events = EPOLLIN;
        m->fd_signal = -1;
        m->fd_ep = -1;

        m->activator_pid = -1;

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

        return 0;
}

static int manager_stop_services(Manager *m) {
        if (m->activator_pid > 0) {
                if (kill(m->activator_pid, SIGTERM) < 0)
                        return -errno;

                m->activator_pid = -1;
        }

        return 0;
}

static int kernel_filesystem_mount(bool early) {
        static const struct mountpoint {
                const char *what;
                const char *where;
                const char *type;
                const char *options;
                unsigned long flags;
        } mount_early[] = {
                { "devtmpfs", "/dev",         "devtmpfs", "mode=755",  MS_NOSUID|MS_NOEXEC|MS_STRICTATIME },
                { "devpts",   "/dev/pts",     "devpts",   "mode=620",  MS_NOSUID|MS_NOEXEC },
                { "proc",     "/proc",        "proc",     "hidepid=2", MS_NOSUID|MS_NOEXEC|MS_NODEV },
                { "sysfs",    "/sys",         "sysfs",    NULL,        MS_NOSUID|MS_NOEXEC|MS_NODEV },
        }, mount_late[] = {
                { "bus1fs",   "/sys/fs/bus1", "bus1fs",   NULL,        MS_NOSUID|MS_NOEXEC|MS_NODEV },
        };

        if (early) {
                unsigned int i;

                for (i = 0; i < C_ARRAY_SIZE(mount_early); i++) {
                        if (mkdir(mount_early[i].where, 0700) < 0 && errno != EEXIST)
                                return -errno;

                        if (mount(mount_early[i].what, mount_early[i].where,
                                  mount_early[i].type, mount_early[i].flags,
                                  mount_early[i].options) < 0 && errno != EBUSY)
                                return -errno;
                }
        } else {
                unsigned int i;

                for (i = 0; i < C_ARRAY_SIZE(mount_late); i++) {
                        if (mount(mount_late[i].what, mount_late[i].where,
                                  mount_late[i].type, mount_late[i].flags,
                                  mount_late[i].options) < 0 && errno != EBUSY)
                                return -errno;
                }
        }

        return 0;
}

static void module_log(void *data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {}

static int modules_load(void) {
        static const struct module {
                const char *name;
                const char *path;
        } modules[] = {
                { "bus1", "/sys/module/bus1" },
                { "loop", "/sys/module/loop" },
        };
        unsigned int i;
        struct kmod_ctx *ctx = NULL;
        int r = 0;

        for (i = 0; i < C_ARRAY_SIZE(modules); i++) {
                struct kmod_module *mod;

                if (modules[i].path && access(modules[i].path, F_OK) >= 0)
                        continue;

                if (!ctx) {
                        ctx = kmod_new(NULL, NULL);
                        if (!ctx) {
                                r = -ENOMEM;
                                goto err;
                        }

                        kmod_set_log_fn(ctx, module_log, NULL);
                        r = kmod_load_resources(ctx);
                        if (r < 0)
                                goto err;
                }

                r = kmod_module_new_from_name(ctx, modules[i].name, &mod);
                if (r < 0)
                        continue;

                kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST|KMOD_PROBE_IGNORE_COMMAND,
                                                NULL, NULL, NULL, NULL);
                kmod_module_unref(mod);
        }

err:
        kmod_unref(ctx);
        return r;
}

C_DEFINE_CLEANUP(blkid_probe, blkid_free_probe);

static int disk_probe(const char *disk, Manager *m) {
        const char *s;
        _c_cleanup_(blkid_free_probep) blkid_probe b = NULL;
        blkid_partlist pl;
        int i;
        int r;

        assert(!m->partition);

        b = blkid_new_probe_from_filename(disk);
        if (!b)
                return -EIO;

        blkid_probe_enable_partitions(b, 1);
        blkid_probe_set_partitions_flags(b, BLKID_PARTS_ENTRY_DETAILS);
        r = blkid_do_safeprobe(b);
        if (r < 0)
                return r;

        r = blkid_probe_lookup_value(b, "PTTYPE", &s, NULL);
        if (r < 0)
                return r;

        if (strcmp(s, "gpt") != 0)
                return -ENODEV;

        r = blkid_probe_lookup_value(b, "PTUUID", &s, NULL);
        if (r < 0)
                return r;

        if (strcmp(s, m->disk) != 0)
                return -ENODEV;

        kmsg(LOG_INFO, "Found disk %s (%s).", disk, m->disk);

        pl = blkid_probe_get_partitions(b);
        if(!pl)
                return -EIO;

        for (i = 0; i < blkid_partlist_numof_partitions(pl); i++) {
                blkid_partition p;

                p = blkid_partlist_get_partition(pl, i);
                s = blkid_partition_get_type_string(p);
                if (!s)
                        continue;

                if (strcmp(s, C_STRINGIFY(BUS1_GPT_PARTITION_TYPE_UUID)) != 0)
                        continue;

                if (isdigit(disk[strlen(disk) - 1])) {
                        if (asprintf(&m->partition, "%sp%d", disk, blkid_partition_get_partno(p)) < 0)
                                return -ENOMEM;
                } else {
                        if (asprintf(&m->partition, "%s%d", disk, blkid_partition_get_partno(p)) < 0)
                                return -ENOMEM;
                }

                kmsg(LOG_INFO, "Found partition type bus1 at %s.", m->partition);

                return 0;
        }

        return -ENODEV;
}

static int partition_probe(const char *partition, char **fstype) {
        _c_cleanup_(blkid_free_probep) blkid_probe b = NULL;
        const char *s;
        int r;

        b = blkid_new_probe_from_filename(partition);
        if (!b)
                return -EIO;

        r = blkid_do_safeprobe(b);
        if (r < 0)
                return r;

        r = blkid_probe_lookup_value(b, "TYPE", &s, NULL);
        if (r < 0)
                return r;

        *fstype = strdup(s);
        if (!*fstype)
                return -ENOMEM;

        return 0;
}

static int sysfs_cb(int sysfd, const char *subsystem, const char *devtype,
                    int devfd, const char *devname, const char *modalias,
                    void *userdata) {
        _c_cleanup_(c_freep) char *device = NULL;
        Manager *m = userdata;
        int r;

        if (asprintf(&device, "/dev/%s", devname) < 0)
                return -ENOMEM;

        r = disk_probe(device, m);
        if (r < 0)
                return 0;

        return 1;
}

static int manager_run(Manager *m) {
        _c_cleanup_(c_closep) int sysfd = -1;
        c_usec start;
        bool exit = false;
        int r;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return -errno;

        start = c_usec_from_clock(CLOCK_BOOTTIME);

        while (!exit) {
                c_usec now;
                int n;
                struct epoll_event ev;

                n = epoll_wait(m->fd_ep, &ev, 1, 100);
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

                //FIXME: hook up device events
                if (m->partition)
                        r = access(m->partition, R_OK) == 0;
                else
                        r = sysfs_enumerate(sysfd, "block", "disk", -1, sysfs_cb, m);
                if (r < 0)
                        return r;
                if (r == 1)
                        return 0;

                now = c_usec_from_clock(CLOCK_BOOTTIME);
                if (now - start > c_usec_from_sec(30))
                        break;
        }

        if (m->partition)
                kmsg(LOG_ERR, "Partition %s not found.", m->disk);
        if (m->disk)
                kmsg(LOG_ERR, "Disk %s not found.", m->disk);

        return -ENODEV;
}

static int partition_mount(const char *partition, const char *dir) {
        _c_cleanup_(c_freep) char *fstype = NULL;
        int r;

        r = partition_probe(partition, &fstype);
        if (r < 0) {
                kmsg(LOG_ERR, "No partition or no filesystem found on partition %s.", partition);
                return r;
        }

        if (mkdir(dir, 0755) < 0)
                return -errno;

        kmsg(LOG_INFO, "Mounting partition %s (%s).", partition, fstype);
        if (mount(partition, dir, fstype, MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) < 0)
                return -errno;

        return 0;
}

static int system_image_mount(const char *image, const char *dir) {
        _c_cleanup_(c_closep) int fd_loopctl = -1;
        _c_cleanup_(c_closep) int fd_loop = -1;
        _c_cleanup_(c_closep) int fd_image = -1;
        _c_cleanup_(c_freep) char *loopdev = NULL;
        int n;

        fd_loopctl = open("/dev/loop-control", O_RDWR|O_CLOEXEC);
        if (fd_loopctl < 0)
                return -errno;

        n = ioctl(fd_loopctl, LOOP_CTL_GET_FREE);
        if (n < 0)
                return -errno;

        if (asprintf(&loopdev, "/dev/loop%d", n) < 0)
                return -ENOMEM;

        fd_loop = open(loopdev, O_RDWR|O_CLOEXEC);
        if (fd_loop < 0)
                return -errno;

        fd_image = open(image, O_RDONLY|O_CLOEXEC);
        if (fd_image < 0)
                return -errno;

        if (ioctl(fd_loop, LOOP_SET_FD, fd_image) < 0)
                return -errno;

        if (mount(loopdev, dir, "squashfs", MS_RDONLY|MS_NODEV, NULL) < 0)
                return -errno;

        return 0;
}

static int directory_delete(int *dfd) {
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        struct stat st;
        struct dirent *d;
        int r;

        dir = fdopendir(*dfd);
        if (!dir)
                return -errno;
        *dfd = -1;

        if (fstat(dirfd(dir), &st) < 0)
                return -errno;

        for (d = readdir(dir); d; d = readdir(dir)) {
                if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
                        continue;

                if (d->d_type == DT_DIR) {
                        struct stat st2;
                        _c_cleanup_(c_closep) int dfd2 = -1;

                        if (fstatat(dirfd(dir), d->d_name, &st2, AT_SYMLINK_NOFOLLOW) < 0)
                                continue;

                        if (st.st_dev != st2.st_dev)
                                continue;

                        dfd2 = openat(dirfd(dir), d->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (dfd2 < 0)
                                return -errno;

                        r = directory_delete(&dfd2);
                        if (r < 0)
                                return r;

                        if (unlinkat(dirfd(dir), d->d_name, AT_REMOVEDIR) < 0)
                                return -errno;

                        continue;
                }

                if (unlinkat(dirfd(dir), d->d_name, 0) < 0)
                        return -errno;
        }

        return 0;
}

static int switch_root(const char *newroot) {
        static const char *mounts[] = {
                "/dev",
                "/proc",
                "/sys",
        };
        unsigned i;
        _c_cleanup_(c_closep) int rootfd = -1;
        int r;

        rootfd = openat(AT_FDCWD, "/", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (rootfd < 0)
                return -errno;

        for (i = 0; i < C_ARRAY_SIZE(mounts); i++) {
                _c_cleanup_(c_freep) char *target = NULL;

                if (asprintf(&target, "%s%s", newroot, mounts[i]) < 0)
                        return -ENOMEM;

                if (mount(mounts[i], target, NULL, MS_MOVE, NULL) < 0)
                        return -errno;
        }

        if (chdir(newroot) < 0)
                return -errno;

        if (mount(newroot, "/", NULL, MS_MOVE, NULL) < 0)
                return -errno;

        if (chroot(".") < 0)
                return -errno;

        if (chdir("/") < 0)
                return -errno;

        r = directory_delete(&rootfd);
        if (r < 0)
                return r;

        return 0;
}

static int stdio_connect(const char *device) {
        int fd;

        fd = open(device, O_RDWR|O_NOCTTY);
        if (fd < 0)
                return -errno;

        if (dup2(fd, STDIN_FILENO) < 0 ||
            dup2(fd, STDOUT_FILENO) < 0 ||
            dup2(fd, STDERR_FILENO) < 0)
            return -errno;

        if (fd > STDERR_FILENO)
                close(fd);

        return 0;
}

static int rdshell(const char *release) {
        const char *argv[] = {
                "/usr/bin/bash",
                NULL
        };
        const char *env[] = {
                "TERM=linux",
                NULL
        };
        pid_t p;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                if (stdio_connect("/dev/console") < 0)
                        return EXIT_FAILURE;

                if (setsid() < 0)
                        return EXIT_FAILURE;

                if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) < 0)
                        return EXIT_FAILURE;

                printf("Welcome to %s (%s).\n\n"
                       "Type 'exit' to continue.\n\n", program_invocation_short_name, release);

                execve(argv[0], (char **)argv, (char **)env);
                return EXIT_FAILURE;
        }

        p = waitpid(p, NULL, 0);
        if (p < 0)
                return -errno;

        return 0;
}

int main(int argc, char **argv) {
        static char name[] = "org.bus1.rdinit";
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
        _c_cleanup_(c_freep) char *release = NULL;
        bool shell = false;
        _c_cleanup_(c_freep) char *image = NULL;
        _c_cleanup_(c_freep) char *init = NULL;
        struct timezone tz = {};
        const char *init_argv[] = {
                "/usr/bin/org.bus1.init",
                NULL
        };
        int r;

        program_invocation_short_name = name;
        if (prctl(PR_SET_NAME, name) < 0) {
                r = -errno;
                goto fail;
        }

        umask(0);
        if (setsid() < 0) {
                r = -errno;
                goto fail;
       }

        /* do a dummy call to disable the time zone warping magic */
        if (settimeofday(NULL, &tz) < 0) {
                r = -errno;
                goto fail;
        }

        r = manager_new(&m);
        if (r < 0)
                goto fail;

        /* early mount before module load and command line parsing */
        r = kernel_filesystem_mount(true);
        if (r < 0)
                goto fail;

        log = kmsg(0, NULL);
        if (!log) {
                r = -errno;
                goto fail;
        }

        r = stdio_connect("/dev/null");
        if (r < 0)
                goto fail;

        r = kernel_cmdline_option("rdshell", NULL);
        if (r < 0)
                goto fail;
        shell = !!r;

        r = bus1_read_release(&release);
        if (r < 0)
                goto fail;

        if (shell) {
                r = rdshell(release);
                if (r < 0)
                        goto fail;
        }

        r = modules_load();
        if (r < 0)
                goto fail;

        /* late mount after module load */
        r = kernel_filesystem_mount(false);
        if (r < 0)
                goto fail;

        if (mkdir("/sysroot", 0755) < 0) {
                r = -errno;
                goto fail;
        }

        r = tmpfs_root("/sysroot");
        if (r < 0)
                goto fail;

        r = kernel_cmdline_option("partition", &m->partition);
        if (r < 0)
                goto fail;

        r = kernel_cmdline_option("disk", &m->disk);
        if (r < 0)
                goto fail;

        if (!m->partition && !m->disk) {
                kmsg(LOG_EMERG, "Missing disk= or partition= on the kernel command line.");
                r = -EINVAL;
                goto fail;
        }

        r = manager_start_services(m, -1);
        if (r < 0)
                goto fail;

        r = manager_run(m);
        if (r < 0)
                goto fail;

        r = partition_mount(m->partition, "/sysroot/bus1");
        if (r < 0)
                goto fail;

        if (asprintf(&image, "/sysroot/bus1/system/%s.img", release) < 0) {
                r = -ENOMEM;
                goto fail;
        }

        r = system_image_mount(image, "/sysroot/usr");
        if (r < 0) {
                kmsg(LOG_EMERG, "Unable to mount system image %s: %s.", image, strerror(-r));
                goto fail;
        }

        if (mount("/sysroot/usr/etc", "/sysroot/etc", NULL, MS_BIND, NULL) < 0) {
                r = -errno;
                goto fail;
        }

        if (mount("/sysroot/bus1/data", "/sysroot/var", NULL, MS_BIND, NULL) < 0) {
                r = -errno;
                goto fail;
        }

        if (shell) {
                r = rdshell(release);
                if (r < 0)
                        goto fail;
        }

        r = manager_stop_services(m);
        if (r < 0)
                goto fail;

        r = child_reap(NULL);
        if (r < 0)
                goto fail;

        r = switch_root("/sysroot");
        if (r < 0)
                goto fail;

        r = kernel_cmdline_option("init", &init);
        if (r < 0)
                goto fail;

        kmsg(LOG_INFO, "Executing %s.", init ?: "org.bus1.init");
        if (init)
                init_argv[0] = init;

        execve(init_argv[0], (char **)init_argv, NULL);
        r = -errno;
        kmsg(LOG_EMERG, "Failed to execute %s.", init_argv[0]);

fail:
        kmsg(LOG_EMERG, "Unrecoverable failure. System rebooting: %s.", strerror(-r));

        if (shell)
                rdshell(release);

        sleep(5);
        reboot(RB_AUTOBOOT);

        return EXIT_FAILURE;
}

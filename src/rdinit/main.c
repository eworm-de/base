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

#include <org.bus1/b1-platform.h>
#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libkmod.h>
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

#include "shared/disk-encrypt.h"
#include "shared/disk-sign.h"
#include "shared/file.h"
#include "shared/kmsg.h"
#include "shared/mount.h"
#include "shared/kernel-cmdline.h"
#include "shared/process.h"
#include "shared/tmpfs-root.h"
#include "shared/uuid.h"

#include "dev.h"
#include "disk-gpt.h"
#include "sysctl.h"

typedef struct {
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_signal;

        uint8_t disk_uuid[16];   /* Boot disk GPT UUID. */
        char *device_data;       /* Data device mounted at /var. */
        char *device_boot;       /* Boot device mounted at /boot. */
        char *loader_dir;        /* Boot loader directory in /boot. */

        /* org.bus1.activator */
        pid_t activator_pid;
} Manager;

static Manager *manager_free(Manager *m) {
        free(m->device_data);
        free(m->device_boot);
        free(m->loader_dir);
        c_close(m->fd_ep);
        c_close(m->fd_signal);
        free(m);
        return NULL;
}

C_DEFINE_CLEANUP(Manager *, manager_free);
static int manager_new(Manager **manager) {
        _c_cleanup_(manager_freep) Manager *m = NULL;
        sigset_t mask;
        _c_cleanup_(c_closep) int fd_signal = -1;
        _c_cleanup_(c_closep) int fd_ep = -1;

        m = calloc(1, sizeof(Manager));
        if (!m)
                return -ENOMEM;

        m->activator_pid = -1;

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

        return 0;
}

static int kernel_filesystem_mount(void) {
        static const struct mountpoint {
                const char *what;
                const char *where;
                const char *type;
                const char *options;
                unsigned long flags;
        } mounts[] = {
                { "devtmpfs", "/dev",         "devtmpfs", "mode=755",                            MS_NOSUID|MS_NOEXEC|MS_STRICTATIME },
                { "devpts",   "/dev/pts",     "devpts",   "mode=620,ptmxmode=0666,newinstance",  MS_NOSUID|MS_NOEXEC },
                { "proc",     "/proc",        "proc",     "hidepid=2",                           MS_NOSUID|MS_NOEXEC|MS_NODEV },
                { "sysfs",    "/sys",         "sysfs",    NULL,                                  MS_NOSUID|MS_NOEXEC|MS_NODEV },
        };

        for (size_t i = 0; i < C_ARRAY_SIZE(mounts); i++) {
                if (mkdir(mounts[i].where, 0700) < 0 && errno != EEXIST)
                        return -errno;

                if (mount(mounts[i].what, mounts[i].where,
                          mounts[i].type, mounts[i].flags,
                          mounts[i].options) < 0)
                        return -errno;
        }

        /* Replace device node with a symlink to the local node from the mounted filesystem instance. */
        unlink("/dev/ptmx");
        if (symlink("pts/ptmx", "/dev/ptmx") < 0)
                return -errno;

        return 0;
}

static void module_log(void *data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {}

static int modules_load(void) {
        static const struct module {
                const char *name;
                const char *path;
        } modules[] = {
                { "bus1",   "/sys/module/bus1" },
                { "dm_mod", "/sys/module/dm_mod" },
                { "loop",   "/sys/module/loop" },
        };
        struct kmod_ctx *ctx = NULL;
        int r = 0;

        for (size_t i = 0; i < C_ARRAY_SIZE(modules); i++) {
                struct kmod_module *mod;

                if (modules[i].path && access(modules[i].path, F_OK) >= 0)
                        continue;

                if (!ctx) {
                        ctx = kmod_new(NULL, NULL);
                        if (!ctx) {
                                r = -ENOMEM;
                                break;
                        }

                        kmod_set_log_fn(ctx, module_log, NULL);
                        r = kmod_load_resources(ctx);
                        if (r < 0)
                                break;
                }

                r = kmod_module_new_from_name(ctx, modules[i].name, &mod);
                if (r < 0)
                        break;

                r = kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST|KMOD_PROBE_IGNORE_COMMAND,
                                                    NULL, NULL, NULL, NULL);
                kmod_module_unref(mod);
                if (r < 0)
                        break;
        }

        kmod_unref(ctx);
        return r;
}

static int sysfs_cb(int sysfd, const char *subsystem, const char *devtype,
                    int devfd, const char *devname, const char *modalias,
                    void *userdata) {
        _c_cleanup_(c_freep) char *device = NULL;
        Manager *m = userdata;
        int r;

        if (asprintf(&device, "/dev/%s", devname) < 0)
                return -ENOMEM;

        r = disk_gpt_find_partitions(device, m->disk_uuid, &m->device_boot, &m->device_data);
        if (r < 0)
                return 0;

        return 1;
}

static int manager_run(Manager *m) {
        _c_cleanup_(c_closep) int sysfd = -1;
        uint64_t start_usec;
        bool exit = false;
        int r;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return -errno;

        start_usec = c_usec_from_clock(CLOCK_BOOTTIME);

        while (!exit) {
                uint64_t now_usec;
                int n;
                struct epoll_event ev;

                n = epoll_wait(m->fd_ep, &ev, 1, 100);
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

                //FIXME: hook up device events
                if (m->device_boot)
                        r = access(m->device_boot, R_OK) == 0 && access(m->device_data, R_OK) == 0;
                else
                        r = sysfs_enumerate(sysfd, "block", "disk", -1, sysfs_cb, m);
                if (r < 0)
                        return r;
                if (r == 1)
                        return 0;

                now_usec = c_usec_from_clock(CLOCK_BOOTTIME);
                if (now_usec - start_usec > c_usec_from_sec(30))
                        break;
        }

        if (m->device_boot)
                kmsg(LOG_ERR, "Boot device %s not found.", m->device_boot);
        if (m->device_data)
                kmsg(LOG_ERR, "Data device %s not found.", m->device_data);

        return -ENODEV;
}

static int format_data(const char *device,
                      char **device_cryptp,
                      const char *image_name,
                      const char *data_type) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        uint8_t buf[4096];
        _c_cleanup_(c_freep) char *device_crypt = NULL;
        pid_t p;
        int r;

        f = fopen(device, "r+e");
        if (!f)
                return -errno;

        if (fread(&buf, sizeof(buf), 1, f) != 1)
                return -EIO;

        for (size_t i = 0; i < sizeof(buf); i++)
                if (buf[i] != 0)
                        return -EBUSY;

        r = disk_encrypt_format_volume(device,
                                       image_name,
                                       data_type,
                                       NULL,
                                       NULL);
        if (r < 0)
                return r;

        r = disk_encrypt_setup_device(device, &device_crypt, NULL, NULL);
        if (r < 0)
                return r;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                _c_cleanup_(c_freep) char *mkfs = NULL;
                const char *argv[] = {
                        NULL,
                        "-L",
                        "bus1",
                        "-q",
                        device_crypt,
                        NULL
                };

                if (asprintf(&mkfs, "/usr/bin/mkfs.%s", data_type) < 0)
                        return EXIT_FAILURE;

                argv[0] = mkfs;
                execve(argv[0], (char **)argv, NULL);

                return EXIT_FAILURE;
        }

        p = waitpid(p, NULL, 0);
        if (p < 0)
                return -errno;

        *device_cryptp = device_crypt;
        device_crypt = NULL;

        return 0;
}

static int mount_data(const char *device, const char *dir) {
        _c_cleanup_(c_freep) char *device_crypt = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        int r;

        r = disk_encrypt_setup_device(device, &device_crypt, &image_name, &data_type);
        if (r < 0) {
                image_name = strdup("org.bus1.data");
                if (!image_name)
                        return -ENOMEM;

                //FIXME: get default fstype from /usr/lib/org.bus1/data-filesystem
                data_type = strdup("xfs");
                if (!data_type)
                        return -ENOMEM;

                kmsg(LOG_INFO, "Data partition %s at %s is not initialized.", image_name, device);

                r = format_data(device, &device_crypt, image_name, data_type);
                if (r < 0)
                        goto fail;

                kmsg(LOG_INFO, "Initialized data partition %s at %s (%s).", image_name, device, data_type);
        }

        kmsg(LOG_INFO, "Mounting %s device %s (%s) at /var.", image_name, device_crypt, data_type);
        if (mount(device_crypt, dir, data_type, MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL) < 0)
                return -errno;

        return 0;

fail:
        kmsg(LOG_ERR, "Unable to unlock data volume %s: %s", device, strerror(-r));

        return r;
}

static int mount_usr(const char *image, const char *dir) {
        _c_cleanup_(c_freep) char *device = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        int r;

        r = disk_sign_setup_device(image, &device, &data_type);
        if (r < 0)
                return r;

        kmsg(LOG_INFO, "Mounting %s (%s) at /usr.", device, data_type);
        if (mount(device, dir, data_type, MS_RDONLY|MS_NODEV, NULL) < 0)
                return -errno;

        return 0;
}

static int directory_delete(int *dfd) {
        _c_cleanup_(c_closedirp) DIR *dir = NULL;
        struct stat st;
        int r;

        dir = fdopendir(*dfd);
        if (!dir)
                return -errno;
        *dfd = -1;

        if (fstat(dirfd(dir), &st) < 0)
                return -errno;

        for (struct dirent *d = readdir(dir); d; d = readdir(dir)) {
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
        _c_cleanup_(c_closep) int rootfd = -1;
        int r;

        rootfd = openat(AT_FDCWD, "/", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (rootfd < 0)
                return -errno;

        for (size_t i = 0; i < C_ARRAY_SIZE(mounts); i++) {
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

static int manager_parse_kernel_cmdline(Manager *m) {
        _c_cleanup_(c_freep) char *disk = NULL;
        int r;

        r = kernel_cmdline_option("loader", &m->loader_dir);
        if (r < 0)
                return r;

        if (m->loader_dir) {
                char *s;

                if (m->loader_dir[0] != '/')
                        return -EINVAL;

                s = strrchr(m->loader_dir, '/');
                if (!s)
                        return -EINVAL;

                *s = '\0';
        }

        r = kernel_cmdline_option("disk", &disk);
        if (r < 0)
                return r;

        if (disk) {
                r = uuid_from_string(disk, m->disk_uuid);
                if (r < 0)
                        return r;

                return 0;
        }

        r = kernel_cmdline_option("data", &m->device_data);
        if (r < 0)
                return r;

        r = kernel_cmdline_option("boot", &m->device_boot);
        if (r < 0)
                return r;

        if (!m->device_data || !m->device_boot)
                return -EINVAL;

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
        reboot(RB_AUTOBOOT);
}

int main(int argc, char **argv) {
        static const struct sigaction sa = {
                .sa_handler = dump_process,
                .sa_flags = SA_NODEFER,
        };

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

        r = kernel_filesystem_mount();
        if (r < 0)
                goto fail;

        log = kmsg(0, NULL);
        if (!log) {
                r = -errno;
                goto fail;
        }

        r = dev_symlinks("/dev");
        if (r < 0)
                goto fail;

        r = stdio_connect("/dev/null");
        if (r < 0)
                goto fail;

        r = kernel_cmdline_option("rdshell", NULL);
        if (r < 0)
                goto fail;
        shell = !!r;

        r = file_read_line("/usr/lib/org.bus1/release", &release);
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

        r = tmpfs_root("/tmp");
        if (r < 0)
                goto fail;

        r = manager_parse_kernel_cmdline(m);
        if (r < 0) {
                kmsg(LOG_EMERG, "Expecting disk= or boot=, data= on the kernel command line.");
                r = -EINVAL;
                goto fail;
        }

        r = manager_start_services(m, -1);
        if (r < 0)
                goto fail;

        r = manager_run(m);
        if (r < 0)
                goto fail;

        if (mkdir("/tmp/boot", 0755) < 0)
                goto fail;

        kmsg(LOG_INFO, "Mounting boot device %s at /boot.", m->device_boot);
        r = mount_boot(m->device_boot, "/tmp/boot", 0);
        if (r < 0)
                return r;

        if (asprintf(&image, "/tmp/boot%s/%s.img", m->loader_dir ?: "", release) < 0) {
                r = -ENOMEM;
                goto fail;
        }

        kmsg(LOG_INFO, "Setting up cryptographic integrity validation of system image %s.img.", release);
        r = mount_usr(image, "/tmp/usr");
        if (r < 0) {
                kmsg(LOG_EMERG, "Unable to mount system image %s: %s.", image, strerror(-r));
                goto fail;
        }

        if (mount("/tmp/usr/etc", "/tmp/etc", NULL, MS_BIND, NULL) < 0) {
                r = -errno;
                goto fail;
        }

        kmsg(LOG_INFO, "Setting up decryption of data volume %s.", m->device_data);
        r = mount_data(m->device_data, "/tmp/var");
        if (r < 0)
                goto fail;

        if (symlink("../run", "/tmp/var/run") < 0 && errno != EEXIST) {
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

        r = process_reap_children(NULL);
        if (r < 0)
                goto fail;

        r = switch_root("/tmp");
        if (r < 0)
                goto fail;

        r = sysctl_apply();
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

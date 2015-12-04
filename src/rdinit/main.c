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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <dirent.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <linux/loop.h>
#include <libkmod.h>

#include <c-macro.h>
#include <c-cleanup.h>

static const struct mountpoint {
        const char *what;
        const char *where;
        const char *type;
        const char *options;
        unsigned long flags;
} mountpoints[] = {
        { "proc",     "/proc",    "proc",     NULL,        MS_NOSUID|MS_NOEXEC|MS_NODEV },
        { "sysfs",    "/sys",     "sysfs",    NULL,        MS_NOSUID|MS_NOEXEC|MS_NODEV },
        { "devtmpfs", "/dev",     "devtmpfs", "mode=755",  MS_NOSUID|MS_STRICTATIME },
        { "devpts",   "/dev/pts", "devpts",   "mode=620",  MS_NOSUID|MS_NOEXEC },
};

static const struct module {
        const char *name;
        const char *path;
} modules[] = {
        { "loop", "/sys/module/loop" },
};

static int filesystem_mount(void) {
        unsigned int i;
        int r;

        for (i = 0; i < C_ARRAY_SIZE(mountpoints); i++) {
                r = mkdir(mountpoints[i].where, 0700);
                if (r < 0 && errno != EEXIST)
                        return -errno;

                r = mount(mountpoints[i].what, mountpoints[i].where,
                          mountpoints[i].type, mountpoints[i].flags,
                          mountpoints[i].options);
                if (r < 0 && errno != EBUSY)
                        return -errno;
        }

        return 0;
}

static void module_log(void *data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {}

static int modules_load(void) {
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

                kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST, NULL, NULL, NULL, NULL);
                kmod_module_unref(mod);
        }

err:
        kmod_unref(ctx);
        return r;
}

static int newroot_create(const char *root) {
        _c_cleanup_(c_closep) int rootfd = -1;
        static const char *dirs[] = {
                "proc",
                "sys",
                "dev",
                "usr",
                "lib64",
        };
        static const struct link {
                const char *file;
                const char *target;
        } links[] = {
                { "bin",                        "usr/bin" },
                { "etc",                        "usr/etc" },
                { "lib",                        "usr/lib" },
                { "sbin",                       "usr/bin" },
                { "lib64/ld-linux-x86-64.so.2", "../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" },
        };
        unsigned int i;

        if (mkdir(root, 0755) < 0)
                return -errno;

        rootfd = openat(AT_FDCWD, root, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (rootfd < 0)
                return -errno;

        for (i = 0; i < C_ARRAY_SIZE(dirs); i++)
                if (mkdirat(rootfd, dirs[i], 0755) < 0)
                        return -errno;

        for (i = 0; i < C_ARRAY_SIZE(links); i++)
                if (symlinkat(links[i].target, rootfd, links[i].file) < 0)
                        return -errno;

        return 0;
}

static int bus1_disk_mount(const char *dir) {
        if (mkdir(dir, 0755) < 0)
                return -errno;

        if (mount("/dev/sda2", dir, "xfs", 0, NULL) < 0)
                return -errno;

        return 0;
}

static int bus1_system_mount(const char *image, const char *dir) {
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

        if (mount(loopdev, dir, "squashfs", MS_RDONLY, NULL) < 0)
                return -errno;

        return 0;
}

static pid_t service_start(const char *prog) {
        const char *argv[] = {
                prog,
                NULL
        };
        const char *env[] = {
                NULL
        };
        pid_t p;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                if (setsid() < 0)
                        return -errno;

                execve(argv[0], (char **)argv, (char **)env);
                return -errno;
        }

        return p;
}

static int directory_delete(int *dfd, const char *exclude) {
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

                if (exclude && strcmp(d->d_name, exclude) == 0)
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

                        r = directory_delete(&dfd2, NULL);
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

static int root_switch(const char *newroot) {
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

        for (i = 0; i< C_ARRAY_SIZE(mounts); i++) {
                _c_cleanup_(c_freep) char *target = NULL;

                if (asprintf(&target, "%s%s", newroot, mounts[i]) < 0)
                        return -ENOMEM;

                if (mount(mounts[i], target, NULL, MS_MOVE, NULL) < 0)
                        return -errno;
        }

        if (chdir(newroot) < 0)
                return -errno;

        if (mount(newroot, "/", NULL, MS_BIND, NULL) < 0)
                return -errno;

        if (chroot(".") < 0)
                return -errno;

        if (chdir("/") < 0)
                return -errno;

        r = directory_delete(&rootfd, newroot + 1);
        if (r < 0)
                return r;

        return 0;
}

static int init_execute(void) {
        const char *argv[] = {
                "/usr/bin/org.bus1.init",
                NULL
        };
        const char *env[] = {
                NULL
        };

        execve(argv[0], (char **)argv, (char **)env);
        return -errno;
}

static int bash_execute(void) {
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
                if (setsid() < 0)
                        return -errno;

                if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) < 0)
                        return -errno;

                printf("Welcome to org.bus1.rdinit.\n\n"
                       "Type exit to continue.\n\n");

                execve(argv[0], (char **)argv, (char **)env);
                return -errno;
        }

        p = waitpid(p, NULL, 0);
        if (p < 0)
                return errno;

        return 0;
}

static int kernel_cmdline_option(const char *key) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char line[4096];
        char *s;
        size_t l;

        f = fopen("/proc/cmdline", "re");
        if (!f)
                return false;

        if (fgets(line, sizeof(line), f) == NULL)
                return false;

        s = strstr(line, key);
        if (!s)
                return false;

        if (s > line && s[-1] != ' ')
                return false;

        l = strlen(key);
        if (s[l] != ' ' && s[l] != '\0' && s[l] != '\n')
                return false;

        return true;
}

int main(int argc, char **argv) {
        static char name[] = "org.bus1.rdnit";
        struct sigaction sa = {
                .sa_handler = SIG_IGN,
                .sa_flags = SA_NOCLDSTOP|SA_NOCLDWAIT|SA_RESTART,
        };
        struct timezone tz = {};
        pid_t pid_devices = 0;
        int r;

        argv[0] = name;
        prctl(PR_SET_NAME, name);

        umask(0);
        if (setsid() < 0)
                return EXIT_FAILURE;

        if (sigaction(SIGCHLD, &sa, NULL) < 0)
                return -errno;

        r = filesystem_mount();
        if (r < 0)
                return EXIT_FAILURE;

        /* do a dummy call to disable the time zone warping magic */
        r  = settimeofday(NULL, &tz);
        if (r < 0)
                return EXIT_FAILURE;

        r = modules_load();
        if (r < 0)
                return EXIT_FAILURE;

        pid_devices = service_start("/usr/bin/org.bus1.devices");
        if (pid_devices < 0)
                return EXIT_FAILURE;

        r = newroot_create("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        r = bus1_disk_mount("/sysroot/bus1");
        if (r < 0)
                return EXIT_FAILURE;

        r = bus1_system_mount("/sysroot/bus1/system/system.img", "/sysroot/usr");
        if (r < 0)
                return EXIT_FAILURE;

        if (symlink("bus1/data", "/sysroot/var") < 0)
                return EXIT_FAILURE;

        if (kernel_cmdline_option("rd.shell"))
                bash_execute();

        if (kill(pid_devices, SIGTERM) < 0)
                return EXIT_FAILURE;

        r = root_switch("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        init_execute();
        return EXIT_FAILURE;
}

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
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include <libkmod.h>

#define ELEMENTSOF(x) (sizeof(x) / sizeof((x)[0]))

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

        for (i = 0; i < ELEMENTSOF(mountpoints); i++) {
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
        int r;

        for (i = 0; i < ELEMENTSOF(modules); i++) {
                struct kmod_module *mod;

                if (modules[i].path && access(modules[i].path, F_OK) >= 0)
                        continue;

                if (!ctx) {
                        ctx = kmod_new(NULL, NULL);
                        if (!ctx)
                                return -ENOMEM;

                        kmod_set_log_fn(ctx, module_log, NULL);
                        r = kmod_load_resources(ctx);
                        if (r < 0)
                                return r;
                }

                r = kmod_module_new_from_name(ctx, modules[i].name, &mod);
                if (r < 0)
                        continue;

                kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST, NULL, NULL, NULL, NULL);
                kmod_module_unref(mod);
        }

        kmod_unref(ctx);

        return 0;
}

static int exec_bash(void) {
        const char *argv[] = {
                "/bin/bash",
                NULL
        };
        const char *env[] = {
                "TERM=linux",
                NULL
        };
        pid_t p;
        int r;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                r = setsid();
                if (r < 0)
                        return -errno;

                r = ioctl(STDIN_FILENO, TIOCSCTTY, 1);
                if (r < 0)
                        return -errno;

                execve(argv[0], (char **)argv, (char **)env);
                return -errno;
        }

        p = waitpid(p, NULL, 0);
        if (p < 0)
                return -errno;

        return 0;
}

static pid_t exec_service(const char *prog) {
        const char *argv[] = {
                prog,
                NULL
        };
        const char *env[] = {
                NULL
        };
        pid_t p;
        int r;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                r = setsid();
                if (r < 0)
                        return -errno;

                execve(argv[0], (char **)argv, (char **)env);
                return -errno;
        }

        return p;
}

int main(int argc, char **argv) {
        static char name[] = "org.bus1.init";
        struct timezone tz = {};

        int rootfd;
        int r;

        argv[0] = name;
        prctl(PR_SET_NAME, name);

        r = filesystem_mount();
        if (r < 0)
                return EXIT_FAILURE;

        /* do a dummy call to disable the time zone warping magic */
        r  = settimeofday(NULL, &tz);
        if (r < 0)
                return EXIT_FAILURE;

        umask(0);
        if (setsid() < 0)
                return EXIT_FAILURE;

        r = modules_load();
        if (r < 0)
                return EXIT_FAILURE;

        if (mkdir("/sysroot", 0755) < 0 ||
            mkdir("/sysroot/dev", 0755) < 0 ||
            mkdir("/sysroot/proc", 0755) < 0 ||
            mkdir("/sysroot/sys", 0755) < 0 ||
            mkdir("/sysroot/usr", 0755) < 0)
                return EXIT_FAILURE;

        if (symlink("usr/etc", "/sysroot/etc") < 0 ||
            symlink("usr/bin", "/sysroot/bin") < 0 ||
            symlink("usr/bin", "/sysroot/sbin") < 0)
                return EXIT_FAILURE;

        if (mkdir("/sysroot/lib64", 0755) < 0 ||
            symlink("../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2", "/sysroot/lib64/ld-linux-x86-64.so.2") < 0)
                return EXIT_FAILURE;

        if (mkdir("/bus1", 0755) < 0)
                return EXIT_FAILURE;

        // read gpt, find bus1 part
        if (mount("/dev/sda2", "/bus1", "xfs", 0, NULL) < 0)
                return EXIT_FAILURE;

        exec_service("/usr/bin/org.bus1.devices");
        exec_bash();

        // create loopdev, attach /bus1/system/system.img
        // mount /sysroot/usr, mount /sysroot/var

        if (mount("/dev", "/sysroot/dev", NULL, MS_MOVE, NULL) < 0||
            mount("/proc", "/sysroot/dev", NULL, MS_MOVE, NULL) < 0 ||
            mount("/sys", "/sysroot/sys", NULL, MS_MOVE, NULL) < 0)
                return EXIT_FAILURE;

        rootfd = openat(AT_FDCWD, "/", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (rootfd < 0)
                return EXIT_FAILURE;

        if (chdir("/sysroot") ||
            mount("/sysroot", "/", NULL, MS_MOVE, NULL) ||
            chroot(".") ||
            chdir("/"))
                return EXIT_FAILURE;

        // cleanup leftover /
        close(rootfd);

        return EXIT_FAILURE;
}

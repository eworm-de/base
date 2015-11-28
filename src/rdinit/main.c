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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

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

        for (i = 0; i < ARRAY_SIZE(mountpoints); i++) {
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

        for (i = 0; i < ARRAY_SIZE(modules); i++) {
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

static int basedirs_create(const char *base) {
        int basefd;
        static const char *dirs[] = {
                "proc",
                "sys",
                "dev",
                "usr",
                "var",
                "lib64",
        };
        static const struct link {
                const char *file;
                const char *target;
        } links[] = {
                { "etc",                        "usr/etc" },
                { "bin",                        "usr/bin" },
                { "sbin",                       "usr/bin" },
                { "lib64/ld-linux-x86-64.so.2", "../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" },
        };
        unsigned int i;
        int r;

        r = mkdir(base, 0755);
        if (r < 0)
                return -errno;

        basefd = openat(AT_FDCWD, base, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (basefd < 0)
                return -errno;

        for (i = 0; i < ARRAY_SIZE(dirs); i++) {
                r = mkdirat(basefd, dirs[i], 0755);
                if (r < 0)
                        return -errno;
        }

        for (i = 0; i < ARRAY_SIZE(links); i++) {
                r = symlinkat(links[i].target, basefd, links[i].file);
                if (r < 0)
                        return -errno;
        }

        close(basefd);
        return 0;
}

static int bus1_disk_mount(const char *dir) {
        int r;

        r = mkdir(dir, 0755);
        if (r < 0)
                return -errno;

        r = mount("/dev/sda2", dir, "xfs", 0, NULL);
        if (r < 0)
                return -errno;

        return 0;
}


static int bash(void) {
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

static pid_t service_start(const char *prog) {
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

static int root_switch(const char *root) {
        static const char *mounts[] = {
                "/dev",
                "/proc",
                "/sys",
        };
        unsigned i;
        int rootfd;
        int r;

        rootfd = openat(AT_FDCWD, "/", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (rootfd < 0)
                return -errno;

        for (i = 0; i< ARRAY_SIZE(mounts); i++) {
                char *target = NULL;

                if (asprintf(&target, "%s%s", root, mounts[i]) < 0)
                        return -ENOMEM;

                r = mount(mounts[i], target, NULL, MS_MOVE, NULL);
                if (r < 0)
                        return -errno;

                free(target);
        }

        r  = chdir(root);
        if (r < 0)
                return -errno;

        r = mount(root, "/", NULL, MS_MOVE, NULL);
        if (r < 0)
                return -errno;

        r = chroot(".");
        if (r < 0)
                return -errno;

        r = chdir("/");
                return -errno;

        // cleanup leftover /
        close(rootfd);

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

int main(int argc, char **argv) {
        static char name[] = "org.bus1.rdnit";
        struct timezone tz = {};
        pid_t pid_devices = 0;
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

        pid_devices = service_start("/usr/bin/org.bus1.devices");
        if (pid_devices < 0)
                return EXIT_FAILURE;

        r = basedirs_create("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        r = bus1_disk_mount("/bus1");
        if (r < 0)
                return EXIT_FAILURE;

        // create loopdev, attach /bus1/system/system.img
        // mount loop /sysroot/usr

        r = mount("/bus1/data", "/sysroot/var", NULL, MS_BIND, NULL);
        if (r < 0)
                return EXIT_FAILURE;

        bash();

        r = kill(pid_devices, SIGTERM);
        if (r < 0)
                return EXIT_FAILURE;

        r = root_switch("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        init_execute();
        return EXIT_FAILURE;
}
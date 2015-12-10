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
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libkmod.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/mount.h>

//FIXME: use bus
#include "../devices/sysfs.h"
#include "util.h"

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
        { "bus1", "/sys/module/bus1" },
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

                kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST|KMOD_PROBE_IGNORE_COMMAND,
                                                NULL, NULL, NULL, NULL);
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

C_DEFINE_CLEANUP(blkid_probe, blkid_free_probe);

static int bus1_disk_probe(const char *disk, const char *disk_uuid, char **partition) {
        const char *s;
        _c_cleanup_(blkid_free_probep) blkid_probe b = NULL;
        blkid_partlist pl;
        int i;
        int r;

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

        if (disk_uuid && strcmp(s, disk_uuid) != 0)
                return -ENODEV;

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
                        if (asprintf(partition, "%sp%d", disk, blkid_partition_get_partno(p)) < 0)
                                return -ENOMEM;
                } else {
                        if (asprintf(partition, "%s%d", disk, blkid_partition_get_partno(p)) < 0)
                                return -ENOMEM;
                }

                return 0;
        }

        return -ENODEV;
}

static int bus1_partition_probe(const char *partition, char **fstype) {
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
                    const void *in, void *out) {
        _c_cleanup_(c_freep) char *device = NULL;
        const char *disk_uuid = in;
        char **partition = out;
        int r;

        if (asprintf(&device, "/dev/%s", devname) < 0)
                return -ENOMEM;

        r = bus1_disk_probe(device, disk_uuid, partition);
        if (r < 0)
                return 0;

        return 1;
}

static int bus1_disk_find(const char *disk_uuid, char **partition) {
        _c_cleanup_(c_closep) int sysfd = -1;
        unsigned int i;
        int r;

        sysfd = openat(AT_FDCWD, "/sys", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (sysfd < 0)
                return -errno;

        for (i = 0; i < 15; i++) {
                r = sysfs_enumerate(sysfd, "block", "disk", -1, sysfs_cb, disk_uuid, partition);
                if (r < 0)
                        return r;
                if (r == 1)
                        return 0;

                //FIXME: wait for events and retry
                sleep(1);
        }

        return -ENOENT;
}

static int bus1_disk_mount(const char *partition, const char *dir) {
        _c_cleanup_(c_freep) char *fstype = NULL;
        int r;

        r = bus1_partition_probe(partition, &fstype);
        if (r < 0)
                return r;

        if (mkdir(dir, 0755) < 0)
                return -errno;

        if (mount(partition, dir, fstype, 0, NULL) < 0)
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

static int init_execute(const char *init) {
        const char *argv[] = {
                "/usr/bin/org.bus1.init",
                NULL
        };

        if (init)
                argv[0] = init;

        execve(argv[0], (char **)argv, NULL);
        return -errno;
}

int main(int argc, char **argv) {
        static char name[] = "org.bus1.rdinit";
        _c_cleanup_(c_freep) char *disk = NULL;
        _c_cleanup_(c_freep) char *partition = NULL;
        _c_cleanup_(c_freep) char *init = NULL;
        struct timezone tz = {};
        pid_t pid_devices = 0;
        bool shell;
        _c_cleanup_(c_freep) char *release = NULL;
        _c_cleanup_(c_freep) char *system = NULL;
        int r;

        program_invocation_short_name = name;
        prctl(PR_SET_NAME, name);

        umask(0);
        if (setsid() < 0)
                return EXIT_FAILURE;

        r = filesystem_mount();
        if (r < 0)
                return EXIT_FAILURE;

        /* do a dummy call to disable the time zone warping magic */
        r  = settimeofday(NULL, &tz);
        if (r < 0)
                return EXIT_FAILURE;

        if (bus1_release(&release) < 0)
                return EXIT_FAILURE;

        shell = kernel_cmdline_option("rdshell", NULL);
        if (shell)
                bash_execute(release);

        r = modules_load();
        if (r < 0)
                return EXIT_FAILURE;

        pid_devices = service_start("/usr/bin/org.bus1.devices");
        if (pid_devices < 0)
                return EXIT_FAILURE;

        r = newroot_create("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        if (kernel_cmdline_option("root", &partition) < 0)
                return EXIT_FAILURE;

        if (kernel_cmdline_option("disk", &disk) < 0)
                return EXIT_FAILURE;

        if (!partition && !disk)
                return EXIT_FAILURE;

        if (!partition) {
                r = bus1_disk_find(disk, &partition);
                if (r < 0)
                        return r;
        }

        r = bus1_disk_mount(partition, "/sysroot/bus1");
        if (r < 0)
                return EXIT_FAILURE;

        if (asprintf(&system, "/sysroot/bus1/system/%s.img", release) < 0)
                return EXIT_FAILURE;

        r = bus1_system_mount(system, "/sysroot/usr");
        if (r < 0)
                return EXIT_FAILURE;

        if (symlink("bus1/data", "/sysroot/var") < 0)
                return EXIT_FAILURE;

        if (shell)
                bash_execute(release);

        if (kill(pid_devices, SIGTERM) < 0)
                return EXIT_FAILURE;

        r = root_switch("/sysroot");
        if (r < 0)
                return EXIT_FAILURE;

        if (kernel_cmdline_option("init", &init) < 0)
                return EXIT_FAILURE;

        init_execute(init);

        return EXIT_FAILURE;
}

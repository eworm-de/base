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
#include <signal.h>
#include <dirent.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>

#include "uevent.h"
#include "permissions.h"
#include "module.h"

static int sysfs_coldplug(int devfd, const char *path, const char *subdir) {
        int dfd;
        DIR *dir;
        struct dirent *dent;
        int r;

        /* /sys/bus, /sys/class */
        dfd = openat(AT_FDCWD, path, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
        if (dfd < 0)
                return dfd;

        dir = fdopendir(dfd);
        if (!dir)
                return -errno;

        /* /sys/bus/pci, /sys/class/block */
        for (dent = readdir(dir);dent ; dent = readdir(dir)) {
                DIR *dir2;
                struct dirent *dent2;

                if (dent->d_name[0] == '.')
                        continue;

                dfd = openat(dirfd(dir), dent->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                if (dfd < 0)
                        return dfd;

                /* /sys/bus/pci/devices */
                if (subdir) {
                        int fd;

                        fd = openat(dfd, subdir, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (fd < 0)
                                return fd;

                        close(dfd);
                        dfd = fd;
                }

                dir2 = fdopendir(dfd);
                if (!dir2)
                        return -errno;

                /* /sys/bus/pci/0000:00:00.0, /sys/class/block/sda */
                for (dent2 = readdir(dir2); dent2; dent2 = readdir(dir2)) {
                        int fd;
                        FILE *f;
                        char line[4096];
                        char *s;
                        ssize_t len;
                        char *subsystem = NULL;
                        char *devtype = NULL;
                        char *devname = NULL;
                        char *modalias = NULL;

                        if (dent2->d_name[0] == '.')
                                continue;

                        dfd = openat(dirfd(dir2), dent2->d_name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
                        if (dfd < 0) {
                                if (errno == ENOTDIR)
                                        continue;

                                return dfd;
                        }

                        len = readlinkat(dfd, "subsystem", line, sizeof(line));
                        if (len < 0)
                                return -EINVAL;
                        if (len <= 0 || len == (ssize_t)sizeof(line))
                                return -EINVAL;
                        line[len] = '\0';

                        s = strrchr(line, '/');
                        if (!s)
                                return -EINVAL;
                        subsystem = strdup(s + 1);
                        if (!subsystem)
                                return -ENOMEM;

                        fd = openat(dfd, "uevent", O_RDONLY|O_NONBLOCK|O_CLOEXEC);
                        if (fd < 0)
                                return fd;

                        f = fdopen(fd, "re");
                        if (!f)
                                return -errno;

                        while (fgets(line, sizeof(line), f) != NULL) {
                                char *value;
                                char *end;

                                /* some broken drivers add another newline */
                                if (strcmp(line, "\n") == 0)
                                        continue;

                                value = strchr(line, '=');
                                if (!value)
                                        return -EINVAL;
                                *value = '\0';
                                value++;

                                end = strchr(value, '\n');
                                if (!end)
                                        return -EINVAL;
                                *end = '\0';

                                if (strcmp(line, "DEVTYPE") == 0) {
                                        devtype = strdup(value);
                                        if (!devtype)
                                                return -ENOMEM;
                                } else if (strcmp(line, "DEVNAME") == 0) {
                                        devname = strdup(value);
                                        if (!devname)
                                                return -ENOMEM;
                                } else if (strcmp(line, "MODALIAS") == 0) {
                                        modalias = strdup(value);
                                        if (!modalias)
                                                return -ENOMEM;
                                }
                        }

                        if (devname) {
                                printf("/dev/%s (%s)\n", devname, subsystem);
                                r = permissions_apply(devfd, devname, subsystem, devtype);
                                if (r < 0)
                                        return r;
                        }


                        if (modalias) {
                                printf("modprobe %s (%s)\n", modalias, subsystem);
                                r = module_load(modalias);
                                if (r < 0)
                                        return r;
                        }

                        free(subsystem);
                        free(devtype);
                        free(devname);
                        free(modalias);

                        fclose(f);
                        close(dfd);
                }

                closedir(dir2);
        }

        closedir(dir);

        return 0;
}

int main(int argc, char **argv) {
        int fd_uevent;
        int fd_signal;
        int fd_ep;
        sigset_t mask;
        struct epoll_event ep_monitor = { .events = EPOLLIN };
        struct epoll_event ep_signal = { .events = EPOLLIN };
        int devfd;
        int r;

        devfd = openat(AT_FDCWD, "/dev", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (devfd < 0)
                return EXIT_FAILURE;

        fd_ep = epoll_create1(EPOLL_CLOEXEC);
        if (fd_ep < 0)
                return EXIT_FAILURE;

        fd_uevent = uevent_connect();
        if (fd_uevent < 0)
                return EXIT_FAILURE;
        ep_monitor.data.fd = fd_uevent;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        fd_signal = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
        if (fd_signal < 0)
                return EXIT_FAILURE;
        ep_signal.data.fd = fd_signal;

        if (epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_uevent, &ep_monitor) < 0 ||
            epoll_ctl(fd_ep, EPOLL_CTL_ADD, fd_signal, &ep_signal) < 0)
                return EXIT_FAILURE;

        r = sysfs_coldplug(devfd, "/sys/bus", "devices");
        if (r < 0)
                return EXIT_FAILURE;

        r = sysfs_coldplug(devfd, "/sys/class", NULL);
        if (r < 0)
                return EXIT_FAILURE;

        for (;;) {
                int n;
                struct epoll_event ev;

                n = epoll_wait(fd_ep, &ev, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return EXIT_FAILURE;
                }

                if (ev.data.fd == fd_uevent && ev.events & EPOLLIN) {
                        char *action = NULL, *subsystem = NULL, *devtype = NULL, *devname = NULL, *modalias = NULL;

                        r = uevent_receive(fd_uevent, &action, &subsystem, &devtype, &devname, &modalias);
                        if (r < 0)
                                return EXIT_FAILURE;

                        if (action && strcmp(action, "add") == 0) {
                                if (devname) {
                                        printf("/dev/%s (%s)\n", devname, subsystem);
                                        r = permissions_apply(devfd, devname, subsystem, devtype);
                                        if (r < 0)
                                                return EXIT_FAILURE;
                                }

                                if (modalias) {
                                        printf("modprobe %s (%s)\n", modalias, subsystem);
                                        r = module_load(modalias);
                                        if (r < 0)
                                                return EXIT_FAILURE;
                                }
                        }

                        free(action);
                        free(subsystem);
                        free(devtype);
                        free(devname);
                        free(modalias);

                        continue;
                }

                if (ev.data.fd == fd_signal && ev.events & EPOLLIN) {
                                struct signalfd_siginfo fdsi;
                                ssize_t size;

                                size = read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                                if (size != sizeof(struct signalfd_siginfo))
                                        continue;
                                if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT)
                                        break;
                }
        }

        close(fd_signal);
        close(fd_uevent);
        close(fd_ep);
        close(devfd);
        return EXIT_SUCCESS;
}

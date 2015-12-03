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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "c-shared.h"

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

                printf("Welcome to bus1!\n\n");

                execve(argv[0], (char **)argv, (char **)env);
                return -errno;
        }

        p = waitpid(p, NULL, 0);
        if (p < 0)
                return errno;

        return 0;
}

int main(int argc, char **argv) {
        struct sigaction sa = {
                .sa_handler = SIG_IGN,
                .sa_flags = SA_NOCLDSTOP|SA_NOCLDWAIT|SA_RESTART,
        };
        pid_t pid_devices = 0;

        umask(0);

        if (sigaction(SIGCHLD, &sa, NULL) < 0)
                return -errno;

        pid_devices = service_start("/usr/bin/org.bus1.devices");
        if (pid_devices < 0)
                return EXIT_FAILURE;

        bash_execute();
        return EXIT_FAILURE;
}

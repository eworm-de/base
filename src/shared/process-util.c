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
#include <sys/stat.h>
#include <sys/wait.h>

#include "process-util.h"
#include "util.h"

int process_reap_children(pid_t *p) {
        pid_t pid = -1;

        for (;;) {
                siginfo_t si = {};

                if (waitid(P_ALL, 0, &si, WEXITED|WNOHANG) < 0) {
                        if (errno == ECHILD)
                                break;

                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                pid = si.si_pid;
                break;
        }

        if (p)
                *p = pid;

        return 0;
}

pid_t process_start_program(const char *prog) {
        const char *argv[] = {
                prog,
                NULL
        };
        pid_t p;

        p = fork();
        if (p < 0)
                return -errno;

        if (p == 0) {
                if (setsid() < 0)
                        return EXIT_FAILURE;

                execve(argv[0], (char **)argv, NULL);
                return EXIT_FAILURE;
        }

        return p;
}

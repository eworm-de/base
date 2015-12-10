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
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "util.h"

int bash_execute(const char *release) {
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
                return errno;

        return 0;
}

pid_t service_start(const char *prog) {
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

bool kernel_cmdline_option(const char *key, char **value) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char line[4096];
        char *s, *e;
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
        if (s[l] == ' ' || s[l] == '\n')
                return true;

        if (s[l] != '=')
                return false;

        s = s + l + 1;
        e = strchr(s, ' ');
        if (!e)
                e = strchr(s, '\n');
        if (!e)
                return false;

        e[0] = '\0';
        *value = strdup(s);
        if (!*value)
                return false;

        return true;
}

int bus1_release(char **release) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char line[4096];
        size_t len;

        f = fopen("/usr/lib/bus1-release", "re");
        if (!f)
                return -errno;

        if (fgets(line, sizeof(line), f) == NULL)
                return -errno;

        len = strlen(line);
        if (len < 1)
                return -EINVAL;

        if (line[len - 1] == '\n')
                line[len - 1] = '\0';

        *release = strdup(line);
        if (!*release)
                return -ENOMEM;

        return 0;
}

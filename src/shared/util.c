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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include "util.h"

int hexstr_to_bytes(const char *str, uint8_t *bytes) {
        size_t len;
        unsigned int i;
        char buf[3] = {};

        len = strlen(str);
        if (len % 2)
                return -EINVAL;

        len /= 2;

        for (i = 0; i < len; i++) {
                char *endp;

                memcpy(buf, str + (i * 2), 2);
                bytes[i] = strtoul(buf, &endp, 16);
                if (endp != buf + 2)
                        return -EINVAL;
        }

        return 0;
}

int bytes_to_hexstr(const uint8_t *bytes, size_t len, char **str) {
        char *s;
        unsigned int i;

        s = malloc((len * 2) + 1);
        if (!s)
                return -ENOMEM;

        for(i = 0; i < len; i++)
                sprintf(s + (i * 2), "%02x", bytes[i]);

        s[len * 2] = '\0';
        *str = s;

        return 0;
}

char *escape_hex(const char *in) {
        static const char hex[16] = "0123456789abcdef";
        const char *i;
        char *out, *o;

        out = malloc(strlen(in) * 4 + 1);
        if (!out)
                return NULL;

        for (i = in, o = out; *i; i++) {
                switch (*i) {
                case 'a' ... 'z':
                case 'A' ... 'Z':
                case '0' ... '9':
                case '_':
                case '-':
                case '.':
                        *(o++) = *i;
                        break;

                default:
                        *(o++) = '\\';
                        *(o++) = 'x';
                        *(o++) = hex[(*i & 0xf) >> 4];
                        *(o++) = hex[*i & 0xf];
                        break;
                }
        }

        *o = '\0';

        return out;
}

int child_reap(pid_t *p) {
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

int kernel_cmdline_option(const char *key, char **value) {
        _c_cleanup_(c_fclosep) FILE *f = NULL;
        char line[4096];
        char *s, *e;
        size_t l;

        f = fopen("/proc/cmdline", "re");
        if (!f)
                return -errno;

        if (fgets(line, sizeof(line), f) == NULL)
                return -errno;

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
                return -ENOMEM;

        return true;
}

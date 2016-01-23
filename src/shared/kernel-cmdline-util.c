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
#include <sys/wait.h>

#include <kernel-cmdline-util.h>

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

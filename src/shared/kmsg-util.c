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

#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include <syslog.h>
#include <sys/stat.h>

#include "kmsg-util.h"

static FILE *f;

FILE *kmsg(int level, const char *msg, ...) {
        va_list ap;
        size_t len;

        if (!f) {
                f = fopen("/dev/kmsg", "we");
                if (!f)
                        return NULL;

                setvbuf(f, NULL, _IOFBF, 0);
        }

        if (!msg)
                return f;

        len = strlen(msg);
        if (len < 1)
                return f;

        if (fprintf(f, "<%d>%s[%d]: ", LOG_USER | level, program_invocation_short_name, getpid()) < 0)
                return f;

        va_start(ap, msg);
        vfprintf(f, msg, ap);
        va_end(ap);

        if (msg[len - 1] != '\n')
                fprintf(f, "\n");
        fflush(f);

        return f;
}

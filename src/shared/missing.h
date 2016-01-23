#pragma once
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

#include <sys/syscall.h>

#ifndef HAVE_DECL_GETRANDOM
static inline int getrandom(void *buffer, size_t count, unsigned flags) {
        int r;

        r = syscall(__NR_getrandom, buffer, count, flags);
        if (r < 0) {
                errno = -r;
                return -1;
        }

        return r;
}
#endif

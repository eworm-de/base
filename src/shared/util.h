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

static inline int c_sys_clone(unsigned long flags, void *child_stack) {
#if defined(__s390__) || defined(__CRIS__)
        return (int)syscall(__NR_clone, child_stack, flags);
#else
        return (int)syscall(__NR_clone, flags, child_stack);
#endif
}

int child_reap(pid_t *p);
pid_t service_start(const char *prog);
int bus1_read_release(char **release);
int kernel_cmdline_option(const char *key, char **value);

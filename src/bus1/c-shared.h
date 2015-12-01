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

#include <dirent.h>

#define C_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define C_DEFINE_CLEANUP_FUNC(type, func)      \
        static inline void func##p(type *p) {  \
                if (*p)                        \
                        func(*p);              \
        }                                      \
        struct __useless_struct_to_allow_trailing_semicolon__

#define _c_cleanup_(_x) __attribute__((cleanup(_x)))

static inline void *c_free(void *p) {
        free(p);
        return NULL;
}

static inline void c_freep(void *p) {
        c_free(*(void **)p);
}

static inline int c_close(int fd) {
        if (fd >= 0)
                close(fd);
        return -1;
}

static inline void c_closep(int *fd) {
        c_close(*fd);
}

static inline FILE *c_fclose(FILE *f) {
        if (f)
                fclose(f);
        return NULL;
}

C_DEFINE_CLEANUP_FUNC(FILE *, c_fclose);

static inline DIR *c_closedir(DIR *d) {
        if (d)
                closedir(d);
        return NULL;
}

C_DEFINE_CLEANUP_FUNC(DIR *, c_closedir);

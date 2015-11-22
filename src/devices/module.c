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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <libkmod.h>
#include <sys/prctl.h>

#include "module.h"

static void module_log(void *data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {}

static void *module_thread(void *p) {
        struct kmod_ctx *ctx;
        char *modalias = p;
        struct kmod_module *mod = NULL;
        int r;

        prctl(PR_SET_NAME, (unsigned long) "module");

        ctx = kmod_new(NULL, NULL);
        if (!ctx) {
                r = -ENOMEM;
                goto err;
        }

        kmod_set_log_fn(ctx, module_log, NULL);

        r = kmod_load_resources(ctx);
        if (r < 0)
                goto err;

        r = kmod_module_new_from_name(ctx, modalias, &mod);
        if (r < 0)
                goto err;

        r = kmod_module_probe_insert_module(mod,
                                            KMOD_PROBE_APPLY_BLACKLIST|KMOD_PROBE_IGNORE_COMMAND,
                                            NULL, NULL, NULL, NULL);
err:
        kmod_module_unref(mod);
        kmod_unref(ctx);
        free(modalias);

        return NULL;
}

int module_load(const char *modalias) {
        pthread_attr_t a;
        char *s;
        pthread_t t;
        int r;

        r = pthread_attr_init(&a);
        if (r > 0)
                return -r;

        r = pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        if (r > 0)
                goto finish;

        s = strdup(modalias);
        if (!s)
                return -ENOMEM;

        r = pthread_create(&t, &a, module_thread, s);

finish:
        pthread_attr_destroy(&a);
        return -r;
}

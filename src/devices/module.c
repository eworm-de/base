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

#include <libkmod.h>
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>

#include <org.bus1/c-macro.h>

#include "module.h"

struct work_item {
        struct work_item *previous;
        struct work_item *next;
        const char *modalias;
};

struct work_item *work_item_free(struct work_item *work_item) {
        if (!work_item)
                return NULL;

        assert(!work_item->previous);
        assert(!work_item->next);

        free(work_item);

        return NULL;
}

static int work_item_new(struct work_item **work_itemp, const char *modalias) {
        struct work_item *work_item;
        size_t n_modalias;

        n_modalias = strlen(modalias) + 1;
        work_item = malloc(sizeof(*work_item) + n_modalias);
        if (!work_item)
                return -ENOMEM;
        work_item->modalias = memcpy((void*)(work_item + 1), modalias, n_modalias);

        work_item->previous = NULL;
        work_item->next = NULL;

        *work_itemp = work_item;

        return 0;
}

struct work_item *work_item_pop(Manager *m) {
        struct work_item *work_item;

        assert(m);

        if (!m->work_items_first) {
                assert(!m->work_items_last);
                return NULL;
        } else
                assert(m->work_items_last);

        work_item = m->work_items_first;

        assert(!work_item->previous);

        if (work_item->next)
                work_item->next->previous = NULL;
        else
                m->work_items_last = NULL;

        m->work_items_first = work_item->next;
        work_item->next = NULL;

        return work_item;
}

static void work_item_push(Manager *m, struct work_item *work_item) {
        assert(m);

        if (!work_item)
                return;

        assert(!work_item->previous);
        assert(!work_item->next);

        if (m->work_items_last) {
                m->work_items_last->next = work_item;
                work_item->previous = m->work_items_last;
        } else
                m->work_items_first = work_item;

        m->work_items_last = work_item;
}

static void module_log(void *data, int priority, const char *file, int line, const char *fn, const char *format, va_list args) {}

static void *module_thread(void *p) {
        Manager *m = p;
        struct kmod_ctx *ctx;
        struct work_item *work_item = NULL;
        int r;

        assert(m);

        prctl(PR_SET_NAME, (unsigned long) "module");

        ctx = kmod_new(NULL, NULL);
        if (!ctx)
                return NULL;

        kmod_set_log_fn(ctx, module_log, NULL);

        r = kmod_load_resources(ctx);
        if (r < 0)
                goto finish;

        pthread_mutex_lock(&m->worker_lock);
        work_item = work_item_pop(m);
        if (!work_item)
                m->n_workers --;
        pthread_mutex_unlock(&m->worker_lock);

        while (work_item) {
                struct kmod_list *list = NULL;

                r = kmod_module_new_from_lookup(ctx, work_item->modalias, &list);
                if (r < 0 && r != -ENOSYS)
                        goto finish;

                if (list) {
                        struct kmod_list *l;

                        kmod_list_foreach(l, list) {
                                struct kmod_module *mod;

                                mod = kmod_module_get_module(l);
                                kmod_module_probe_insert_module(mod, KMOD_PROBE_APPLY_BLACKLIST|KMOD_PROBE_IGNORE_COMMAND,
                                                                NULL, NULL, NULL, NULL);
                                kmod_module_unref(mod);
                        }

                        kmod_module_unref_list(list);
                }

                work_item_free(work_item);
                pthread_mutex_lock(&m->worker_lock);
                work_item = work_item_pop(m);
                if (!work_item)
                        m->n_workers --;
                pthread_mutex_unlock(&m->worker_lock);
        }

finish:
        kmod_unref(ctx);
        work_item_free(work_item);
        return NULL;
}

int module_load(Manager *m, const char *modalias) {
        struct work_item *work_item;
        int r;

        assert(m);
        assert(modalias);

        r = work_item_new(&work_item, modalias);
        if (r < 0)
                return r;

        pthread_mutex_lock(&m->worker_lock);
        work_item_push(m, work_item);
        if (m->n_workers < m->max_workers) {
                _c_cleanup_(pthread_attr_destroy) pthread_attr_t a;
                pthread_t t;

                r = pthread_attr_init(&a);
                assert(r == 0); /* On Linux this cannot fail. */

                r = pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
                if (r > 0)
                        goto unlock;

                r = pthread_create(&t, &a, module_thread, m);
                if (r > 0)
                        goto unlock;

                m->n_workers ++;
        }

unlock:
        pthread_mutex_unlock(&m->worker_lock);

        return -r;
}

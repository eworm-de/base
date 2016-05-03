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

enum {
        UEVENT_ACTION_ADD,
        UEVENT_ACTION_CHANGE,
        UEVENT_ACTION_REMOVE,
        UEVENT_ACTION_MOVE,
        UEVENT_ACTION_ONLINE,
        UEVENT_ACTION_OFFLINE,
};

typedef struct Manager Manager;
struct device;
struct uevent_subscription;

struct uevent_subscriptions {
        struct uevent_subscription *head;
        struct uevent_subscription *tail;
        uint64_t seqnum;
};

void uevent_subscription_unlink(struct uevent_subscriptions *uss,
                                struct uevent_subscription *us);
struct uevent_subscription *uevent_subscription_free(struct uevent_subscription *us);
int uevent_sysfs_sync(struct uevent_subscriptions *uss,
                      int sysfd,
                      struct uevent_subscription **us,
                      int (*cb)(void *userdata),
                      void *userdata);
int uevent_subscriptions_init(struct uevent_subscriptions *uss, int sysfd);
void uevent_subscriptions_destroy(struct uevent_subscriptions *uss);

int uevent_subscriptions_dispatch(struct uevent_subscriptions *uss, uint64_t seqnum);
int uevent_subscriptions_dispatch_all(struct uevent_subscriptions *uss);

int uevent_connect(void);
int uevent_receive(Manager *m, struct device **devicep, int *actionp, uint64_t *seqnum);

int uevent_action_from_string(const char *action);

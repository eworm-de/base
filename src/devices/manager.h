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

#include <c-rbtree.h>
#include <sys/epoll.h>

#include "uevent.h"

typedef struct Manager {
        FILE *log;
        int fd_uevent;
        int fd_signal;
        int fd_ep;
        struct epoll_event ep_uevent;
        struct epoll_event ep_signal;
        int devicesfd;
        int sysbusfd;
        int sysclassfd;
        int sysfd;
        int devfd;
        struct uevent_subscriptions uevent_subscriptions;
        struct uevent_subscription subscription_settle;
        bool settled;
        CRBTree devices;
        CRBTree subsystems;
} Manager;

Manager *manager_free(Manager *m);
int manager_new(Manager **manager);

int manager_enumerate(Manager *manager);
int manager_run(Manager *manager);

C_DEFINE_CLEANUP(Manager *, manager_free);

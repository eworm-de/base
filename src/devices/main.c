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

#include <string.h>
#include <sys/capability.h>
#include <sys/prctl.h>

#include <org.bus1/c-macro.h>
#include <org.bus1/b1-identity.h>
#include "shared/kmsg.h"

#include "manager.h"

C_DEFINE_CLEANUP(cap_t, cap_free);

static int privileges_drop(uid_t uid, const cap_value_t *caps, unsigned int n_caps) {
        _c_cleanup_(cap_freep) cap_t cap = NULL;
        static const cap_value_t setpcap = CAP_SETPCAP;

        assert(uid > 0);

        if (setresgid(uid, uid, uid) < 0)
                return -errno;

        /* Retain caps across setresuid(). */
        if (prctl(PR_SET_KEEPCAPS, 1) < 0)
                return -errno;

        if (setresuid(uid, uid, uid) < 0)
                return -errno;

        if (prctl(PR_SET_KEEPCAPS, 0) < 0)
                return -errno;

        cap = cap_get_proc();
        if (!cap)
                return -errno;

        /* Gain CAP_SETPCAP to be able to drop the capability bounding set. */
        if (cap_set_flag(cap, CAP_EFFECTIVE, 1, &setpcap, CAP_SET) < 0)
                return -errno;

        if (cap_set_proc(cap) < 0)
                return -errno;

        for (cap_value_t c = 0;; c++)
                if (cap_drop_bound(c) < 0)
                        break;

        if (cap_clear(cap) < 0)
                return -errno;

        if (cap_set_flag(cap, CAP_EFFECTIVE, n_caps, caps, CAP_SET) < 0 ||
            cap_set_flag(cap, CAP_PERMITTED, n_caps, caps, CAP_SET) < 0)
                return -errno;

        if (cap_set_proc(cap) < 0)
                return -errno;

        return 0;
}

int main(int argc, char **argv) {
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        _c_cleanup_(manager_freep) Manager *m = NULL;
        cap_value_t caps[] = {
                CAP_CHOWN,
                CAP_FOWNER,
                CAP_DAC_OVERRIDE,
                CAP_SYS_MODULE,
        };
        int r;

        log = kmsg(0, NULL);
        if (!log)
                return EXIT_FAILURE;

        r = manager_new(&m);
        if (r < 0) {
                kmsg(LOG_ERR, "Failed to create manager object: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        r = privileges_drop(BUS1_IDENTITY_DEVICES, caps, C_ARRAY_SIZE(caps));
        if (r < 0) {
                kmsg(LOG_ERR, "Failed to drop privileges: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        kmsg(LOG_INFO, "Coldplug, adjust /dev permissions and load kernel modules for current devices.");
        r = manager_enumerate(m);
        if (r < 0) {
                kmsg(LOG_ERR, "Failed to enumerate devices: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        r = manager_run(m);
        if (r < 0) {
                kmsg(LOG_ERR, "Mainloop failed: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <bus1/c-macro.h>
#include <bus1/c-cleanup.h>

#include "util.h"

static int system_shutdown(int cmd) {
        unsigned int i;

        for (i = 0; i < 10; i++) {
                if (mount(NULL, "/bus1", NULL, MS_REMOUNT|MS_RDONLY, NULL) >= 0)
                        break;

                printf("killing all processes\n");
                kill(-1, SIGKILL);
                sleep(1);
        }

        sync();

        return reboot(cmd);
}

int main(int argc, char **argv) {
        struct sigaction sa = {
                .sa_handler = SIG_IGN,
                .sa_flags = SA_NOCLDSTOP|SA_NOCLDWAIT|SA_RESTART,
        };
        pid_t pid_devices = 0;
        _c_cleanup_(c_freep) char *release = NULL;

        umask(0);

        if (sigaction(SIGCHLD, &sa, NULL) < 0)
                return EXIT_FAILURE;

        if (bus1_release(&release) < 0)
                return EXIT_FAILURE;

        pid_devices = service_start("/usr/bin/org.bus1.devices");
        if (pid_devices < 0)
                return EXIT_FAILURE;

        bash_execute(release);

        system_shutdown(RB_POWER_OFF);
        return EXIT_FAILURE;
}

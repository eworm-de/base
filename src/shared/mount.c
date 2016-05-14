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

#include <c-macro.h>
#include <org.bus1/c-shared.h>
#include <sys/mount.h>
#include "mount.h"

int mount_boot(const char *device, const char *directory, unsigned long flags) {
        if (mount(device, directory ?: "/boot", "vfat", flags|MS_NOSUID|MS_NOEXEC|MS_NODEV, "umask=0027") < 0)
                return -errno;

        return 0;
}

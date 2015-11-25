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

int uevent_coldplug(int devfd, int (* cb)(int devfd,
                                          const char *subsystem, const char *devtype,
                                          const char *devname, const char *modalias));
int uevent_connect(void);
int uevent_receive(int sk, char **action, char **subsystem, char **devtype,
                           char **devname, char **modalias);

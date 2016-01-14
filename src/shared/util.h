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

int uuid_parse(const char *str, uint8_t *uuid);
int bytes_to_hexstr(const uint8_t *bytes, size_t len, char **str);
int hexstr_to_bytes(const char *str, uint8_t *bytes);
char *escape_hex(const char *in);
int child_reap(pid_t *p);
pid_t service_start(const char *prog);
int bus1_read_release(char **release);
int kernel_cmdline_option(const char *key, char **value);

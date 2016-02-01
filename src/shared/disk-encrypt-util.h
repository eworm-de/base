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

int encrypt_get_info(FILE *f,
                     char **image_namep,
                     uint64_t *offsetp, uint64_t *sizep,
                     char **crypt_typep, char **data_typep,
                     char **keyp);

int disk_encrypt_setup_device(const char *device, char **devicep, char **image_namep, char **data_typep);

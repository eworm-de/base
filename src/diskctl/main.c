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

#include <bus1/c-macro.h>
#include <bus1/c-shared.h>

#include "image.h"
#include "encrypt.h"
#include "util.h"

int main(int argc, char **argv) {
        const char *verb = argv[1];
        const char *data_file = argv[2];
        const char *name = argv[3];
        int r;

        if (!verb)
                goto fail;

        if (strcmp(verb, "image") == 0) {
                if (argc != 4) {
                        fprintf(stderr, "Usage: %s image <image> <name>\n", program_invocation_short_name);
                        return EXIT_FAILURE;
                }

                if (image_print_info(data_file) >= 0)
                        return EXIT_SUCCESS;

                r = image_append_hash(data_file, name);
                if (r < 0) {
                        fprintf(stderr, "Error writing %s: %s\n", data_file, strerror(-r));
                        return EXIT_FAILURE;
                }

                if (image_print_info(data_file) < 0)
                        return EXIT_FAILURE;

                return EXIT_SUCCESS;

        } else if (strcmp(verb, "encrypt") == 0) {
                if (argc != 4) {
                        fprintf(stderr, "Usage: %s encrypt <device> <name>\n", program_invocation_short_name);
                        return EXIT_FAILURE;
                }

                if (encrypt_print_info(data_file) >= 0)
                        return EXIT_SUCCESS;

                r = encrypt_setup_volume(data_file, name);
                if (r < 0) {
                        fprintf(stderr, "Error writing %s: %s\n", data_file, strerror(-r));
                        return EXIT_FAILURE;
                }

                if (encrypt_print_info(data_file) < 0)
                        return EXIT_FAILURE;

                return EXIT_SUCCESS;
        }

fail:
        fprintf(stderr, "Usage: %s image|encrypt [OPTIONS]\n", program_invocation_short_name);
        return EXIT_FAILURE;
}

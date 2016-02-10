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

#include <org.bus1/c-macro.h>
#include <org.bus1/c-shared.h>
#include "shared/disk-encrypt.h"
#include "shared/disk-sign.h"

#include "encrypt.h"
#include "sign.h"

int main(int argc, char **argv) {
        const char *verb = argv[1];
        int r;

        if (!verb)
                goto fail;

        if (strcmp(verb, "sign") == 0) {
                if (argc == 6) {
                        const char *name = argv[2];
                        const char *type = argv[3];
                        const char *filename_in = argv[4];
                        const char *filename_out = argv[5];

                        r = disk_sign_format_volume(filename_in, filename_out, name, type);
                        if (r < 0) {
                                fprintf(stderr, "Error writing %s: %s\n", filename_out, strerror(-r));
                                return EXIT_FAILURE;
                        }

                        if (disk_sign_print_info(filename_out) < 0)
                                return EXIT_FAILURE;

                        return EXIT_SUCCESS;
                }

                fprintf(stderr, "Usage: %s sign <name> <type> <data file> <image file>\n", program_invocation_short_name);
                return EXIT_FAILURE;

        } else if (strcmp(verb, "encrypt") == 0) {
                if (argc == 5) {
                        const char *name = argv[2];
                        const char *type = argv[3];
                        const char *filename = argv[4];

                        r = disk_encrypt_format_volume(filename, name, type);
                        if (r < 0) {
                                fprintf(stderr, "Error writing %s: %s\n", filename, strerror(-r));
                                return EXIT_FAILURE;
                        }

                        if (disk_encrypt_print_info(filename) < 0)
                                return EXIT_FAILURE;

                        return EXIT_SUCCESS;
                }

        } else if (strcmp(verb, "setup") == 0) {
                if (argc == 3) {
                        const char *filename = argv[2];
                        _c_cleanup_(c_freep) char *device = NULL;
                        _c_cleanup_(c_freep) char *image_name = NULL;
                        _c_cleanup_(c_freep) char *data_type = NULL;

                        r = disk_sign_setup_device(filename, &device, &data_type);
                        if (r >= 0) {
                                printf("Attached signed image %s to device %s.\n", data_type, device);
                                return EXIT_SUCCESS;
                        }

                        r = disk_encrypt_setup_device(filename, &device, &image_name, &data_type);
                        if (r >= 0) {
                                printf("Attached encrypted image %s (%s) to device %s.\n", image_name, data_type, device);
                                return EXIT_SUCCESS;
                        }

                        fprintf(stderr, "Unable to set up image %s: %s\n", filename, strerror(-r));
                        return EXIT_FAILURE;
                }

                fprintf(stderr, "Usage: %s setup <image>\n", program_invocation_short_name);
                return EXIT_FAILURE;

        } else if (strcmp(verb, "info") == 0) {
                if (argc == 3) {
                        const char *filename = argv[2];

                        if (disk_sign_print_info(filename) >= 0)
                                return EXIT_SUCCESS;

                        if (disk_encrypt_print_info(filename) >= 0)
                                return EXIT_SUCCESS;

                        fprintf(stderr, "Unable to read information from image %s\n", filename);
                        return EXIT_FAILURE;
                }

                fprintf(stderr, "Usage: %s info <image>\n", program_invocation_short_name);
                return EXIT_FAILURE;
        }

fail:
        fprintf(stderr,
                "Usage: %s [VERB] [OPTIONS]\n"
                " sign - create a signed data device/image\n"
                " encrypt - create an empty encrypted device/image\n"
                " setup - attach a device/image to a mapping device\n"
                " info - print metadata info for a device/image\n",
                program_invocation_short_name);

        return EXIT_FAILURE;
}

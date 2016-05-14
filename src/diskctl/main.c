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
#include <getopt.h>
#include <string.h>
#include "shared/disk-encrypt.h"
#include "shared/disk-sign.h"
#include "encrypt.h"
#include "sign.h"

static int verb_encrypt(int argc, char **argv) {
        static const struct option options[] = {
                { "help", no_argument,       NULL, 'h' },
                { "name", required_argument, NULL, 'n' },
                { "type", required_argument, NULL, 't' },
                {}
        };
        int c;
        const char *name = NULL;
        const char *type = NULL;
        const char *filename = NULL;
        uint8_t recovery_key[32];
        uint64_t recovery_key_size;
        int r;

        while ((c = getopt_long(argc, argv, "hnt", options, NULL)) >= 0) {
                switch (c) {
                case 'h':
                        fprintf(stderr, "Usage: %s encrypt <name> <type> <file>\n", program_invocation_short_name);
                        return 0;

                case 'n':
                        name = optarg;
                        break;

                case 't':
                        type = optarg;
                        break;

                default:
                        return -EINVAL;
                }
        }

        if (!name || !type || !argv[optind])
                return -EINVAL;

        filename = argv[optind];

        r = disk_encrypt_format_volume(filename,
                                       name,
                                       type,
                                       recovery_key,
                                       &recovery_key_size);
        if (r < 0) {
                fprintf(stderr, "Error writing %s: %s\n", filename, strerror(-r));
                return r;
        }

        if (disk_encrypt_print_info(filename) < 0)
                return r;

        disk_encrypt_print_recovery(recovery_key, recovery_key_size);

        return 0;
}

static int verb_info(int argc, char **argv) {
        static const struct option options[] = {
                { "help", no_argument, NULL, 'h' },
                {}
        };
        int c;
        const char *filename;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                case 'h':
                        fprintf(stderr, "Usage: %s info <image>\n", program_invocation_short_name);
                        return 0;

                default:
                        return -EINVAL;
                }
        }

        if (!argv[optind])
                return -EINVAL;

        filename = argv[optind];

        if (disk_sign_print_info(filename) >= 0)
                return 0;

        if (disk_encrypt_print_info(filename) >= 0)
                return 0;

        fprintf(stderr, "Unable to read information from image %s\n", filename);
        return -EIO;
}

static int verb_setup(int argc, char **argv) {
        static const struct option options[] = {
                { "help", no_argument, NULL, 'h' },
                {}
        };
        int c;
        const char *filename;
        _c_cleanup_(c_freep) char *device = NULL;
        _c_cleanup_(c_freep) char *image_name = NULL;
        _c_cleanup_(c_freep) char *data_type = NULL;
        int r;

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
                switch (c) {
                case 'h':
                        fprintf(stderr, "Usage: %s setup <image>\n", program_invocation_short_name);
                        return 0;

                default:
                        return -EINVAL;
                }
        }

        if (!argv[optind])
                return -EINVAL;

        filename = argv[optind];

        r = disk_sign_setup_device(filename, &device, &data_type);
        if (r >= 0) {
                printf("Attached signed image %s to device %s.\n", data_type, device);
                return 0;
        }

        r = disk_encrypt_setup_device(filename, &device, &image_name, &data_type);
        if (r >= 0) {
                printf("Attached encrypted image %s (%s) to device %s.\n", image_name, data_type, device);
                return 0;
        }

        fprintf(stderr, "Unable to setup image %s\n", filename);
        return -EIO;
}

static int verb_sign(int argc, char **argv) {
        static const struct option options[] = {
                { "help", no_argument,       NULL, 'h' },
                { "name", required_argument, NULL, 'n' },
                { "type", required_argument, NULL, 't' },
                {}
        };
        int c;
        const char *name = NULL;
        const char *type = NULL;
        const char *filename_in = NULL;
        const char *filename_out = NULL;
        int r;

        while ((c = getopt_long(argc, argv, "hnt", options, NULL)) >= 0) {
                switch (c) {
                case 'h':
                        printf("Usage: %s sign --name=<name> --type=<type> <data file> <image file>\n", program_invocation_short_name);
                        return 0;

                case 'n':
                        name = optarg;
                        break;

                case 't':
                        type = optarg;
                        break;

                default:
                        return -EINVAL;
                }
        }

        if (!name || !type || !argv[optind] || !argv[optind + 1])
                return -EINVAL;

        filename_in = argv[optind];
        filename_out = argv[optind + 1];

        r = disk_sign_format_volume(filename_in, filename_out, name, type);
        if (r < 0) {
                fprintf(stderr, "Error writing %s: %s\n", filename_out, strerror(-r));
                return r;
        }

        if (disk_sign_print_info(filename_out) < 0)
                return r;

        return 0;
}

int main(int argc, char **argv) {
        static const struct option options[] = {
                { "help",    no_argument, NULL, 'h' },
                { "version", no_argument, NULL, 'V' },
                {}
        };
        int c;
        static const struct {
                const char *verb;
                const char *info;
                int (*fn)(int argc, char **argv);
        } verbs[] = {
                { "encrypt", "create an empty encrypted device/image",    verb_encrypt },
                { "info",    "print metadata info for a device/image",    verb_info },
                { "setup",   "attach a device/image to a mapping device", verb_setup },
                { "sign",    "create a signed data device/image",         verb_sign },
        };
        const char *verb;
        int r = -EINVAL;

        while ((c = getopt_long(argc, argv, "+hV", options, NULL)) >= 0) {
                switch (c) {
                case 'h':
                        printf("Usage: %s [VERB] [OPTIONS]\n", program_invocation_short_name);
                        for (size_t i = 0; i < C_ARRAY_SIZE(verbs); i++)
                                printf(" %s - %s\n", verbs[i].verb, verbs[i].info);

                        r = 0;
                        goto finish;

                case 'V':
                        printf(VERSION "\n");
                        goto finish;

                default:
                        goto finish;
                }
        }

        if (!argv[optind])
                goto finish;

        verb = argv[optind];

        argc -= optind;
        argv += optind;
        optind = 0;

        for (size_t i = 0; i < C_ARRAY_SIZE(verbs); i++) {
                if (strcmp(verb, verbs[i].verb) != 0)
                        continue;

                r  = verbs[i].fn(argc, argv);
                break;
        }

finish:
        if (r < 0) {
                fprintf(stderr, "Error: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

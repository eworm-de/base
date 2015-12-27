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
#include <sys/prctl.h>

#include "kmsg.h"
#include "util.h"

enum {
        ARG_0,
        ARG_COMM,
        ARG_PID,
        ARG_UID,
        ARG_GID,
        ARG_SIGNAL,
        ARG_MAX
};

static ssize_t copy_file(int fd_in, int fd_out) {
        ssize_t size = 0;

        for (;;) {
                char buf[64 * 1024];
                ssize_t in;
                ssize_t n;
                char *p;

                in = read(fd_in, buf, sizeof(buf));
                if (in == 0)
                        break;
                if (in < 0) {
                        if (errno == EINTR)
                                continue;

                        return -errno;
                }

                p = buf;
                n = in;
                while (n > 0) {
                        ssize_t out;

                        out = write(fd_out, p, n);
                        if (out == 0)
                                return -EIO;
                        if (out < 0) {
                                if (errno == EINTR)
                                        continue;

                                return -errno;
                        }

                        p += out;
                        n -= out;

                        size += out;
                }
        }

        return size;
}

int main(int argc, char **argv) {
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        pid_t pid;
        uid_t uid;
        gid_t gid;
        int sig;
        const char *comm;
        _c_cleanup_(c_closep) int fd = -1;
        _c_cleanup_(c_freep) char *core_file = NULL;
        _c_cleanup_(c_freep) char *proc_file = NULL;
        size_t size;

        prctl(PR_SET_DUMPABLE, 0);

        if (argc != ARG_MAX)
                return EXIT_FAILURE;

        comm = argv[ARG_COMM];
        pid = strtoull(argv[ARG_PID], NULL, 10);
        uid = strtoull(argv[ARG_UID], NULL, 10);
        gid = strtoull(argv[ARG_GID], NULL, 10);
        sig = strtoul(argv[ARG_SIGNAL], NULL, 10);

        log = kmsg(LOG_INFO, "%s[%d] caught signal %d (%s) uid=%d:%d", comm, pid, sig, strsignal(sig), uid, gid);

        /* Allocate an unlinked file on disk. */
        fd = open("/var", O_TMPFILE|O_RDWR|O_CLOEXEC, 0600);
        if (fd < 0)
                return EXIT_FAILURE;

        /* Stream the coredump from the kernel to the file. */
        size = copy_file(STDIN_FILENO, fd);
        if (size <= 0)
                return EXIT_FAILURE;

        /* Store the coredump on disk. */
        if (access("/var/crash", W_OK) < 0)
                return EXIT_SUCCESS;

        if (uid > 0) {
                if (asprintf(&core_file, "/var/crash/%s-%d", comm, uid) < 0)
                        return EXIT_FAILURE;
        } else {
                if (asprintf(&core_file, "/var/crash/%s", comm) < 0)
                        return EXIT_FAILURE;
        }

        if (asprintf(&proc_file, "/proc/self/fd/%d", fd) < 0)
                return EXIT_FAILURE;

        unlink(core_file);
        if (linkat(AT_FDCWD, proc_file, AT_FDCWD, core_file, AT_SYMLINK_FOLLOW) < 0)
                return EXIT_FAILURE;

        kmsg(LOG_INFO, "%s[%d] wrote coredump to %s (%zd kB).", comm, pid, core_file, size / 1024);

        return EXIT_SUCCESS;
}

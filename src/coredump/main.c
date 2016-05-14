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
#include <dwarf.h>
#include <elfutils/libdwfl.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <org.bus1/b1-identity.h>
#include <org.bus1/c-shared.h>
#include "shared/kmsg.h"
#include "shared/string.h"

struct cb_data {
        Dwfl *dwfl;
        Elf *elf;
        unsigned int thread;
        unsigned int frame;
};

static int frame_cb(Dwfl_Frame *frame, void *userdata) {
        struct cb_data *c = userdata;
        Dwarf_Addr pc, pc_adjusted, bias = 0;
        _c_cleanup_(c_freep) Dwarf_Die *scopes = NULL;
        const char *fname = NULL, *symbol = NULL;
        Dwfl_Module *module;
        bool is_activation;

        if (c->frame >= 32)
                return DWARF_CB_ABORT;

        if (!dwfl_frame_pc(frame, &pc, &is_activation))
                return DWARF_CB_ABORT;

        pc_adjusted = pc - (is_activation ? 0 : 1);
        module = dwfl_addrmodule(c->dwfl, pc_adjusted);
        if (module) {
                Dwarf_Die *cudie;
                int n;

                cudie = dwfl_module_addrdie(module, pc_adjusted, &bias);
                if (cudie) {
                        n = dwarf_getscopes(cudie, pc_adjusted - bias, &scopes);
                        for (Dwarf_Die *s = scopes; s < scopes + n; s++) {
                                Dwarf_Attribute *a, space;

                                if (dwarf_tag(s) != DW_TAG_subprogram &&
                                    dwarf_tag(s) != DW_TAG_inlined_subroutine &&
                                    dwarf_tag(s) != DW_TAG_entry_point)
                                        continue;

                                a = dwarf_attr_integrate(s, DW_AT_MIPS_linkage_name, &space);
                                if (!a)
                                        a = dwarf_attr_integrate(s, DW_AT_linkage_name, &space);
                                if (a)
                                        symbol = dwarf_formstring(a);
                                if (!symbol)
                                        symbol = dwarf_diename(s);

                                if (symbol)
                                        break;
                        }
                }

                if (!symbol)
                        symbol = dwfl_module_addrname(module, pc_adjusted);

                fname = dwfl_module_info(module, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        }

        kmsg(LOG_INFO, "#%-2u 0x%016" PRIx64 " %s (%s)\n", c->frame, (uint64_t)pc, symbol ?: "n/a", fname ?: "n/a");
        c->frame ++;

        return DWARF_CB_OK;
}

static int thread_cb(Dwfl_Thread *thread, void *userdata) {
        struct cb_data *c = userdata;
        pid_t tid;

        if (c->thread >= 32)
                return DWARF_CB_ABORT;

        tid = dwfl_thread_tid(thread);
        kmsg(LOG_INFO, "Task %d:\n", tid);

        c->frame = 0;
        if (dwfl_thread_getframes(thread, frame_cb, c) < 0)
                return DWARF_CB_ABORT;

        c->thread ++;

        return DWARF_CB_OK;
}

static int stacktrace_log(int fd, char **error) {
        static const Dwfl_Callbacks cbs = {
                .find_elf = dwfl_build_id_find_elf,
                .find_debuginfo = dwfl_standard_find_debuginfo,
        };
        struct cb_data c = {};
        int r = -EINVAL;

        if (lseek(fd, 0, SEEK_SET) < 0)
                return -errno;

        elf_version(EV_CURRENT);
        c.elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
        if (!c.elf)
                goto finish;

        c.dwfl = dwfl_begin(&cbs);
        if (!c.dwfl)
                goto finish;

        if (dwfl_core_file_report(c.dwfl, c.elf, NULL) < 0)
                goto finish;

        if (dwfl_report_end(c.dwfl, NULL, NULL) != 0)
                goto finish;

        if (dwfl_core_file_attach(c.dwfl, c.elf) < 0)
                goto finish;

        if (dwfl_getthreads(c.dwfl, thread_cb, &c) < 0)
                goto finish;

        r = 0;
finish:
        if (c.dwfl)
                dwfl_end(c.dwfl);
        if (c.elf)
                elf_end(c.elf);

        if (r < 0 && error) {
                *error = strdup(dwfl_errmsg(dwfl_errno()));
                if (!*error)
                        return -ENOMEM;
        }

        return r;
}

int main(int argc, char **argv) {
        enum {
                ARG_0,
                ARG_PID,
                ARG_UID,
                ARG_GID,
                ARG_SIGNAL,
                ARG_COMM,
                ARG_MAX
        };
        _c_cleanup_(c_fclosep) FILE *log = NULL;
        pid_t pid;
        uid_t uid;
        gid_t gid;
        int sig;
        const char *comm;
        _c_cleanup_(c_closep) int fd = -1;
        int64_t core_size = 0;
        _c_cleanup_(c_closep) int dfd = -1;
        _c_cleanup_(c_freep) char *error = NULL;

        prctl(PR_SET_DUMPABLE, 0);

        if (argc < ARG_MAX)
                return EXIT_FAILURE;

        pid = strtoull(argv[ARG_PID], NULL, 10);
        uid = strtoull(argv[ARG_UID], NULL, 10);
        gid = strtoull(argv[ARG_GID], NULL, 10);
        sig = strtoul(argv[ARG_SIGNAL], NULL, 10);
        comm = argv[ARG_COMM];

        log = kmsg(LOG_INFO, "%s[%d] caught signal %d (%s) uid=%d:%d", comm, pid, sig, strsignal(sig), uid, gid);

        /* Allocate an unlinked file on disk. */
        fd = open("/var", O_TMPFILE|O_RDWR|O_CLOEXEC, 0660);
        if (fd < 0)
                return EXIT_FAILURE;

        if (fchownat(fd, "", BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP, AT_EMPTY_PATH) < 0)
                return EXIT_FAILURE;

        /* Stream the coredump from the kernel to the file. */
        for (;;) {
                ssize_t n;

                n = splice(STDIN_FILENO, NULL, fd, NULL, 1024 * 1024 * 1024, 0);
                if (n < 0)
                        return EXIT_FAILURE;

                if (n == 0)
                        break;

                core_size += n;
        }

        /* Safe coredump to disk. */
        dfd = openat(AT_FDCWD, "/var/coredump", O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC|O_PATH);
        if (dfd >= 0) {
                _c_cleanup_(c_freep) char *comm_escaped = NULL;
                _c_cleanup_(c_freep) char *proc_file = NULL;
                _c_cleanup_(c_freep) char *core_file = NULL;

                if (fchownat(dfd, ".", BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP, 0) < 0)
                        return EXIT_FAILURE;

                if (fchmodat(dfd, ".", 0750, 0) < 0)
                        return EXIT_FAILURE;

                comm_escaped = escape_hex(comm);
                if (!comm_escaped)
                        return EXIT_FAILURE;

                if (asprintf(&core_file, "%d-%s", uid, comm_escaped) < 0)
                        return EXIT_FAILURE;

                if (asprintf(&proc_file, "/proc/self/fd/%d", fd) < 0)
                        return EXIT_FAILURE;

                if (unlinkat(dfd, core_file, 0) < 0 && errno != ENOENT)
                        return EXIT_FAILURE;

                if (linkat(AT_FDCWD, proc_file, dfd, core_file, AT_SYMLINK_FOLLOW) < 0)
                        return EXIT_FAILURE;

                kmsg(LOG_INFO, "%s[%d] wrote coredump to /var/coredump/%s (%" PRIu64 " kB).", comm, pid, core_file, core_size / 1024);
        }

        /* Drop privileges. */
        if (setresgid(BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP) < 0 ||
            setresuid(BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP, BUS1_IDENTITY_COREDUMP) < 0)
                return EXIT_FAILURE;

        if (stacktrace_log(fd, &error) < 0)
                kmsg(LOG_INFO, "Unable to generate stack trace: %s.", error);

        return EXIT_SUCCESS;
}

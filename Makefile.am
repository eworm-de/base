# ------------------------------------------------------------------------------
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

SUBDIRS = .

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# Keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

CLEANFILES = $(BUILT_SOURCES)
bin_PROGRAMS =
noinst_PROGRAMS =
noinst_LIBRARIES =

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I $(top_srcdir)/src \
	-I $(top_srcdir)/src/shared \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
BUS1_CFLAGS = -I $(top_srcdir)/../libbus1/src
BUS1_LIBS = -L $(top_srcdir)/../libbus1/.libs -lbus1

# ------------------------------------------------------------------------------
noinst_LIBRARIES += \
	libshared.a

libshared_a_SOURCES = \
	src/shared/aeswrap-util.h \
	src/shared/aeswrap-util.c \
	src/shared/disk-encrypt-util.h \
	src/shared/disk-encrypt-util.c \
	src/shared/disk-sign-hash-tree-util.h \
	src/shared/disk-sign-hash-tree-util.c \
	src/shared/disk-sign-util.h \
	src/shared/disk-sign-util.c \
	src/shared/disk-util.h \
	src/shared/disk-util.c \
	src/shared/file-util.h \
	src/shared/file-util.c \
	src/shared/kernel-cmdline-util.h \
	src/shared/kernel-cmdline-util.c \
	src/shared/kmsg-util.h \
	src/shared/kmsg-util.c \
	src/shared/mount-util.h \
	src/shared/mount-util.c \
	src/shared/process-util.h \
	src/shared/process-util.c \
	src/shared/string-util.h \
	src/shared/string-util.c \
	src/shared/tmpfs-root-util.h \
	src/shared/tmpfs-root-util.c \
	src/shared/uuid-util.h \
	src/shared/uuid-util.c

libshared_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(GCRYPT_CFLAGS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.init

org_bus1_init_SOURCES = \
	src/init/main.c

org_bus1_init_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS)

org_bus1_init_LDADD = \
	libshared.a \
	$(BUS1_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.rdinit

org_bus1_rdinit_SOURCES = \
	src/devices/sysfs.h \
	src/devices/sysfs.c \
	src/rdinit/disk-gpt.h \
	src/rdinit/disk-gpt.c \
	src/rdinit/sysctl.h \
	src/rdinit/sysctl.c \
	src/rdinit/main.c

org_bus1_rdinit_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(KMOD_CFLAGS) \
	$(GCRYPT_CFLAGS)

org_bus1_rdinit_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(KMOD_LIBS) \
	$(GCRYPT_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.devices

org_bus1_devices_SOURCES = \
	src/org.bus1/b1-identity.h \
	src/devices/uevent.h \
	src/devices/uevent.c \
	src/devices/sysfs.h \
	src/devices/sysfs.c \
	src/devices/permissions.h \
	src/devices/permissions.c \
	src/devices/module.h \
	src/devices/module.c \
	src/devices/main.c

org_bus1_devices_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(CAP_LIBS) \
	$(KMOD_CFLAGS) \
	-pthread

org_bus1_devices_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(CAP_CFLAGS) \
	$(KMOD_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.activator

org_bus1_activator_SOURCES = \
	src/org.bus1/b1-identity.h \
	src/activator/service.h \
	src/activator/service.c \
	src/activator/main.c

org_bus1_activator_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS)

org_bus1_activator_LDADD = \
	libshared.a \
	$(BUS1_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.coredump

org_bus1_coredump_SOURCES = \
	src/coredump/main.c

org_bus1_coredump_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS)

org_bus1_coredump_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(ELFUTILS_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.diskctl

org_bus1_diskctl_SOURCES = \
	src/org.bus1/b1-meta-header.h \
	src/org.bus1/b1-disk-encrypt-header.h \
	src/org.bus1/b1-disk-sign-header.h \
	src/diskctl/encrypt.h \
	src/diskctl/encrypt.c \
	src/diskctl/sign.h \
	src/diskctl/sign.c \
	src/diskctl/main.c

org_bus1_diskctl_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(GCRYPT_CFLAGS)

org_bus1_diskctl_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(GCRYPT_LIBS)

# ------------------------------------------------------------------------------
.PHONY: install-tree
install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree

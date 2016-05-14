# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgincludedir=$(includedir)/org.bus1

CLEANFILES = $(BUILT_SOURCES)
bin_PROGRAMS =
pkginclude_HEADERS =
noinst_PROGRAMS =
noinst_LIBRARIES =

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
noinst_LIBRARIES += \
	libshared.a

libshared_a_SOURCES = \
	src/shared/aeswrap.h \
	src/shared/aeswrap.c \
	src/shared/disk-encrypt.h \
	src/shared/disk-encrypt.c \
	src/shared/disk-sign-hash-tree.h \
	src/shared/disk-sign-hash-tree.c \
	src/shared/disk-sign.h \
	src/shared/disk-sign.c \
	src/shared/disk.h \
	src/shared/disk.c \
	src/shared/file.h \
	src/shared/file.c \
	src/shared/kernel-cmdline.h \
	src/shared/kernel-cmdline.c \
	src/shared/kmsg.h \
	src/shared/kmsg.c \
	src/shared/mount.h \
	src/shared/mount.c \
	src/shared/process.h \
	src/shared/process.c \
	src/shared/string.h \
	src/shared/string.c \
	src/shared/tmpfs-root.h \
	src/shared/tmpfs-root.c \
	src/shared/uuid.h \
	src/shared/uuid.c

libshared_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(OPENSSL_CFLAGS)

# ------------------------------------------------------------------------------
pkginclude_HEADERS = \
	src/org.bus1/b1-disk-encrypt-header.h \
	src/org.bus1/b1-disk-sign-header.h \
	src/org.bus1/b1-identity.h \
	src/org.bus1/b1-meta-header.h

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
	org.bus1.devices

org_bus1_devices_SOURCES = \
	src/org.bus1/b1-identity.h \
	src/devices/uevent.h \
	src/devices/uevent.c \
	src/devices/sysfs.h \
	src/devices/sysfs.c \
	src/devices/permissions.h \
	src/devices/permissions.c \
	src/devices/manager.h \
	src/devices/manager.c \
	src/devices/module.h \
	src/devices/module.c \
	src/devices/main.c \
	src/devices/device.h \
	src/devices/device.c

org_bus1_devices_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(CAP_CFLAGS) \
	$(CRBTREE_CFLAGS) \
	$(KMOD_CFLAGS) \
	-pthread

org_bus1_devices_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(CAP_LIBS) \
	$(CRBTREE_LIBS) \
	$(KMOD_LIBS)

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
	$(OPENSSL_CFLAGS)

org_bus1_diskctl_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(OPENSSL_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.login

org_bus1_login_SOURCES = \
	src/login/main.c

org_bus1_login_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS)

org_bus1_login_LDADD = \
	libshared.a \
	$(BUS1_LIBS)

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
	src/rdinit/dev.h \
	src/rdinit/dev.c \
	src/rdinit/disk-gpt.h \
	src/rdinit/disk-gpt.c \
	src/rdinit/sysctl.h \
	src/rdinit/sysctl.c \
	src/rdinit/main.c

# FIXME: use org.bus1.devices
org_bus1_rdinit_SOURCES += \
	src/devices/sysfs.h \
	src/devices/sysfs.c

org_bus1_rdinit_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(KMOD_CFLAGS) \
	$(OPENSSL_CFLAGS)

org_bus1_rdinit_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(KMOD_LIBS) \
	$(OPENSSL_LIBS)

# ------------------------------------------------------------------------------
install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree

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
BUS1_LIBS = -L $(top_srcdir)/../libbus1/.libs

# ------------------------------------------------------------------------------
noinst_LIBRARIES += \
	libshared.a

libshared_a_SOURCES = \
	src/shared/kmsg.h \
	src/shared/kmsg.c \
	src/shared/tmpfs-root.h \
	src/shared/tmpfs-root.c \
	src/shared/util.h \
	src/shared/util.c

libshared_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.init

org_bus1_init_SOURCES = \
	src/init/sysctl.h \
	src/init/sysctl.c \
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
	src/rdinit/main.c

org_bus1_rdinit_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUS1_CFLAGS) \
	$(KMOD_CFLAGS) \
	$(BLKID_CFLAGS)

org_bus1_rdinit_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(KMOD_LIBS) \
	$(BLKID_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.devices

org_bus1_devices_SOURCES = \
	src/bus1/b1-identity.h \
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
	$(KMOD_CFLAGS) \
	-pthread

org_bus1_devices_LDADD = \
	libshared.a \
	$(BUS1_LIBS) \
	$(KMOD_LIBS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.activator

org_bus1_activator_SOURCES = \
	src/bus1/b1-identity.h \
	src/activator/service.h \
	src/activator/service.c \
	src/activator/main.c

org_bus1_activator_CFLAGS = \
	$(BUS1_CFLAGS) \
	$(AM_CFLAGS)

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
	$(BUS1_LIBS)

# ------------------------------------------------------------------------------
.PHONY: install-tree
install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree

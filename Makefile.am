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

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.devices

org_bus1_devices_SOURCES = \
	src/bus1/bus1-platform.h \
	src/devices/uevent.h \
	src/devices/uevent.c \
	src/devices/permissions.h \
	src/devices/permissions.c \
	src/devices/module.h \
	src/devices/module.c \
	src/devices/main.c

org_bus1_devices_CFLAGS = \
	$(AM_CFLAGS) \
	$(KMOD_CFLAGS) \
	-pthread

org_bus1_devices_LDADD = \
	$(KMOD_LIBS)

# ------------------------------------------------------------------------------
.PHONY: install-tree
install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree

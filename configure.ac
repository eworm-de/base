# ------------------------------------------------------------------------------
# versions

AC_PREREQ([2.64])
AC_INIT([base],
        [1],
        [http://www.github.com/bus1/base],
        [base],
        [http://www.github.com/bus1/base])

# ------------------------------------------------------------------------------
# autotools

AC_CONFIG_SRCDIR([src/org.bus1/b1-identity.h])
AC_CONFIG_MACRO_DIR([build/m4])
AC_CONFIG_HEADERS([build/config.h])
AC_CONFIG_AUX_DIR([build/build-aux])

# suppress conflicting defaults
CFLAGS=${CFLAGS:-""}
CXXFLAGS=${CXXFLAGS:-""}

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AM_MAINTAINER_MODE([enable])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects parallel-tests])
AM_SILENT_RULES([yes])
AC_CANONICAL_HOST
AC_DEFINE_UNQUOTED([CANONICAL_HOST], "$host", [Canonical host string.])
AC_PROG_CC_C99
AC_PROG_RANLIB

m4_pattern_forbid([^_?PKG_[A-Z_]+$],[*** pkg.m4 missing, please install pkg-config])

# ------------------------------------------------------------------------------
# toolchain

OUR_CFLAGS=${OUR_CFLAGS:-$(echo \
        -g \
        -Og \
        -pipe \
        -ftrapv \
        -ffast-math \
        -fno-common \
        -fdiagnostics-show-option \
        -fno-strict-aliasing \
        -fvisibility=hidden \
        -ffunction-sections \
        -fdata-sections \
        -fstack-protector \
        -fstack-protector-strong \
        -fPIC \
        --param=ssp-buffer-size=4 \
        -Wall \
        -Wextra \
        -Wno-inline \
        -Wundef \
        -Wformat=2 \
        -Wformat-security \
        -Wformat-nonliteral \
        -Wsign-compare \
        -Wmissing-include-dirs \
        -Wold-style-definition \
        -Wpointer-arith \
        -Winit-self \
        -Wdeclaration-after-statement \
        -Wfloat-equal \
        -Wmissing-prototypes \
        -Wstrict-prototypes \
        -Wredundant-decls \
        -Wmissing-declarations \
        -Wmissing-noreturn \
        -Wshadow \
        -Wendif-labels \
        -Wstrict-aliasing=2 \
        -Wwrite-strings \
        -Wno-long-long \
        -Wno-overlength-strings \
        -Wno-unused-parameter \
        -Wno-missing-field-initializers \
        -Wno-unused-result \
        -Werror=overflow \
        -Wdate-time \
        -Wnested-externs)}

OUR_CPPFLAGS=${OUR_CPPFLAGS:-$(echo \
        -Wp,-D_FORTIFY_SOURCE=2 \
        -Wno-typedef-redefinition \
        -Wno-gnu-variable-sized-type-not-at-end)}

OUR_LDFLAGS=${OUR_LDFLAGS:-$(echo \
        -Wl,--as-needed \
        -Wl,--no-undefined \
        -Wl,--gc-sections \
        -Wl,-z,relro \
        -Wl,-z,now \
        -pie)}

AC_SUBST(OUR_CFLAGS)
AC_SUBST(OUR_CPPFLAGS)
AC_SUBST(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# dependencies

AC_ARG_ENABLE(static, AS_HELP_STRING([--enable-static], [enable temporary static library linking]))
AS_IF([test "$enable_static" = "yes"], [
        # temporary hack until we do proper shared library linking
        BUS1_CFLAGS="-I../libbus1/src"
        BUS1_LIBS="-L../libbus1 -lbus1"
        AC_SUBST(BUS1_CFLAGS)
        AC_SUBST(BUS1_LIBS)
], [
        PKG_CHECK_MODULES(BUS1, [libbus1],
                [AC_DEFINE(HAVE_BUS1, 1, [Define if libbus1 is available])],
                AC_MSG_ERROR([*** libbus1 library not found]))
])

PKG_CHECK_MODULES(CAP, [libcap],
        [AC_DEFINE(HAVE_LIBCAP, 1, [Define if libcap is available])],
        AC_MSG_ERROR([*** libcap not found]))

PKG_CHECK_MODULES(KMOD, [libkmod],
        [AC_DEFINE(HAVE_KMOD, 1, [Define if kmod is available])],
        AC_MSG_ERROR([*** libkmod not found]))

PKG_CHECK_MODULES(OPENSSL, [libcrypto],
        [AC_DEFINE(HAVE_OPENSSL, 1, [Define if openssl is available])],
        AC_MSG_ERROR([*** openssl not found]))

AC_CHECK_LIB([dw], [dwfl_core_file_attach],
        [AC_DEFINE(HAVE_ELFUTILS, 1, [Define if elfutils is available])],
        AC_MSG_ERROR([*** elfutils library not found]))
ELFUTILS_LIBS="-lelf -ldw"
AC_SUBST(ELFUTILS_LIBS)

# ------------------------------------------------------------------------------
# report

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        prefix:                 ${prefix}
        exec_prefix:            ${exec_prefix}
        includedir:             ${includedir}

        CFLAGS:                 ${OUR_CFLAGS} ${CFLAGS}
        CPPFLAGS:               ${OUR_CPPFLAGS} ${CPPFLAGS}
        LDFLAGS:                ${OUR_LDFLAGS} ${LDFLAGS}
])

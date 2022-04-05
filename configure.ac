AC_INIT([tcpxfer], [1.0], [matthew@ife.onl])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Wno-extra-portability -Werror subdir-objects])

LT_INIT
AC_PROG_CC
AC_CONFIG_HEADERS([config.ac.h])

PKG_CHECK_MODULES([GSL], gsl)

CFLAGS+=" -std=c99"

AC_SUBST([AM_CPPFLAGS], [-D_GNU_SOURCE])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

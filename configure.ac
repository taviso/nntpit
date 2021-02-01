dnl nntpit: reddit2nntp gateway
dnl 
dnl Uses portions of nntpsink, Copyright (c) 2011-2014 Felicity Tarnell.
dnl

AC_PREREQ([2.68])
AC_INIT([nntpit], [T.1.0], [taviso@gmail.com])
AC_CONFIG_SRCDIR([nntpit.c])
AC_CONFIG_HEADERS([setup.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_MEMBER([struct sockaddr_in.sin_len],
	AC_DEFINE([HAVE_SIN_LEN], 1, [Define if struct sockaddr_in has a sin_len member]),
	[],
	[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]
)

# Checks for libraries.
AC_ARG_ENABLE([ssl],
	      [AS_HELP_STRING([--disable-ssl], [don't use SSL])],
	      [if test "$enableval" = yes; then
		       use_ssl=yes
	       else
		       use_ssl=no
	       fi],
	      [use_ssl=yes])

if test "$use_ssl" = yes; then
	AC_CHECK_LIB([ssl], [SSL_CTX_new],
	     [LIBS="$LIBS -lssl"
	      AC_DEFINE([HAVE_OPENSSL], 1, [Define if OpenSSL is present])
	     ])
fi

AC_CHECK_LIB([ev], [ev_run], [], [AC_MSG_ERROR([cannot find libev])])
AC_CHECK_HEADER([ev.h], [], [AC_MSG_ERROR([cannot find ev.h])])

PKG_CHECK_MODULES([glib], [glib-2.0])
PKG_CHECK_MODULES([curl], [libcurl])
PKG_CHECK_MODULES([json], [json-c])

AC_CHECK_LIB([pthread], [pthread_create], [LIBS="$LIBS -lpthread"])
AC_CHECK_HEADERS([inttypes.h stdint.h])

AC_CHECK_FUNCS([strndup strlcpy strlcat setproctitle arc4random fdatasync])

AC_SUBST(EXTRA_SRCS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

#
# This file is part of nntpit, https://github.com/taviso/nntpit
#
# Based on nntpsink, Copyright (c) 2011-2014 Felicity Tarnell.
#

AM_CFLAGS	= $(curl_CFLAGS) $(glib_CFLAGS) $(json_CFLAGS) -ggdb3
AM_CPPFLAGS	= -I$(top_srcdir) -I. -DSYSCONFDIR=\"$(sysconfdir)\"
LDADD		= $(curl_LIBS) $(glib_LIBS) $(json_LIBS)

bin_PROGRAMS	= nntpit

nntpit_SOURCES	= nntpit.c charq.c strlcpy.c reddit.c spool.c comments.c \
	subreddit.c jsonutil.c fetch.c rfc5536.c charq.h reddit.h jsonutil.h

INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendgroupwise\"	\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/backends/file		\
	-I$(top_builddir)/calendar/backends/file	\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/servers/groupwise		\
	-I$(top_builddir)/servers/groupwise		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)

noinst_LTLIBRARIES = libecalbackendgroupwise.la

libecalbackendgroupwise_la_SOURCES =		\
	e-cal-backend-groupwise.c		\
	e-cal-backend-groupwise.h		\
	e-cal-backend-groupwise-utils.c		\
	e-cal-backend-groupwise-utils.h

libecalbackendgroupwise_la_LIBADD =					\
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la		\
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la	\
	$(top_builddir)/servers/groupwise/libegroupwise.la		\
	$(SOUP_LIBS)

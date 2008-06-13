INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendgroupwise\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	-I$(top_srcdir)/servers/groupwise		\
	-I$(top_builddir)/servers/groupwise		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)

extension_LTLIBRARIES = libecalbackendgroupwise.la

libecalbackendgroupwise_la_SOURCES =		\
	e-cal-backend-groupwise-factory.c	\
	e-cal-backend-groupwise-factory.h	\
	e-cal-backend-groupwise.c		\
	e-cal-backend-groupwise.h		\
	e-cal-backend-groupwise-utils.c		\
	e-cal-backend-groupwise-utils.h

libecalbackendgroupwise_la_LIBADD =					\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/servers/groupwise/libegroupwise-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/libebackend/libebackend-1.2.la			\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(SOUP_LIBS)

libecalbackendgroupwise_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

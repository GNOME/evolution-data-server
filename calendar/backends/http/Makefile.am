extension_LTLIBRARIES = libecalbackendhttp.la

libecalbackendhttp_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libecalbackendhttp\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)

libecalbackendhttp_la_SOURCES =		\
	e-cal-backend-http-factory.c	\
	e-cal-backend-http-factory.h	\
	e-cal-backend-http.c		\
	e-cal-backend-http.h

libecalbackendhttp_la_LIBADD =						\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/libebackend/libebackend-1.2.la			\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(SOUP_LIBS)

libecalbackendhttp_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

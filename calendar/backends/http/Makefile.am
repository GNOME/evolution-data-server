INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendfile\"		\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/backends/file		\
	-I$(top_builddir)/calendar/backends/file	\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(EVOLUTION_CALENDAR_CFLAGS)

noinst_LTLIBRARIES = libecalbackendhttp.la

libecalbackendhttp_la_SOURCES =		\
	e-cal-backend-http.c		\
	e-cal-backend-http.h

libecalbackendhttp_la_LIBADD =						\
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la		\
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la
INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendfile\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	$(EVOLUTION_CALENDAR_CFLAGS)

noinst_LTLIBRARIES = libecalbackendfile.la

libecalbackendfile_la_SOURCES =		\
	e-cal-backend-file-events.c\
	e-cal-backend-file-events.h\
	e-cal-backend-file-todos.c\
	e-cal-backend-file-todos.h\
	e-cal-backend-file.c	\
	e-cal-backend-file.h

libecalbackendfile_la_LIBADD =					\
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la


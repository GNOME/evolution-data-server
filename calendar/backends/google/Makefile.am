extension_LTLIBRARIES = libecalbackendgoogle.la

libecalbackendgoogle_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libecalbackendgoogle\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/servers/google/libgdata		\
	-I$(top_builddir)/servers/google/libgdata	\
	-I$(top_srcdir)/servers/google/libgdata-google	\
	-I$(top_builddir)/servers/google/libgdata-google\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)					

libecalbackendgoogle_la_SOURCES =		\
	e-cal-backend-google-factory.c		\
	e-cal-backend-google-factory.h		\
	e-cal-backend-google.c			\
	e-cal-backend-google.h			\
	e-cal-backend-google-utils.c		\
	e-cal-backend-google-utils.h		

libecalbackendgoogle_la_LIBADD =						\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la			\
	$(top_builddir)/libebackend/libebackend-1.2.la				\
	$(top_builddir)/servers/google/libgdata/libgdata-1.2.la			\
	$(top_builddir)/servers/google/libgdata-google/libgdata-google-1.2.la	\
	$(EVOLUTION_CALENDAR_LIBS)						\
	$(SOUP_LIBS)

libecalbackendgoogle_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

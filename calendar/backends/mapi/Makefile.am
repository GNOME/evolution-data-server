INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendmapi\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	-I$(top_srcdir)/servers/mapi			\
	-I$(top_builddir)/servers/mapi			\
	-DMAPI_DATADIR=\""$(mapidatadir)"\"		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(LIBMAPI_CFLAGS)

extension_LTLIBRARIES = libecalbackendmapi.la

mapidata_DATA = \
	tz-mapi-to-ical 	\
	tz-ical-to-mapi 

libecalbackendmapi_la_SOURCES =		\
	e-cal-backend-mapi-factory.c	\
	e-cal-backend-mapi-factory.h	\
	e-cal-backend-mapi.c		\
	e-cal-backend-mapi.h		\
	e-cal-backend-mapi-utils.c	\
	e-cal-backend-mapi-utils.h	\
	e-cal-backend-mapi-tz-utils.c	\
	e-cal-backend-mapi-tz-utils.h

libecalbackendmapi_la_LIBADD =						\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/servers/mapi/libexchangemapi-1.0.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(LIBMAPI_LIBS)

libecalbackendmapi_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)

EXTRA_DIST = 	\
	$(mapidata_DATA)

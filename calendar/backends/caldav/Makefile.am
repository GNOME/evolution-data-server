extension_LTLIBRARIES = libecalbackendcaldav.la

libecalbackendcaldav_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(SOUP_CFLAGS) \
	-DG_LOG_DOMAIN=\"e-cal-backend-caldav\"

libecalbackendcaldav_la_SOURCES = \
	e-cal-backend-caldav-factory.c \
	e-cal-backend-caldav-factory.h \
	e-cal-backend-caldav.c \
	e-cal-backend-caldav.h

libecalbackendcaldav_la_LIBADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(SOUP_LIBS)

libecalbackendcaldav_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

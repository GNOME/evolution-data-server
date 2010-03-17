extension_LTLIBRARIES = libecalbackendweather.la

libecalbackendweather_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libecalbackendweather\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"		\
	-DWEATHER_DATADIR=\""$(weatherdatadir)"\"	\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(LIBGWEATHER_CFLAGS)

libecalbackendweather_la_SOURCES =	\
	e-cal-backend-weather-factory.c	\
	e-cal-backend-weather-factory.h	\
	e-cal-backend-weather.c		\
	e-cal-backend-weather.h		\
	e-weather-source.c		\
	e-weather-source.h		\
	e-weather-source-ccf.c		\
	e-weather-source-ccf.h

libecalbackendweather_la_LIBADD =						\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la			\
	$(top_builddir)/libebackend/libebackend-1.2.la				\
	$(EVOLUTION_CALENDAR_LIBS)						\
	$(SOUP_LIBS)								\
	$(LIBGWEATHER_LIBS)

libecalbackendweather_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED)
	

weatherdata_in_files = Locations.xml.in
weatherdata_DATA = $(weatherdata_in_files:.xml.in=.xml)
# FIXME - should probably do some sort of intltool merge here
%.xml:	%.xml.in
	cp $< $@

DISTCLEANFILES = Locations.xml
EXTRA_DIST = Locations.xml.in

-include $(top_srcdir)/git.mk

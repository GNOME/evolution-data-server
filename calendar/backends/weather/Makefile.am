NULL =

ecal_backend_LTLIBRARIES = libecalbackendweather.la

libecalbackendweather_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libecalbackendweather\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\" \
	-DWEATHER_DATADIR=\""$(weatherdatadir)"\" \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(LIBGWEATHER_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libecalbackendweather_la_SOURCES = \
	e-cal-backend-weather-factory.c \
	e-cal-backend-weather.c \
	e-cal-backend-weather.h \
	e-weather-source.c \
	e-weather-source.h \
	$(NULL)

libecalbackendweather_la_LIBADD = \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(LIBGWEATHER_LIBS) \
	$(NULL)

libecalbackendweather_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

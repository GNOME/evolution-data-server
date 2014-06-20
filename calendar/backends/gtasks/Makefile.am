NULL =

ecal_backend_LTLIBRARIES = libecalbackendgtasks.la

libecalbackendgtasks_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(GDATA_CFLAGS) \
	$(SOUP_CFLAGS) \
	-DG_LOG_DOMAIN=\"e-cal-backend-gtasks\" \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libecalbackendgtasks_la_SOURCES = \
	e-cal-backend-gtasks-factory.c \
	e-cal-backend-gtasks.c \
	e-cal-backend-gtasks.h \
	e-gdata-oauth2-authorizer.c \
	e-gdata-oauth2-authorizer.h \
	$(NULL)

libecalbackendgtasks_la_LIBADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(GDATA_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

libecalbackendgtasks_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

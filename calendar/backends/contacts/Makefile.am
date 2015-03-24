NULL =

ecal_backend_LTLIBRARIES = libecalbackendcontacts.la

libecalbackendcontacts_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libecalbackendcontact\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libecalbackendcontacts_la_SOURCES = \
	e-cal-backend-contacts-factory.c \
	e-cal-backend-contacts.c \
	e-cal-backend-contacts.h \
	$(NULL)

libecalbackendcontacts_la_LIBADD = \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_LIBS) \
	$(NULL)

libecalbackendcontacts_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

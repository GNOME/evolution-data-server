NULL =

service_in_files = org.gnome.evolution.dataserver.AddressBook.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = $(service_DATA)
EXTRA_DIST = $(service_in_files)

libexec_PROGRAMS = evolution-addressbook-factory

evolution_addressbook_factory_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"evolution-addressbook-factory\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir) \
	-I$(top_builddir)/addressbook \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(FACTORY_GTK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(GOA_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

evolution_addressbook_factory_SOURCES = \
	evolution-addressbook-factory.c \
	$(NULL)

evolution_addressbook_factory_LDADD = \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(LIBSECRET_LIBS) \
	$(FACTORY_GTK_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(GOA_LIBS) \
	$(NULL)

evolution_addressbook_factory_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

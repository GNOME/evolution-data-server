NULL =

dbus_service_in_files = org.gnome.evolution.dataserver.AddressBook.service.in
dbus_service_DATA = $(dbus_service_in_files:.service.in=.service)

systemd_user_in_files = evolution-addressbook-factory.service.in
if WITH_SYSTEMD_USER_UNITS
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)
endif

# this works for both D-Bus and systemd services
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = \
	$(dbus_service_DATA) \
	$(systemd_user_DATA) \
	$(NULL)
EXTRA_DIST = \
	$(dbus_service_in_files) \
	$(systemd_user_in_files) \
	$(NULL)

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

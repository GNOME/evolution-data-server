NULL =

service_in_files = org.gnome.evolution.dataserver.Sources.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = $(service_DATA)
EXTRA_DIST = $(service_in_files)

libexec_PROGRAMS = evolution-source-registry

evolution_source_registry_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"evolution-source-registry\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(GNOME_KEYRING_CFLAGS) \
	$(FACTORY_GTK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

evolution_source_registry_SOURCES = \
	evolution-source-registry.c \
	evolution-source-registry-migrate-basedir.c \
	evolution-source-registry-migrate-sources.c \
	$(NULL)

evolution_source_registry_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(GNOME_KEYRING_LIBS) \
	$(FACTORY_GTK_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

-include $(top_srcdir)/git.mk

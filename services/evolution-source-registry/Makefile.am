NULL =

builtin_sources_in_files = \
	builtin/caldav-stub.source.in \
	builtin/contacts-stub.source.in \
	builtin/google-stub.source.in \
	builtin/ldap-stub.source.in \
	builtin/local-stub.source.in \
	builtin/weather-stub.source.in \
	builtin/webcal-stub.source.in \
	builtin/webdav-stub.source.in \
	builtin/birthdays.source.in \
	builtin/local.source.in \
	builtin/sendmail.source.in \
	builtin/system-address-book.source.in \
	builtin/system-calendar.source.in \
	builtin/system-memo-list.source.in \
	builtin/system-proxy.source.in \
	builtin/system-task-list.source.in \
	builtin/vfolder.source.in \
	$(NULL)
builtin_sources = $(builtin_sources_in_files:.source.in=.source)

%.source: %.source.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po)
	$(AM_V_GEN) $(MKDIR_P) builtin && LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@

dbus_service_in_files = org.gnome.evolution.dataserver.Sources.service.in
dbus_service_DATA = $(dbus_service_in_files:.service.in=.service)

systemd_user_in_files = evolution-source-registry.service.in
if WITH_SYSTEMD_USER_UNITS
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)
endif

# this works for both
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(builtin_sources) \
	$(dbus_service_DATA) \
	$(systemd_user_DATA) \
	$(NULL)

EXTRA_DIST = \
	evolution-source-registry-resource.xml \
	$(builtin_sources_in_files) \
	$(dbus_service_in_files) \
	$(systemd_user_in_files) \
	$(NULL)

libexec_PROGRAMS = \
	evolution-source-registry \
	evolution-scan-gconf-tree-xml \
	$(NULL)

evolution_source_registry_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"evolution-source-registry\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(FACTORY_GTK_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

evolution_source_registry_SOURCES = \
	$(BUILT_SOURCES) \
	evolution-source-registry.c \
	evolution-source-registry-migrate-basedir.c \
	evolution-source-registry-migrate-proxies.c \
	evolution-source-registry-migrate-sources.c \
	evolution-source-registry-migrate-imap-to-imapx.c \
	$(NULL)

evolution_source_registry_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(FACTORY_GTK_LIBS) \
	$(LIBSECRET_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

evolution_scan_gconf_tree_xml_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"evolution-scan-gconf-tree-xml\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)

evolution_scan_gconf_tree_xml_SOURCES = \
	evolution-scan-gconf-tree-xml.c \
	evolution-source-registry-migrate-sources.c \
	$(NULL)

evolution_scan_gconf_tree_xml_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(LIBSECRET_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)

evolution-source-registry-resource.h: evolution-source-registry-resource.xml $(builtin_sources)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
	$(srcdir)/evolution-source-registry-resource.xml \
	--target=$@ \
	--sourcedir=$(builddir)/builtin \
	--c-name evolution_source_registry \
	--generate-header
evolution-source-registry-resource.c: evolution-source-registry-resource.xml $(builtin_sources)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
	$(srcdir)/evolution-source-registry-resource.xml \
	--target=$@ \
	--sourcedir=$(builddir)/builtin \
	--c-name evolution_source_registry \
	--generate-source

BUILT_SOURCES = \
	evolution-source-registry-resource.c \
	evolution-source-registry-resource.h \
	$(NULL)

-include $(top_srcdir)/git.mk

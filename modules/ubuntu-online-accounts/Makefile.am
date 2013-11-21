NULL =

module_LTLIBRARIES = module-ubuntu-online-accounts.la

module_ubuntu_online_accounts_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-ubuntu-online-accounts\" \
	$(LIBACCOUNTS_GLIB_CFLAGS) \
	$(LIBSIGNON_GLIB_CFLAGS) \
	$(E_BACKEND_CFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(REST_CFLAGS) \
	$(NULL)

module_ubuntu_online_accounts_la_SOURCES = \
	module-ubuntu-online-accounts.c \
	e-signon-session-password.c \
	e-signon-session-password.h \
	uoa-utils.c \
	uoa-utils.h \
	$(NULL)

module_ubuntu_online_accounts_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(LIBACCOUNTS_GLIB_LIBS) \
	$(LIBSIGNON_GLIB_LIBS) \
	$(E_BACKEND_LIBS) \
	$(JSON_GLIB_LIBS) \
	$(CAMEL_LIBS) \
	$(REST_LIBS) \
	$(NULL)

module_ubuntu_online_accounts_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

%.application: %.application.in
	$(AM_V_GEN) $(INTLTOOL_MERGE) --no-translations -x -u $< $@

%.service-type: %.service-type.in
	$(AM_V_GEN) $(INTLTOOL_MERGE) --no-translations -x -u $< $@

%.service: %.service.in
	$(AM_V_GEN) $(INTLTOOL_MERGE) --no-translations -x -u $< $@

applicationdir = `$(PKG_CONFIG) --variable=applicationfilesdir libaccounts-glib`
application_DATA = evolution-data-server.application

servicetypedir = `$(PKG_CONFIG) --variable=servicetypefilesdir libaccounts-glib`
servicetype_DATA = \
	mail.service-type \
	calendar.service-type \
	contacts.service-type \
	$(NULL)

servicedir = `$(PKG_CONFIG) --variable=servicefilesdir libaccounts-glib`
service_DATA = \
	google-gmail.service \
	google-calendar.service \
	google-contacts.service \
	windows-live-mail.service \
	yahoo-mail.service \
	yahoo-calendar.service \
	$(NULL)

desktopdir = $(datadir)/applications
desktop_in_files = evolution-data-server-uoa.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = \
	evolution-data-server.application.in.in \
	mail.service-type.in.in \
	calendar.service-type.in.in \
	contacts.service-type.in.in \
	google-gmail.service.in.in \
	google-calendar.service.in.in \
	google-contacts.service.in.in \
	windows-live-mail.service.in.in \
	yahoo-mail.service.in.in \
	yahoo-calendar.service.in.in \
	$(desktop_DATA) \
	$(NULL)

DISTCLEANFILES = \
	$(application_DATA) \
	$(desktop_DATA) \
	$(servicetype_DATA) \
	$(service_DATA) \
	$(NULL)

-include $(top_srcdir)/git.mk

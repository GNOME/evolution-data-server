NULL =

module_LTLIBRARIES = module-gnome-online-accounts.la

module_gnome_online_accounts_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-gnome-online-accounts\" \
	$(E_BACKEND_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(GOA_CFLAGS) \
	$(NULL)

module_gnome_online_accounts_la_SOURCES = \
	module-gnome-online-accounts.c \
	e-goa-client.c \
	e-goa-client.h \
	e-goa-password-based.c \
	e-goa-password-based.h \
	goaewsclient.c \
	goaewsclient.h \
	$(NULL)

module_gnome_online_accounts_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(GOA_LIBS) \
	$(NULL)

module_gnome_online_accounts_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk

NULL =

module_LTLIBRARIES = module-online-accounts.la

module_online_accounts_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-online-accounts\" \
	$(E_BACKEND_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(GOA_CFLAGS) \
	$(NULL)

module_online_accounts_la_SOURCES = \
	module-online-accounts.c \
	goaewsclient.c \
	goaewsclient.h \
	$(NULL)

module_online_accounts_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(GOA_LIBS) \
	$(NULL)

module_online_accounts_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk

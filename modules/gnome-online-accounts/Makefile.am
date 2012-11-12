NULL =

module_LTLIBRARIES = module-gnome-online-accounts.la

# These aren't actually backends, but that's all
# the installation directory has historically held.
ecal_backend_LTLIBRARIES = module-data-cal-factory-goa.la
ebook_backend_LTLIBRARIES = module-data-book-factory-goa.la

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
	goaewsclient.c \
	goaewsclient.h \
	$(NULL)

module_gnome_online_accounts_la_LIBADD = \
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

module_data_factory_goa_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-data-factory-goa\" \
	$(E_BACKEND_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(GOA_CFLAGS) \
	$(NULL)

module_data_factory_goa_SOURCES = \
	module-data-factory-goa.c \
	$(NULL)

module_data_factory_goa_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(CAMEL_LIBS) \
	$(SOUP_LIBS) \
	$(GOA_LIBS) \
	$(NULL)

module_data_factory_goa_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

# Libtool forces us to build separate modules for the address book
# and calendar factories, even though the modules are identical.

module_data_cal_factory_goa_la_CPPFLAGS = $(module_data_factory_goa_CPPFLAGS)
module_data_cal_factory_goa_la_SOURCES = $(module_data_factory_goa_SOURCES)
module_data_cal_factory_goa_la_LIBADD = $(module_data_factory_goa_LIBADD)
module_data_cal_factory_goa_la_LDFLAGS = $(module_data_factory_goa_LDFLAGS)

module_data_book_factory_goa_la_CPPFLAGS = $(module_data_factory_goa_CPPFLAGS)
module_data_book_factory_goa_la_SOURCES = $(module_data_factory_goa_SOURCES)
module_data_book_factory_goa_la_LIBADD = $(module_data_factory_goa_LIBADD)
module_data_book_factory_goa_la_LDFLAGS = $(module_data_factory_goa_LDFLAGS)

-include $(top_srcdir)/git.mk

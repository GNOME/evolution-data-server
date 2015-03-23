NULL =

module_LTLIBRARIES = module-owncloud-backend.la

module_owncloud_backend_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-owncloud-backend\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

module_owncloud_backend_la_SOURCES = \
	module-owncloud-backend.c \
	$(NULL)

module_owncloud_backend_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

module_owncloud_backend_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk

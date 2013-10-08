NULL =

module_LTLIBRARIES = module-outlook-backend.la

module_outlook_backend_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-outlook-backend\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

module_outlook_backend_la_SOURCES = \
	module-outlook-backend.c \
	$(NULL)

module_outlook_backend_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

module_outlook_backend_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk

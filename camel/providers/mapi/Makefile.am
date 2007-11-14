## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelmapi.la
camel_provider_DATA = libcamelmapi.urls

INCLUDES = -I.. \
	-I$(top_srcdir)				\
	-I$(top_srcdir)/camel			\
        -I$(top_srcdir)/libedataserver          \
	-I$(top_srcdir)/servers/mapi/		\
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
        $(LIBMAPI_CFLAGS)                       \
	-DG_LOG_DOMAIN=\"camel-mapi-provider\"

libcamelmapi_la_SOURCES = 			\
	camel-mapi-provider.c			\
	camel-mapi-folder.c                	\
        camel-mapi-store.c	                \
	camel-mapi-store-summary.c         	\
	camel-mapi-summary.c	         	\
	camel-mapi-transport.c			\
	oc.c					\
	oc_thread.c			

noinst_HEADERS =         			\
	camel-mapi-folder.h			\
	camel-mapi-store.h			\
	camel-mapi-store-summary.h		\
	camel-mapi-summary.h	         	\
	camel-mapi-transport.h			\
	camel-mapi-private.h				\
	camel-private.h				\
	oc.h					\
	oc_thread.h


libcamelmapi_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)
libcamelmapi_la_LIBADD =                                            \
	$(top_builddir)/servers/mapi/libexchangemapi-1.0.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la             \
	$(top_builddir)/camel/libcamel-provider-1.2.la	                 \
	$(top_builddir)/camel/libcamel-1.2.la		                 \
	$(CAMEL_LIBS)                                                    \
        $(LIBMAPI_LIBS)

EXTRA_DIST = libcamelmapi.urls

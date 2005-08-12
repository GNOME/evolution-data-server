## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelnntp.la
camel_provider_DATA = libcamelnntp.urls

INCLUDES = -I../..				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/intl			\
	-I$(top_srcdir)/e-util			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
	$(GTK_INCLUDEDIR)			\
	-DCAMEL_EXPLICIT_TRANSLATION_DOMAIN=\"$(GETTEXT_PACKAGE)\"	\
	-DG_LOG_DOMAIN=\"camel-nntp-provider\"

libcamelnntp_la_SOURCES = 			\
	camel-nntp-provider.c			\
	camel-nntp-store.c			\
	camel-nntp-folder.c			\
	camel-nntp-stream.c			\
	camel-nntp-summary.c			\
	camel-nntp-store-summary.c

noinst_HEADERS =			\
	camel-nntp-store.h			\
	camel-nntp-folder.h			\
	camel-nntp-resp-codes.h			\
	camel-nntp-stream.h			\
	camel-nntp-summary.h			\
	camel-nntp-store-summary.h		\
	camel-nntp-private.h

libcamelnntp_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelnntp_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(CAMEL_LIBS)

EXTRA_DIST = libcamelnntp.urls

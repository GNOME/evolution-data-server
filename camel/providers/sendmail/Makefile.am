## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelsendmail.la
camel_provider_DATA = libcamelsendmail.urls

INCLUDES =						\
	-I..						\
	-I$(srcdir)/..					\
	-I$(srcdir)/../../..				\
	-I$(top_srcdir)/intl				\
	-I$(top_srcdir)/camel                   	\
	$(CAMEL_CFLAGS)					\
	$(GNOME_INCLUDEDIR)				\
	$(GTK_INCLUDEDIR) 				\
	-DG_LOG_DOMAIN=\"camel-sendmail-provider\"

libcamelsendmail_la_SOURCES = 			\
	camel-sendmail-provider.c		\
	camel-sendmail-transport.c

noinst_HEADERS =		\
	camel-sendmail-transport.h

libcamelsendmail_la_LDFLAGS = -avoid-version -module

libcamelsendmail_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(CAMEL_LIBS)

EXTRA_DIST = libcamelsendmail.urls

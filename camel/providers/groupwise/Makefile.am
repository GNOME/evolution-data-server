## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelgroupwise.la
camel_provider_DATA = libcamelgroupwise.urls

INCLUDES = -I.. \
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/camel/providers/imap    \
	-I$(top_srcdir)/intl			\
	-I$(top_srcdir)/e-util			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
	$(GTK_INCLUDEDIR)			\
	-DG_LOG_DOMAIN=\"camel-groupwise-provider\"

libcamelgroupwise_la_SOURCES = 			\
	camel-groupwise-provider.c			


libcamelgroupwise_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libcamelgroupwise.urls

## Process this file with automake to produce Makefile.in

libcamelimapincludedir = $(privincludedir)/camel

camel_provider_LTLIBRARIES = libcamelgroupwise.la
camel_provider_DATA = libcamelgroupwise.urls

INCLUDES = -I.. \
	-I$(srcdir)/..					\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/intl				\
	-I$(top_srcdir)/e-util				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/camel/providers/imap		\
	-I$(top_srcdir)/libedataserver			\
	-I$(top_srcdir)/servers/groupwise		\
	$(EVOLUTION_CALENDAR_CFLAGS)                    \
	$(CAMEL_GROUPWISE_CFLAGS)			\
	$(GNOME_INCLUDEDIR)				\
	$(SOUP_CFLAGS)					\
	$(E_UTIL_CFLAGS)				\
	$(GTK_INCLUDEDIR)				\
	-DCAMEL_PROVIDERDIR=\"$(camel_providerdir)\"	\
	-DG_LOG_DOMAIN=\"camel-groupwise-provider\"

libcamelgroupwise_la_SOURCES = 			\
	camel-groupwise-provider.c		\
	camel-groupwise-store.h 	\
	camel-groupwise-store.c		\
	camel-groupwise-folder.h	\
	camel-groupwise-folder.c	\
	camel-groupwise-summary.c	\
	camel-groupwise-summary.h	\
	camel-groupwise-store-summary.c \
	camel-groupwise-store-summary.h \
	camel-groupwise-message-cache.c \
	camel-groupwise-message-cache.h \
	camel-groupwise-transport.c	\
	camel-groupwise-transport.h

libcamelgroupwise_la_LIBADD = 						\
	$(top_builddir)/servers/groupwise/libegroupwise-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(SOUP_LIBS)

libcamelgroupwise_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libcamelgroupwise.urls


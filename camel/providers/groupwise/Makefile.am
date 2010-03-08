camel_provider_LTLIBRARIES = libcamelgroupwise.la
camel_provider_DATA = libcamelgroupwise.urls

libcamelgroupwise_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/camel/providers		\
	-I$(top_srcdir)/camel/providers/imap    \
	-I$(top_srcdir)/libedataserver          \
	-I$(top_srcdir)/servers/groupwise       \
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
	$(SOUP_CFLAGS)                          \
	$(E_DATA_SERVER_CFLAGS)			\
	-DG_LOG_DOMAIN=\"camel-groupwise-provider\"

libcamelgroupwise_la_SOURCES = 			\
	camel-groupwise-provider.c		\
	camel-groupwise-folder.c                \
	camel-groupwise-journal.c               \
	camel-groupwise-store.c                 \
	camel-groupwise-summary.c               \
	camel-groupwise-store-summary.c         \
	camel-groupwise-utils.c			\
	camel-groupwise-transport.c     	       	

noinst_HEADERS =         			\
	camel-groupwise-folder.h		\
	camel-groupwise-journal.h		\
	camel-groupwise-private.h		\
	camel-groupwise-store.h			\
	camel-groupwise-summary.h		\
	camel-groupwise-store-summary.h		\
	camel-groupwise-utils.h			\
	camel-groupwise-transport.h		

libcamelgroupwise_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelgroupwise_la_LIBADD =                                            \
	$(top_builddir)/servers/groupwise/libegroupwise-1.2.la           \
	$(top_builddir)/libedataserver/libedataserver-1.2.la             \
	$(top_builddir)/camel/libcamel-provider-1.2.la	                 \
	$(top_builddir)/camel/libcamel-1.2.la		                 \
	$(CAMEL_LIBS)                                                    \
	$(SOUP_LIBS)							 \
	$(E_DATA_SERVER_LIBS)

EXTRA_DIST = libcamelgroupwise.urls

-include $(top_srcdir)/git.mk

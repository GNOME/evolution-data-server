NULL =

camel_provider_LTLIBRARIES = libcamelpop3.la
camel_provider_DATA = libcamelpop3.urls

libcamelpop3_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	$(CAMEL_CFLAGS) \
	-DG_LOG_DOMAIN=\"camel-pop3-provider\" \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamelpop3_la_SOURCES = \
	camel-pop3-engine.c \
	camel-pop3-folder.c \
	camel-pop3-provider.c \
	camel-pop3-settings.c \
	camel-pop3-stream.c \
	camel-pop3-store.c \
	$(NULL)

noinst_HEADERS = \
	camel-pop3-engine.h \
	camel-pop3-folder.h \
	camel-pop3-settings.h \
	camel-pop3-stream.h \
	camel-pop3-store.h \
	$(NULL)

libcamelpop3_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libcamelpop3_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(CAMEL_LIBS) \
	$(NULL)

EXTRA_DIST = libcamelpop3.urls

-include $(top_srcdir)/git.mk

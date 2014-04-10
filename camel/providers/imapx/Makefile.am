NULL =

camel_provider_LTLIBRARIES = libcamelimapx.la
camel_provider_DATA = libcamelimapx.urls

camel-imapx-tokenise.h: camel-imapx-tokens.txt
	@GPERF@ -H imapx_hash -N imapx_tokenise_struct -L ANSI-C -o -t -k1,$$ $< --output-file=$@

libcamelimapx_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_builddir) \
	-I$(top_builddir)/camel \
	$(CAMEL_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	-DG_LOG_DOMAIN=\"camel-imapx\" \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamelimapx_la_SOURCES = \
	camel-imapx-provider.c \
	camel-imapx-command.c \
	camel-imapx-command.h \
	camel-imapx-conn-manager.c \
	camel-imapx-conn-manager.h \
	camel-imapx-folder.c \
	camel-imapx-folder.h \
	camel-imapx-input-stream.c \
	camel-imapx-input-stream.h \
	camel-imapx-job.c \
	camel-imapx-job.h \
	camel-imapx-list-response.c \
	camel-imapx-list-response.h \
	camel-imapx-logger.c \
	camel-imapx-logger.h \
	camel-imapx-mailbox.c \
	camel-imapx-mailbox.h \
	camel-imapx-namespace.c \
	camel-imapx-namespace.h \
	camel-imapx-namespace-response.c \
	camel-imapx-namespace-response.h \
	camel-imapx-search.c \
	camel-imapx-search.h \
	camel-imapx-server.c \
	camel-imapx-server.h \
	camel-imapx-settings.c \
	camel-imapx-settings.h \
	camel-imapx-status-response.c \
	camel-imapx-status-response.h \
	camel-imapx-store.c \
	camel-imapx-store.h \
	camel-imapx-store-summary.c \
	camel-imapx-store-summary.h \
	camel-imapx-summary.c \
	camel-imapx-summary.h \
	camel-imapx-tokenise.h \
	camel-imapx-utils.c \
	camel-imapx-utils.h \
	$(NULL)

libcamelimapx_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(CAMEL_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

libcamelimapx_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

noinst_PROGRAMS = test-imapx

test_imapx_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I.. \
	-I$(srcdir)/.. \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir) \
	$(CAMEL_CFLAGS) \
	-DG_LOG_DOMAIN=\"camel-imapx\" \
	$(NULL)
test_imapx_SOURCES = test-imapx.c
test_imapx_LDADD = \
	$(CAMEL_LIBS) \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(NULL)

BUILT_SOURCES = camel-imapx-tokenise.h

EXTRA_DIST = \
	camel-imapx-tokens.txt \
	libcamelimapx.urls \
	$(NULL)

-include $(top_srcdir)/git.mk

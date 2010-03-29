camel_provider_LTLIBRARIES = libcamelimapx.la
camel_provider_DATA = libcamelimapx.urls

libcamelimapx_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I..					\
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-imapx\"

libcamelimapx_la_SOURCES =			\
	camel-imapx-exception.c			\
	camel-imapx-stream.c			\
	camel-imapx-utils.c			\
	camel-imapx-provider.c			\
	camel-imapx-store-summary.c		\
	camel-imapx-summary.c			\
	camel-imapx-store.c			\
	camel-imapx-folder.c			\
	camel-imapx-server.c			

noinst_HEADERS =				\
	camel-imapx-exception.h			\
	camel-imapx-stream.h			\
	camel-imapx-store-summary.h		\
	camel-imapx-summary.h			\
	camel-imapx-folder.h			\
	camel-imapx-store.h			\
	camel-imapx-server.h			\
	camel-imapx-utils.h

camel-imapx-tokenise.h: camel-imapx-tokens.txt
	@GPERF@ -H imapx_hash -N imapx_tokenise_struct -L ANSI-C -o -t -k1,$$ $< --output-file=$@

libcamelimapx_la_LIBADD = \
        $(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
        $(top_builddir)/camel/libcamel-provider-1.2.la                  \
        $(top_builddir)/camel/libcamel-1.2.la                           \
        $(CAMEL_LIBS)

libcamelimapx_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

noinst_PROGRAMS = test-imapx

test_imapx_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I..					\
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-imapx\"
test_imapx_SOURCES = test-imapx.c
test_imapx_LDADD =							\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(top_builddir)/camel/libcamel-provider-1.2.la

BUILT_SOURCES = camel-imapx-tokenise.h
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = libcamelimapx.urls camel-imapx-tokens.txt

-include $(top_srcdir)/git.mk

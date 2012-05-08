camel_provider_LTLIBRARIES = libcamelimapx.la
camel_provider_DATA = libcamelimapx.urls

libcamelimapx_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I..					\
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-imapx\"		\
	$(CODE_COVERAGE_CFLAGS)			\
	$(NULL)

libcamelimapx_la_SOURCES =			\
	camel-imapx-provider.c

libcamelimapx_la_LIBADD = \
        $(top_builddir)/camel/libcamel-1.2.la	\
        $(CAMEL_LIBS)

libcamelimapx_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

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
test_imapx_LDADD =				\
	$(CAMEL_LIBS)				\
	$(top_builddir)/camel/libcamel-1.2.la

EXTRA_DIST = libcamelimapx.urls

-include $(top_srcdir)/git.mk

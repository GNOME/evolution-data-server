NULL =

camel_provider_LTLIBRARIES = libcamelsendmail.la
camel_provider_DATA = libcamelsendmail.urls

libcamelsendmail_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I.. \
	-I$(srcdir)/.. \
	-I$(srcdir)/../../.. \
	-I$(top_srcdir)/camel \
	$(CAMEL_CFLAGS) \
	-DG_LOG_DOMAIN=\"camel-sendmail-provider\" \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamelsendmail_la_SOURCES = \
	camel-sendmail-settings.h \
	camel-sendmail-settings.c \
	camel-sendmail-provider.c \
	camel-sendmail-transport.c \
	camel-sendmail-transport.h \
	$(NULL)

libcamelsendmail_la_LDFLAGS = -avoid-version -module \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libcamelsendmail_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(CAMEL_LIBS) \
	$(NULL)

EXTRA_DIST = libcamelsendmail.urls

-include $(top_srcdir)/git.mk

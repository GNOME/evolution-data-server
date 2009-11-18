camel_provider_LTLIBRARIES = libcamelsmtp.la
camel_provider_DATA = libcamelsmtp.urls

libcamelsmtp_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/camel			\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-smtp-provider\"

libcamelsmtp_la_SOURCES = 			\
	camel-smtp-provider.c			\
	camel-smtp-transport.c

noinst_HEADERS =			\
	camel-smtp-transport.h

libcamelsmtp_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelsmtp_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(CAMEL_LIBS)

EXTRA_DIST = libcamelsmtp.urls

-include $(top_srcdir)/git.mk

## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelhula.la
camel_provider_DATA = libcamelhula.urls

INCLUDES = -I.. \
	-I$(top_srcdir)				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/camel/providers/imap    \
        -I$(top_srcdir)/libedataserver          \
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
	-DCAMEL_EXPLICIT_TRANSLATION_DOMAIN=\"$(GETTEXT_PACKAGE)\"	\
	-DG_LOG_DOMAIN=\"camel-hula-provider\"

libcamelhula_la_SOURCES = 			\
	camel-hula-provider.c		
	     	       	


libcamelhula_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)
libcamelhula_la_LIBADD =                                            \
	$(top_builddir)/libedataserver/libedataserver-1.2.la             \
	$(top_builddir)/camel/libcamel-provider-1.2.la	                 \
	$(top_builddir)/camel/libcamel-1.2.la		                 \
	$(CAMEL_LIBS)

EXTRA_DIST = libcamelhula.urls

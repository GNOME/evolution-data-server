
INCLUDES =						\
	-DG_LOG_DOMAIN=\"libexchangemapi\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(LIBMAPI_CFLAGS)				\
	$(E_DATA_SERVER_CFLAGS)


lib_LTLIBRARIES = libexchangemapi-1.0.la

libexchangemapi_1_0_la_SOURCES =			\
	exchange-mapi-folder.c				\
	exchange-mapi-folder.h				\
	exchange-mapi-connection.c			\
	exchange-mapi-connection.h

libexchangemapi_1_0_la_LIBADD =						\
	$(E_DATA_SERVER_LIBS)						\
	$(LIBMAPI_LIBS)

libexchangemapiincludedir = $(privincludedir)/mapi

libexchangemapiinclude_HEADERS =                  \
	exchange-mapi-folder.h			\
	exchange-mapi-connection.h

%-1.0.pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libexchangemapi-1.0.pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


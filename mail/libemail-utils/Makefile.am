
lib_LTLIBRARIES = libemail-utils.la

AM_CFLAGS = -I. -I$(top_builddir)/mail @GNOME_PLATFORM_CFLAGS@ -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""
AM_LDFLAGS = @GNOME_PLATFORM_LIBS@ 


MARSHAL_GENERATED = e-marshal.c e-marshal.h
@EVO_MARSHAL_RULE@

libemail_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(CAMEL_CFLAGS)		\
	$(GNOME_PLATFORM_CFLAGS) \
	-DEVOLUTION_PRIVDATADIR=\""$(privdatadir)"\"	

libemail_utils_la_LIBADD = 	\
		$(CAMEL_LIBS)		\
		$(GNOME_PLATFORM_LIBS)	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/camel/libcamel-provider-1.2.la			\
		$(top_builddir)/libedataserver/libedataserver-1.2.la

libemail_utils_la_LDFLAGS = 

libemail_utils_la_SOURCES = 	\
		e-marshal.c	\
		e-account-utils.c	\
		mail-mt.c	\
		e-signature.c	\
		e-signature-list.c


libmailutilsincludedir = $(includedir)/libemail-utils
libmailutilsinclude_HEADERS = 	\
		e-marshal.h	\
		e-account-utils.h	\
		mail-mt.h	\
		e-signature.h	\
		e-signature-list.h


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libemail-utils.pc

BUILT_SOURCES = $(MARSHAL_GENERATED) 

EXTRA_DIST = e-marshal.list
CLEANFILES    = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)



lib_LTLIBRARIES = libemail-utils.la

AM_CFLAGS = @E_MAIL_FACTORY_CFLAGS@ -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""
AM_LDFLAGS = @E_MAIL_FACTORY_LIBS@ 


MARSHAL_GENERATED = e-marshal.c e-marshal.h
@EVO_MARSHAL_RULE@

libemail_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(E_MAIL_UTILS_CFLAGS)	\
	$(CAMEL_CFLAGS)		\
	$(GNOME_PLATFORM_CFLAGS)

libemail_utils_la_LIBADD = 	\
		$(E_MAIL_UTILS_LIBS)	\
		$(CAMEL_LIBS)		\
		$(GNOME_PLATFORM_LIBS)	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/camel/libcamel-provider-1.2.la			\
		$(top_builddir)/libedataserver/libedataserver-1.2.la

libemail_utils_la_LDFLAGS = 

libemail_utils_la_SOURCES = 	\
		mail-mt.c	\
		e-marshal.c	\
		e-account-utils.c

libmailutilsincludedir = $(includedir)/libemail-utils
libmailutilsinclude_HEADERS = 	\
		mail-mt.h	\
		e-marshal.h	\
		e-account-utils.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libemail-utils.pc

BUILT_SOURCES = $(MARSHAL_GENERATED) 

EXTRA_DIST = e-marshal.list
CLEANFILES    = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)


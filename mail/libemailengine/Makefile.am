
lib_LTLIBRARIES = libemail-engine.la

AM_CFLAGS = -I$(top_builddir) @GNOME_PLATFORM_CFLAGS@ -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""
AM_LDFLAGS = @GNOME_PLATFORM_LIBS@ 


libemail_engine_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(CAMEL_CFLAGS)		\
	$(GNOME_PLATFORM_CFLAGS)

libemail_engine_la_LIBADD = 	\
		$(CAMEL_LIBS)		\
		$(GNOME_PLATFORM_LIBS)	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/camel/libcamel-provider-1.2.la			\
		$(top_builddir)/libedataserver/libedataserver-1.2.la		\
		$(top_builddir)/mail/libemailutils/libemail-utils.la
		

libemail_engine_la_LDFLAGS = 

libemail_engine_la_SOURCES = 	\
		e-mail-session.c	\
		mail-folder-cache.c	

libmailengineincludedir = $(includedir)/libemail-engine
libmailengineinclude_HEADERS = 	\
		e-mail-session.h		\
		mail-folder-cache.h		

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libemail-engine.pc

BUILT_SOURCES = 

EXTRA_DIST = 
CLEANFILES    = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)


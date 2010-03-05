ACLOCAL_AMFLAGS=-I m4

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

if ENABLE_CALENDAR
CALENDAR_DIR = calendar
endif

SUBDIRS = libedataserver libebackend servers camel addressbook $(CALENDAR_DIR) libedataserverui docs art po
DIST_SUBDIRS = libedataserver libebackend servers camel addressbook calendar libedataserverui docs art po

changelogs =			\
	ChangeLog

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evolution-data-server-$(API_VERSION).pc

EXTRA_DIST = 						\
	AUTHORS						\
	$(changelogs)					\
	README						\
	HACKING						\
	MAINTAINERS					\
	TODO						\
	NEWS						\
	marshal.mk					\
	iconv-detect.c					\
	intltool-merge.in				\
	intltool-update.in				\
	intltool-extract.in				\
	evolution-data-server-zip.in			\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES =					\
	iconv-detect.h					\
	$(pkgconfig_DATA)

MAINTAINERCLEANFILES =					\
	$(srcdir)/INSTALL				\
	$(srcdir)/aclocal.m4				\
	$(srcdir)/autoscan.log				\
	$(srcdir)/compile				\
	$(srcdir)/config.guess				\
	$(srcdir)/config.h.in				\
	$(srcdir)/config.sub				\
	$(srcdir)/configure.scan			\
	$(srcdir)/depcomp				\
	$(srcdir)/install-sh				\
	$(srcdir)/ltmain.sh				\
	$(srcdir)/missing				\
	$(srcdir)/mkinstalldirs				\
	$(srcdir)/gtk-doc.make				\
	$(srcdir)/m4/gtk-doc.m4				\
	$(srcdir)/m4/intltool.m4			\
	$(srcdir)/m4/libtool.m4				\
	$(srcdir)/m4/ltoptions.m4			\
	$(srcdir)/m4/ltsugar.m4				\
	$(srcdir)/m4/ltversion.m4			\
	$(srcdir)/m4/lt~obsolete.m4


-include $(top_srcdir)/git.mk

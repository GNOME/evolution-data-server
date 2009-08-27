ACLOCAL_AMFLAGS=-I m4

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

if ENABLE_CALENDAR
CALENDAR_DIR = calendar
endif

SUBDIRS = win32 libedataserver libebackend servers camel addressbook $(CALENDAR_DIR) libedataserverui docs art po
DIST_SUBDIRS = win32 libedataserver libebackend servers camel addressbook calendar libedataserverui docs art po

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

DISTCLEANFILES = \
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	iconv-detect.h					\
	$(pkgconfig_DATA)


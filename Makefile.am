SUBDIRS = libedataserver addressbook calendar src po

ACLOCAL_AMFLAGS = -I m4

changelogs =			\
	ChangeLog

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evolution-data-server-1.0.pc

EXTRA_DIST = 			\
	AUTHORS			\
	$(changelogs)		\
	README			\
	HACKING			\
	MAINTAINERS		\
	TODO			\
	NEWS			\
	marshal.mk		\
	intltool-merge.in	\
	intltool-update.in	\
	intltool-extract.in	\
	$(pkgconfig_DATA:.pc=.pc.in)

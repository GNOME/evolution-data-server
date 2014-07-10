ACLOCAL_AMFLAGS=-I m4

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-gtk-doc \
	--with-private-docs \
	--with-krb5=/usr \
	--enable-introspection \
	--disable-uoa \
	--disable-examples \
	$(NULL)

SUBDIRS = \
	camel \
	data \
	private \
	libedataserver \
	libebackend \
	addressbook \
	calendar \
	modules \
	services \
	tests \
	docs \
	art \
	po \
	$(NULL)

if BUILD_EXAMPLES
SUBDIRS += examples
endif

if HAVE_INTROSPECTION
if HAVE_VALA
SUBDIRS += vala
endif
endif

@GNOME_CODE_COVERAGE_RULES@

%-$(API_VERSION).pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = evolution-data-server-$(API_VERSION).pc

EXTRA_DIST = \
	AUTHORS \
	$(changelogs) \
	README \
	HACKING \
	MAINTAINERS \
	TODO \
	NEWS \
	enumtypes.c.template \
	enumtypes.h.template \
	iconv-detect.c \
	intltool-merge.in \
	intltool-update.in \
	intltool-extract.in \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	$(NULL)

DISTCLEANFILES = \
	iconv-detect.h \
	$(pkgconfig_DATA) \
	$(NULL)

MAINTAINERCLEANFILES = \
	$(srcdir)/ABOUT-NLS \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/autoscan.log \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.rpath \
	$(srcdir)/config.sub \
	$(srcdir)/configure.scan \
	$(srcdir)/depcomp \
	$(srcdir)/gtk-doc.make \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/m4/codeset.m4 \
	$(srcdir)/m4/gettext.m4 \
	$(srcdir)/m4/glibc2.m4 \
	$(srcdir)/m4/glibc21.m4 \
	$(srcdir)/m4/gtk-doc.m4 \
	$(srcdir)/m4/iconv.m4 \
	$(srcdir)/m4/intdiv0.m4 \
	$(srcdir)/m4/intl.m4 \
	$(srcdir)/m4/intldir.m4 \
	$(srcdir)/m4/intlmacosx.m4 \
	$(srcdir)/m4/intltool.m4 \
	$(srcdir)/m4/intmax.m4 \
	$(srcdir)/m4/introspection.m4 \
	$(srcdir)/m4/inttypes-pri.m4 \
	$(srcdir)/m4/inttypes_h.m4 \
	$(srcdir)/m4/lcmessage.m4 \
	$(srcdir)/m4/lib-ld.m4 \
	$(srcdir)/m4/lib-link.m4 \
	$(srcdir)/m4/lib-prefix.m4 \
	$(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/lock.m4 \
	$(srcdir)/m4/longlong.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/m4/ltsugar.m4 \
	$(srcdir)/m4/ltversion.m4 \
	$(srcdir)/m4/lt~obsolete.m4 \
	$(srcdir)/m4/nls.m4 \
	$(srcdir)/m4/po.m4 \
	$(srcdir)/m4/printf-posix.m4 \
	$(srcdir)/m4/progtest.m4 \
	$(srcdir)/m4/size_max.m4 \
	$(srcdir)/m4/stdint_h.m4 \
	$(srcdir)/m4/uintmax_t.m4 \
	$(srcdir)/m4/visibility.m4 \
	$(srcdir)/m4/wchar_t.m4 \
	$(srcdir)/m4/wint_t.m4 \
	$(srcdir)/m4/xsize.m4 \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/po/Makefile.in.in~ \
	$(srcdir)/po/Makevars.template \
	$(srcdir)/po/Rules-quot \
	$(srcdir)/po/boldquot.sed \
	$(srcdir)/po/en@boldquot.header \
	$(srcdir)/po/en@quot.header \
	$(srcdir)/po/insert-header.sin \
	$(srcdir)/po/quot.sed \
	$(srcdir)/po/remove-potcdate.sin \
	$(NULL)

-include $(top_srcdir)/git.mk

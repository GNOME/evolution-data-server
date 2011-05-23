CLEANFILES=

# The marshallers
MARSHAL_GENERATED = e-book-marshal.c e-book-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libebook-1.2.la

libebook_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DG_LOG_DOMAIN=\"libebook\"			\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/addressbook			\
	-I$(top_srcdir)/addressbook/libegdbus		\
	-I$(top_builddir)				\
	-I$(top_builddir)/addressbook			\
	-I$(top_builddir)/addressbook/libebook		\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)			\
	$(CAMEL_CFLAGS)

libebook_1_2_la_SOURCES =				\
	$(MARSHAL_GENERATED)				\
	e-address-western.c				\
	e-book-client.c					\
	e-book-client-view.c				\
	e-book-client-view-private.h			\
	e-book-query.c					\
	e-book-view-private.h				\
	e-book-view.c					\
	e-book.c					\
	e-contact.c					\
	e-destination.c					\
	e-name-western.c				\
	e-name-western-tables.h                         \
	e-vcard.c					\
	e-error.h

libebook_1_2_la_LIBADD =					\
	$(top_builddir)/addressbook/libegdbus/libegdbus-book.la	\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(CAMEL_LIBS)

libebook_1_2_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CURRENT):$(LIBEBOOK_REVISION):$(LIBEBOOK_AGE) $(NO_UNDEFINED)

libebookincludedir = $(privincludedir)/libebook

libebookinclude_HEADERS =				\
	e-address-western.h				\
	e-book-client.h					\
	e-book-client-view.h				\
	e-book-query.h					\
	e-book-view.h					\
	e-book-types.h					\
	e-book.h					\
	e-contact.h					\
	e-destination.h					\
	e-name-western.h				\
	e-vcard.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

e-name-western-tables.h: e-name-western-tables.h.in
	${srcdir}/gen-western-table.py < $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-$(API_VERSION).pc


-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_builddir)/libedataserver \
	--add-include-path=$(srcdir) --identifier-prefix=E --pkg-export \
	libebook-1.2 --c-include="libebook/e-book.h"
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_builddir)/libedataserver \
	--includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = $(libebook_1_2_la_SOURCES)
EBook-1.2.gir: libebook-1.2.la

EBook_1_2_gir_INCLUDES = GObject-2.0 EDataServer-1.2 GLib-2.0 libxml2-2.0
libxml_include = `$(PKG_CONFIG) --cflags libxml-2.0`
gconf_include = `$(PKG_CONFIG) --cflags gconf-2.0`
EBook_1_2_gir_CFLAGS = $(INCLUDES) \
		       -I$(top_srcdir) \
		       $(libxml_include) \
		       $(gconf_include) \
		       -I$(top_builddir) \
		       -I$(top_builddir)/addressbook/ \
		       -I$(top_builddir)/addressbook/libebook \
			$(NULL)
EBook_1_2_gir_LIBS = \
	libebook-1.2.la $(top_builddir)/libedataserver/libedataserver-1.2.la
EBook_1_2_gir_FILES = e-contact.c e-contact.h \
		      e-vcard.c e-vcard.h \
		      e-book.c e-book.h \
		      e-book-view.c e-book-view.h \
		      e-error.h e-book-types.h \
		      e-book-query.c e-book-query.h

INTROSPECTION_GIRS += EBook-1.2.gir

girdir = $(datadir)/gir-1.0
dist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES    += $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	e-book-marshal.list				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)     \
	e-name-western-tables.h.in                      \
	gen-western-table.py

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

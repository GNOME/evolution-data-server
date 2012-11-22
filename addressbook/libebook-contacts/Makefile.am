CLEANFILES=
BUILT_SOURCES=

ENUM_TYPES = e-book-contacts-types.h

e-book-contacts-enumtypes.h: e-book-contacts-enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-book-contacts-enumtypes.h.template $(ENUM_TYPES)) > $@

e-book-contacts-enumtypes.c: e-book-contacts-enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-book-contacts-enumtypes.c.template $(ENUM_TYPES)) > $@

ENUM_GENERATED = e-book-contacts-enumtypes.h e-book-contacts-enumtypes.c

# The marshallers
MARSHAL_GENERATED = e-book-contacts-marshal.c e-book-contacts-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libebook-contacts-1.2.la

libebook_contacts_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DLIBEBOOK_CONTACTS_COMPILATION			\
	-DLOCALEDIR=\""$(localedir)"\"			\
	-DG_LOG_DOMAIN=\"libebook-contacts\"		\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)				\
	-I$(top_builddir)/addressbook			\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)			\
	$(CAMEL_CFLAGS)					\
	$(CODE_COVERAGE_CFLAGS)				\
	$(NULL)

libebook_contacts_1_2_la_SOURCES =				\
	$(ENUM_GENERATED)				\
	$(MARSHAL_GENERATED)				\
	e-address-western.c				\
	e-name-western.c				\
	e-name-western-tables.h                         \
	e-book-query.c					\
	e-contact.c					\
	e-vcard.c					\
	e-source-address-book-config.c			\
	e-source-backend-summary-setup.c


libebook_contacts_1_2_la_LIBADD =					\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(CAMEL_LIBS)

libebook_contacts_1_2_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CONTACTS_CURRENT):$(LIBEBOOK_CONTACTS_REVISION):$(LIBEBOOK_CONTACTS_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libebookcontactsincludedir = $(privincludedir)/libebook-contacts

libebookcontactsinclude_HEADERS =			\
	libebook-contacts.h				\
	e-book-contacts-enumtypes.h			\
	e-book-contacts-types.h				\
	e-book-query.h					\
	e-address-western.h				\
	e-name-western.h				\
	e-contact.h					\
	e-vcard.h					\
	e-source-address-book-config.h			\
	e-source-backend-summary-setup.h


%-$(API_VERSION).pc: %.pc
	 cp $< $@

e-name-western-tables.h: e-name-western-tables.h.in
	$(AM_V_GEN) ($(PYTHON) ${srcdir}/gen-western-table.py < $< > $@.tmp && mv $@.tmp $@) || rm -f $@.tmp

BUILT_SOURCES += e-name-western-tables.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-contacts-$(API_VERSION).pc


-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_builddir)/libedataserver \
	--add-include-path=$(srcdir) --identifier-prefix=E --pkg-export \
	libebook-contacts-1.2 --c-include="libebook-contacts/libebook-contacts.h" --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_builddir)/libedataserver \
	--includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = $(libebook_contacts_1_2_la_SOURCES)
EBookContacts-1.2.gir: libebook-contacts-1.2.la

EBookContacts_1_2_gir_INCLUDES = GObject-2.0 GLib-2.0 libxml2-2.0 \
	Gio-2.0 \
	$(NULL)
libxml_include = `$(PKG_CONFIG) --cflags libxml-2.0`
EBookContacts_1_2_gir_CFLAGS = $(INCLUDES) \
		       $(libxml_include) \
		       -I$(top_srcdir) \
		       -I$(top_builddir) \
		       -I$(top_srcdir)/addressbook/ \
		       -I$(top_builddir)/addressbook/ \
		       -I$(top_srcdir)/addressbook/libebook-contacts \
		       -I$(top_builddir)/addressbook/libebook-contacts \
			-DLIBEBOOK_CONTACTS_COMPILATION		\
		       $(EVOLUTION_ADDRESSBOOK_CFLAGS) \
		       $(CAMEL_CFLAGS) \
			$(NULL)
EBookContacts_1_2_gir_SCANNERFLAGS = --include-uninstalled=$(top_builddir)/libedataserver/EDataServer-1.2.gir
EBookContacts_1_2_gir_LIBS = \
	libebook-contacts-1.2.la $(top_builddir)/libedataserver/libedataserver-1.2.la
EBookContacts_1_2_gir_FILES = $(libebook_contacts_1_2_la_SOURCES) $(libebookcontactsinclude_HEADERS)

INTROSPECTION_GIRS += EBookContacts-1.2.gir

girdir = $(datadir)/gir-1.0
dist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

BUILT_SOURCES += $(ENUM_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES    += $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	e-book-contacts-enumtypes.h.template			\
	e-book-contacts-enumtypes.c.template			\
	e-book-contacts-marshal.list	\
	e-name-western-tables.h.in                      \
	gen-western-table.py

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

NULL =

CLEANFILES=
BUILT_SOURCES=

ENUM_TYPES = e-book-contacts-types.h

e-book-contacts-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef E_BOOK_CONTACTS_ENUMTYPES_H\n#define E_BOOK_CONTACTS_ENUMTYPES_H\n" \
		--ftail "#endif /* E_BOOK_CONTACTS_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

e-book-contacts-enumtypes.c: $(top_srcdir)/enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"e-book-contacts-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = e-book-contacts-enumtypes.h e-book-contacts-enumtypes.c

# The library
lib_LTLIBRARIES = libebook-contacts-1.2.la
noinst_LTLIBRARIES =

if ENABLE_PHONENUMBER
noinst_LTLIBRARIES += libephonenumber-private.la
endif # ENABLE_PHONENUMBER

libebook_contacts_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DLIBEBOOK_CONTACTS_COMPILATION \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DG_LOG_DOMAIN=\"libebook-contacts\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir) \
	-I$(top_builddir)/addressbook \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebook_contacts_1_2_la_SOURCES = \
	$(ENUM_GENERATED) \
	e-address-western.c \
	e-name-western.c \
	e-name-western-tables.h \
	e-book-query.c \
	e-contact.c \
	e-vcard.c \
	e-phone-number.c \
	e-phone-number-private.h \
	e-book-contacts-types.c \
	e-source-backend-summary-setup.c

libebook_contacts_1_2_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(CAMEL_LIBS)

libebook_contacts_1_2_la_LDFLAGS = \
	-version-info $(LIBEBOOK_CONTACTS_CURRENT):$(LIBEBOOK_CONTACTS_REVISION):$(LIBEBOOK_CONTACTS_AGE) $(NO_UNDEFINED) \
	-export-symbols-regex "^e_.*" \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libebookcontactsincludedir = $(privincludedir)/libebook-contacts

libebookcontactsinclude_HEADERS = \
	libebook-contacts.h \
	e-book-contacts-enumtypes.h \
	e-book-contacts-types.h \
	e-book-query.h \
	e-address-western.h \
	e-name-western.h \
	e-contact.h \
	e-vcard.h \
	e-phone-number.h \
	e-source-backend-summary-setup.h \
	$(NULL)

if ENABLE_PHONENUMBER

# We put the C++ code into a separate static library, so that we can use
# the C linker for libebook-contacts. This avoids that libebook-contacts
# depends on the C++ Standard Library, even if phone number support is
# disabled.
libephonenumber_private_la_SOURCES = e-phone-number-private.cpp
libephonenumber_private_la_CPPFLAGS = $(libebook_contacts_1_2_la_CPPFLAGS) $(PHONENUMBER_INCLUDES)
libebook_contacts_1_2_la_LIBADD += @predeps_CXX@ libephonenumber-private.la $(PHONENUMBER_LIBS) @postdeps_CXX@

endif # ENABLE_PHONENUMBER

%-$(API_VERSION).pc: %.pc
	 cp $< $@

e-name-western-tables.h: e-name-western-tables.h.in
	$(AM_V_GEN) ($(PYTHON) ${srcdir}/gen-western-table.py < $< > $@.tmp && mv $@.tmp $@) || rm -f $@.tmp

BUILT_SOURCES += e-name-western-tables.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-contacts-$(API_VERSION).pc


-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = \
	--add-include-path=$(top_builddir)/libedataserver \
	--add-include-path=$(srcdir) \
	--identifier-prefix=E \
	--pkg-export libebook-contacts-1.2 \
	--c-include="libebook-contacts/libebook-contacts.h" \
	$(NULL)
INTROSPECTION_COMPILER_ARGS = \
	--includedir=$(top_builddir)/libedataserver \
	--includedir=$(srcdir) \
	$(NULL)

if HAVE_INTROSPECTION
introspection_sources = $(libebook_contacts_1_2_la_SOURCES)
EBookContacts-1.2.gir: libebook-contacts-1.2.la

EBookContacts_1_2_gir_INCLUDES = GObject-2.0 GLib-2.0 libxml2-2.0 Gio-2.0
libxml_include = `$(PKG_CONFIG) --cflags libxml-2.0`
EBookContacts_1_2_gir_CFLAGS = \
	$(INCLUDES) \
	$(libxml_include) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook/ \
	-I$(top_builddir)/addressbook/ \
	-I$(top_srcdir)/addressbook/libebook-contacts \
	-I$(top_builddir)/addressbook/libebook-contacts \
	-DLIBEBOOK_CONTACTS_COMPILATION \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)
EBookContacts_1_2_gir_SCANNERFLAGS = \
	--include-uninstalled=$(top_builddir)/libedataserver/EDataServer-1.2.gir
EBookContacts_1_2_gir_LIBS = \
	libebook-contacts-1.2.la $(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(NULL)
EBookContacts_1_2_gir_FILES = \
	$(libebook_contacts_1_2_la_SOURCES) \
	$(libebookcontactsinclude_HEADERS) \
	$(NULL)

INTROSPECTION_GIRS += EBookContacts-1.2.gir

girdir = $(datadir)/gir-1.0
dist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

BUILT_SOURCES += $(ENUM_GENERATED)
CLEANFILES    += $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	e-name-western-tables.h.in \
	gen-western-table.py \
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

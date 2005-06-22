INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-DG_LOG_DOMAIN=\"libebook\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

# The corba stubs and skels
CORBA_GENERATED_C =				\
	Evolution-DataServer-Addressbook-common.c		\
	Evolution-DataServer-Addressbook-skels.c		\
	Evolution-DataServer-Addressbook-stubs.c
CORBA_GENERATED_H =			\
	Evolution-DataServer-Addressbook.h

CORBA_GENERATED = $(CORBA_GENERATED_C) $(CORBA_GENERATED_H)

idls =						\
	$(srcdir)/../idl/Evolution-DataServer-Addressbook.idl

idl_flags = $(IDL_INCLUDES)

$(CORBA_GENERATED_H): $(idls)
	$(ORBIT_IDL) $(idl_flags) $(srcdir)/../idl/Evolution-DataServer-Addressbook.idl
$(CORBA_GENERATED_C): $(CORBA_GENERATED_H)

# The marshallers
MARSHAL_GENERATED = e-book-marshal.c e-book-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libebook-1.2.la

libebook_1_2_la_SOURCES =					\
	$(CORBA_GENERATED_C)				\
	$(MARSHAL_GENERATED)				\
	e-address-western.c				\
	e-book-listener.c				\
	e-book-query.c					\
	e-book-view-listener.c				\
	e-book-view.c					\
	e-book.c					\
	e-contact.c					\
	e-destination.c					\
	e-name-western.c				\
	e-vcard.c

libebook_1_2_la_LIBADD =					\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la

libebook_1_2_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CURRENT):$(LIBEBOOK_REVISION):$(LIBEBOOK_AGE) $(NO_UNDEFINED)

libebookincludedir = $(privincludedir)/libebook

libebookinclude_HEADERS =				\
	$(CORBA_GENERATED_H)				\
	e-address-western.h				\
	e-book-listener.h				\
	e-book-query.h					\
	e-book-view-listener.h				\
	e-book-view.h					\
	e-book-types.h					\
	e-book.h					\
	e-contact.h					\
	e-destination.h					\
	e-name-western.h				\
	e-name-western-tables.h				\
	e-vcard.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-$(API_VERSION).pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	e-book-marshal.list				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

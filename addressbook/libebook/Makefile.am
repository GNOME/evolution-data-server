INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-DG_LOG_DOMAIN=\"libebook\"			\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

AM_CFLAGS = 								\
	-DG_DISABLE_DEPRECATED=1					\
	-DGNOME_DISABLE_DEPRECATED=1

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
lib_LTLIBRARIES = libebook.la

libebook_la_SOURCES =					\
	$(CORBA_GENERATED_C)				\
	$(MARSHAL_GENERATED)				\
	e-book-async.c					\
	e-book-listener.c				\
	e-book-query.c					\
	e-book-view-listener.c				\
	e-book-view.c					\
	e-book.c					\
	e-contact.c					\
	e-vcard.c

libebook_la_LIBADD =					\
	$(top_builddir)/libedataserver/libedataserver.la

libebook_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CURRENT):$(LIBEBOOK_REVISION):$(LIBEBOOK_AGE)	\
	-no-undefined

libebookincludedir = $(privincludedir)/libebook

libebookinclude_HEADERS =				\
	$(CORBA_GENERATED_H)				\
	e-book-async.h					\
	e-book-listener.h				\
	e-book-query.h					\
	e-book-view-listener.h				\
	e-book-view.h					\
	e-book-types.h					\
	e-book.h					\
	e-contact.h					\
	e-vcard.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebook-1.0.pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	e-book-marshal.list		\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

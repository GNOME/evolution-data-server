INCLUDES =						\
	-DG_LOG_DOMAIN=\"libedata-book\"		\
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

MARSHAL_GENERATED = e-data-book-marshal.c e-data-book-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libedata-book.la

libedata_book_la_SOURCES =				\
	$(MARSHAL_GENERATED)				\
	$(CORBA_GENERATED_C)				\
	e-book-backend-sexp.c				\
	e-book-backend-summary.c			\
	e-book-backend-sync.c				\
	e-book-backend.c				\
	e-data-book-factory.c				\
	e-data-book-view.c				\
	e-data-book.c					\
	ximian-vcard.h

libedata_book_la_LIBADD =					\
	$(top_builddir)/addressbook/libebook/libebook.la	\
	$(top_builddir)/libedataserver/libedataserver.la

libedata_book_la_LDFLAGS = 									\
	-version-info $(LIBEDATABOOK_CURRENT):$(LIBEDATABOOK_REVISION):$(LIBEDATABOOK_AGE)

libedata_bookincludedir = $(privincludedir)/libedata-book

libedata_bookinclude_HEADERS =				\
	$(CORBA_GENERATED_H)				\
	e-book-backend-sexp.h				\
	e-book-backend-summary.h			\
	e-book-backend-sync.h				\
	e-book-backend.h				\
	e-data-book-factory.h				\
	e-data-book-types.h				\
	e-data-book-view.h				\
	e-data-book.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-book-1.0.pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	e-data-book-marshal.list	\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

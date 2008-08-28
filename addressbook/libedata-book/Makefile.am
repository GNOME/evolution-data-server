INCLUDES =						\
	-DG_LOG_DOMAIN=\"libedata-book\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(DB_CFLAGS)					\
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

# The library
lib_LTLIBRARIES = libedata-book-1.2.la

libedata_book_1_2_la_SOURCES =				\
	$(CORBA_GENERATED_C)				\
	e-book-backend-factory.c			\
	e-book-backend-sexp.c				\
	e-book-backend-summary.c			\
	e-book-backend-cache.c                          \
	e-book-backend-db-cache.c                       \
	e-book-backend-sync.c				\
	e-book-backend.c				\
	e-data-book-factory.c				\
	e-data-book-view.c				\
	e-data-book.c					\
	ximian-vcard.h

libedata_book_1_2_la_LIBADD =					\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(top_builddir)/libebackend/libebackend-1.2.la	\
	$(DB_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS)

libedata_book_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATABOOK_CURRENT):$(LIBEDATABOOK_REVISION):$(LIBEDATABOOK_AGE) $(NO_UNDEFINED)

libedata_bookincludedir = $(privincludedir)/libedata-book

libedata_bookinclude_HEADERS =				\
	$(CORBA_GENERATED_H)				\
	e-book-backend-factory.h			\
	e-book-backend-sexp.h				\
	e-book-backend-summary.h			\
	e-book-backend-sync.h				\
	e-book-backend.h				\
	e-data-book-factory.h				\
	e-data-book-types.h				\
	e-data-book-view.h				\
	e-data-book.h                                   \
	e-book-backend-cache.h 				\
	e-book-backend-db-cache.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-book-$(API_VERSION).pc

BUILT_SOURCES = $(CORBA_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

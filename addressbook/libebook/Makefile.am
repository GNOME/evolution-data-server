INCLUDES =						\
	-DPREFIX=\"$(prefix)\"				\
	-DSYSCONFDIR=\"$(sysconfdir)\"			\
	-DDATADIR=\"$(datadir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DG_LOG_DOMAIN=\"EBook\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_srcdir)					\
	-DG_DISABLE_DEPRECATED				\
	-DLIBGNOME_DISABLE_DEPRECATED			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

# The corba stubs and skels
CORBA_ADDRESSBOOK_SOURCE_H =			\
	addressbook.h
CORBA_ADDRESSBOOK_SOURCE_C =			\
	addressbook-common.c			\
	addressbook-stubs.c			\
	addressbook-skels.c
CORBA_ADDRESSBOOK_IDL = $(srcdir)/../idl/addressbook.idl

$(CORBA_ADDRESSBOOK_SOURCE_H): $(CORBA_ADDRESSBOOK_IDL)
	$(ORBIT_IDL) -I $(srcdir) $(IDL_INCLUDES) $(CORBA_ADDRESSBOOK_IDL)

$(CORBA_ADDRESSBOOK_SOURCE_C): $(CORBA_ADDRESSBOOK_SOURCE_H)

CORBA_SOURCE_H = $(CORBA_ADDRESSBOOK_SOURCE_H)
CORBA_SOURCE_C = $(CORBA_ADDRESSBOOK_SOURCE_C)
CORBA_SOURCE = $(CORBA_SOURCE_H) $(CORBA_SOURCE_C)

# The marshallers
MARSHAL_GENERATED = e-book-marshal.c e-book-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libebook.la

libebook_la_SOURCES =					\
	$(CORBA_SOURCE_C)				\
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
	$(top_builddir)/libedataserver/libedataserver.la\
	$(top_builddir)/libedataserver/ename/libename.la

libebook_la_LDFLAGS = 							\
	-version-info $(LIBEBOOK_CURRENT):$(LIBEBOOK_REVISION):$(LIBEBOOK_AGE)	\
	-no-undefined

libebookincludedir = $(includedir)/evolution-data-server-1.0/libebook

libebookinclude_HEADERS =				\
	$(CORBA_SOURCE_H)				\
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

BUILT_SOURCES = $(CORBA_SOURCE) $(MARSHAL_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	e-book-marshal.list		\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

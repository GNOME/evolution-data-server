INCLUDES =						\
	-DG_LOG_DOMAIN=\"libedatabook\"			\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(DB3_CFLAGS)					\
	$(LDAP_CFLAGS)					\
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

LDAP_SCHEMA =						\
	evolutionperson.schema

if ENABLE_LDAP
LDAP_SOURCE_C = e-book-backend-ldap.c
LDAP_SOURCE_H = e-book-backend-ldap.h
else
LDAP_SOURCE_C =
LDAP_SOURCE_H =
endif

LDAP_SOURCE = e-book-backend-ldap.c e-book-backend-ldap.h

ldapschemadir = $(privdatadir)
ldapschema_DATA= $(LDAP_SCHEMA)

# The library
lib_LTLIBRARIES = libedatabook.la

libedatabook_la_SOURCES =				\
	$(MARSHAL_GENERATED)				\
	$(CORBA_GENERATED_C)				\
	e-book-backend-file.c				\
	$(LDAP_SOURCE_C)				\
	e-book-backend-vcf.c				\
	e-book-backend-sexp.c				\
	e-book-backend-summary.c			\
	e-book-backend-sync.c				\
	e-book-backend.c				\
	e-data-book-factory.c				\
	e-data-book-view.c				\
	e-data-book.c					\
	ximian-vcard.h

libedatabook_la_LIBADD =					\
	$(LDAP_LIBS)						\
	$(top_builddir)/addressbook/libebook/libebook.la	\
	$(top_builddir)/libedataserver/libedataserver.la

libedatabook_la_LDFLAGS = 									\
	-version-info $(LIBEDATABOOK_CURRENT):$(LIBEDATABOOK_REVISION):$(LIBEDATABOOK_AGE)	\
	-no-undefined

libedatabookincludedir = $(privincludedir)/libedatabook

libedatabookinclude_HEADERS =				\
	$(CORBA_GENERATED_H)				\
	e-book-backend-file.h				\
	$(LDAP_SOURCE_H)				\
	e-book-backend-vcf.h				\
	e-book-backend-sexp.h				\
	e-book-backend-summary.h			\
	e-book-backend-sync.h				\
	e-book-backend.h				\
	e-data-book-factory.h				\
	e-data-book-types.h				\
	e-data-book-view.h				\
	e-data-book.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedatabook-1.0.pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	$(LDAP_SOURCE) 			\
	$(LDAP_SCHEMA) 			\
	e-data-book-marshal.list	\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

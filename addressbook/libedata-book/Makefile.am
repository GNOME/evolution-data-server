DBUS_GENERATED_H = e-data-book-glue.h e-data-book-factory-glue.h e-data-book-view-glue.h
%-glue.h: %.xml
	@DBUS_BINDING_TOOL@ --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $^

include $(top_srcdir)/glib-gen.mak
glib_enum_headers=e-data-book-types.h
glib_enum_define=E_DATA_BOOK
glib_enum_prefix=e_data_book

ENUM_GENERATED = e-data-book-enumtypes.h e-data-book-enumtypes.c

# The library
lib_LTLIBRARIES = libedata-book-1.2.la

libedata_book_1_2_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libedata-book\"		\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(DB_CFLAGS)					\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

libedata_book_1_2_la_SOURCES =				\
	$(DBUS_GENERATED_H)				\
	e-book-backend-factory.c			\
	e-book-backend-sexp.c				\
	e-book-backend-summary.c			\
	e-book-backend-cache.c                          \
	e-book-backend-db-cache.c                       \
	e-book-backend-sync.c				\
	e-book-backend.c				\
	e-data-book-view.c				\
	e-data-book.c					\
	ximian-vcard.h					\
	opid.c opid.h					\
	$(ENUM_GENERATED)

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

factorydir = $(libexecdir)

factory_PROGRAMS = e-addressbook-factory

e_addressbook_factory_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)		\
	-DG_LOG_DOMAIN=\"libedata-book\"	\
	-DE_PRGNAME=\"e-addressbook-factory\"	\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	-I$(top_srcdir)/addressbook		\
	-I$(top_builddir)/addressbook

e_addressbook_factory_SOURCES = e-data-book-factory.c e-data-book-factory.h

e_addressbook_factory_LDADD =				\
	libedata-book-1.2.la				\
	$(top_builddir)/libebackend/libebackend-1.2.la

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-book-$(API_VERSION).pc

service_in_files = org.gnome.evolution.dataserver.AddressBook.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@

BUILT_SOURCES = $(DBUS_GENERATED_H) $(ENUM_GENERATED)
CLEANFILES = $(BUILT_SOURCES) $(service_DATA)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	e-data-book-factory.xml				\
	e-data-book-view.xml				\
	e-data-book.xml					\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	$(service_in_files)

-include $(top_srcdir)/git.mk

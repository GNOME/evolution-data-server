# The library
lib_LTLIBRARIES = libedata-book-1.2.la

libedata_book_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DLIBEDATA_BOOK_COMPILATION \
	-DG_LOG_DOMAIN=\"libedata-book\" \
	-DBACKENDDIR=\"$(ebook_backenddir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_srcdir)/addressbook/libegdbus \
	-I$(top_builddir) \
	-I$(top_builddir)/addressbook \
	$(DB_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(GOA_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libedata_book_1_2_la_SOURCES = \
	e-book-backend-factory.c \
	e-book-backend-sexp.c \
	e-book-backend-summary.c \
	e-book-backend-cache.c \
	e-book-backend-db-cache.c \
	e-book-backend-sqlitedb.c \
	e-book-backend-sync.c \
	e-book-backend.c \
	e-data-book.c \
	e-data-book-factory.c \
	e-data-book-view.c \
	ximian-vcard.h

libedata_book_1_2_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libegdbus/libegdbus-book.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(DB_LIBS) \
	$(CAMEL_LIBS) \
	$(SQLITE3_LIBS) \
	$(GOA_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS)

libedata_book_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATABOOK_CURRENT):$(LIBEDATABOOK_REVISION):$(LIBEDATABOOK_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libedata_bookincludedir = $(privincludedir)/libedata-book

libedata_bookinclude_HEADERS = \
	libedata-book.h \
	e-book-backend-factory.h \
	e-book-backend-sexp.h \
	e-book-backend-summary.h \
	e-book-backend-sync.h \
	e-book-backend.h \
	e-data-book-factory.h \
	e-data-book-view.h \
	e-data-book.h \
	e-book-backend-cache.h \
	e-book-backend-sqlitedb.h \
	e-book-backend-db-cache.h

factorydir = $(libexecdir)

noinst_PROGRAMS = e-book-backend-sqlitedb-test

e_book_backend_sqlitedb_test_CPPFLAGS = \
	$(libedata_book_1_2_la_CPPFLAGS)
e_book_backend_sqlitedb_test_SOURCES =	e-book-backend-sqlitedb-test.c
e_book_backend_sqlitedb_test_LDADD = \
	libedata-book-1.2.la \
	$(libedata_book_1_2_la_LIBADD)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-book-$(API_VERSION).pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = \
	e-data-book-factory.xml \
	e-data-book-view.xml \
	e-data-book.xml \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

-include $(top_srcdir)/git.mk

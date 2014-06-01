NULL =

if HAVE_LIBDB
LIBDB_FILES = \
	e-book-backend-file-migrate-bdb.c \
	e-book-backend-file-migrate-bdb.h
endif

ebook_backend_LTLIBRARIES = libebookbackendfile.la

libebookbackendfile_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libebookbackend\" \
	-DBACKENDDIR="\"$(ebook_backenddir)\"" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(DB_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebookbackendfile_la_SOURCES = \
	e-book-backend-file.c \
	e-book-backend-file.h \
	e-book-backend-file-factory.c \
	$(LIBDB_FILES) \
	$(NULL)

libebookbackendfile_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(DB_LIBS) \
	$(CAMEL_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(NULL)

libebookbackendfile_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

NULL =

ebook_backend_LTLIBRARIES = libebookbackendwebdav.la

libebookbackendwebdav_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libebookbackendwebdav\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(SOUP_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebookbackendwebdav_la_SOURCES = \
	e-book-backend-webdav-factory.c \
	e-book-backend-webdav.h \
	e-book-backend-webdav.c \
	$(NULL)

libebookbackendwebdav_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(SOUP_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

libebookbackendwebdav_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

NULL =

SUBDIRS = . tests

ebook_backend_LTLIBRARIES = libebookbackendgoogle.la

libebookbackendgoogle_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"e-book-backend-google\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(GDATA_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebookbackendgoogle_la_SOURCES = \
	e-book-backend-google-factory.c \
	e-book-backend-google.c \
	e-book-backend-google.h \
	e-book-google-utils.c \
	e-book-google-utils.h \
	e-gdata-oauth2-authorizer.c \
	e-gdata-oauth2-authorizer.h \
	$(NULL)

libebookbackendgoogle_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(SOUP_LIBS) \
	$(GDATA_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

libebookbackendgoogle_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

# Private utility library.
# This is split out to allow it to be unit tested.
noinst_LTLIBRARIES = libebook-google-utils.la

libebook_google_utils_la_SOURCES = \
	e-book-google-utils.c \
	e-book-google-utils.h \
	$(NULL)

# libcamel is dragged in by the libebook headers
libebook_google_utils_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(GDATA_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebook_google_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libebookbackendgoogle\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(NULL)

libebook_google_utils_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(GDATA_LIBS) \
	$(NULL)

libebook_google_utils_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

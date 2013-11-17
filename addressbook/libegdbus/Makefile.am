NULL =

noinst_LTLIBRARIES = libegdbus-book.la

libegdbus_book_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libegdbus-book\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/addressbook \
	$(E_DATA_SERVER_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libegdbus_book_la_SOURCES = \
	e-gdbus-book-view.h \
	e-gdbus-book-view.c \
	$(NULL)

libegdbus_book_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

libegdbus_book_la_LDFLAGS = \
	$(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

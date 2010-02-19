extension_LTLIBRARIES = libebookbackendgoogle.la

libebookbackendgoogle_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libebookbackendgoogle\" 	\
	-I$(top_srcdir) 				\
	-I$(top_builddir) 				\
	-I$(top_srcdir)/addressbook 			\
	-I$(top_builddir)/addressbook 			\
	-I$(top_srcdir)/servers/google/libgdata 	\
	-I$(top_builddir)/servers/google/libgdata 	\
	-I$(top_srcdir)/servers/google/libgdata-google 	\
	-I$(top_builddir)/servers/google/libgdata-google \
	$(SOUP_CFLAGS) 					\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

libebookbackendgoogle_la_SOURCES = \
	util.h \
	util.c \
	google-book.h \
	google-book.c \
	e-book-backend-google-factory.c \
	e-book-backend-google.h \
	e-book-backend-google.c

libebookbackendgoogle_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/servers/google/libgdata/libgdata-1.2.la \
	$(top_builddir)/servers/google/libgdata-google/libgdata-google-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(SOUP_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS)

libebookbackendgoogle_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

extension_LTLIBRARIES = libebookbackendwebdav.la

libebookbackendwebdav_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libebookbackendgoogle\" 	\
	-I$(top_srcdir) 				\
	-I$(top_builddir) 				\
	-I$(top_srcdir)/addressbook 			\
	-I$(top_builddir)/addressbook 			\
	$(SOUP_CFLAGS) 					\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

libebookbackendwebdav_la_SOURCES = \
	e-book-backend-webdav-factory.c \
	e-book-backend-webdav.h \
	e-book-backend-webdav.c

libebookbackendwebdav_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(SOUP_LIBS) \
	$(EVOLUTION_ADDRESSBOOK_LIBS)

libebookbackendwebdav_la_LDFLAGS = -module -avoid-version $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(DB3_CFLAGS)					\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_LTLIBRARIES = libebookbackendfile.la

libebookbackendfile_la_SOURCES =			\
	e-book-backend-file.c				\
	e-book-backend-file.h

libebookbackendfile_la_LIBADD =						\
	$(top_builddir)/addressbook/libedata-book/libedata-book.la	\
	$(top_builddir)/libedataserver/libedataserver.la

INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_srcdir)/servers/groupwise               \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)                \
	$(SOUP_CFLAGS)	
noinst_LTLIBRARIES = libebookbackendgroupwise.la

libebookbackendgroupwise_la_SOURCES =			\
	e-book-backend-groupwise.c				\
	e-book-backend-groupwise.h

libebookbackendgroupwise_la_LIBADD =						\
	$(top_builddir)/servers/groupwise/libegroupwise.la
	$(SOUP_LIBS)

noinst_PROGRAMS = create-account
create_account_SOURCES = create-account.c

create_account_LDADD =  $(top_builddir)/addressbook/libedata-book/libedata-book.la      \
        $(top_builddir)/libedataserver/libedataserver.la              \
        $(top_builddir)/servers/groupwise/libegroupwise.la \
        $(E_DATA_SERVER_LIBS)


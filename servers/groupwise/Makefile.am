INCLUDES =						\
	-DG_LOG_DOMAIN=\"libegroupwise\"		\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(SOUP_CFLAGS)					\
	$(E_DATA_SERVER_CFLAGS)				\
	$(DEBUG_CFLAGS)

#noinst_PROGRAMS = create-account soap-test

#create_account_SOURCES = create-account.c
#create_account_LDADD = \
#	$(SOUP_LIBS)							\
#	$(E_DATA_SERVER_LIBS)

#soap_test_SOURCES = soap-test.c
#soap_test_LDADD = 						\
#	$(SOUP_LIBS)						\
#	$(top_builddir)/servers/groupwise/libegroupwise.la	\
#	$(E_DATA_SERVER_LIBS)

noinst_LTLIBRARIES = libegroupwise.la

libegroupwise_la_SOURCES =			\
	e-gw-connection.c			\
	e-gw-connection.h			\
	e-gw-container.c			\
	e-gw-container.h			\
	e-gw-item.c				\
	e-gw-item.h				\
	e-gw-message.c				\
	e-gw-message.h

libegroupwise_la_LIBADD =						\
	$(SOUP_LIBS)

INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendgroupwise\"	\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/backends/file		\
	-I$(top_builddir)/calendar/backends/file	\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)

noinst_PROGRAMS = create-account soap-test

create_account_SOURCES = create-account.c
create_account_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la           \
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la    \
	$(SOUP_LIBS)							\
	$(E_DATA_SERVER_LIBS)

soap_test_SOURCES = soap-test.c
soap_test_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la           	\
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la    	\
	$(top_builddir)/calendar/backends/groupwise/libecalbackendgroupwise.la	\
	$(SOUP_LIBS)								\
	$(E_DATA_SERVER_LIBS)

noinst_LTLIBRARIES = libecalbackendgroupwise.la

libecalbackendgroupwise_la_SOURCES =		\
	e-cal-backend-groupwise.c		\
	e-cal-backend-groupwise.h		\
	e-gw-connection.c			\
	e-gw-connection.h			\
	e-gw-item.c				\
	e-gw-item.h				\
	e-gw-message.c				\
	e-gw-message.h

libecalbackendgroupwise_la_LIBADD =					\
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la		\
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la	\
	$(SOUP_LIBS)

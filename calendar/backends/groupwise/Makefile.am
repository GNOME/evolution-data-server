INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendfile\"		\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/backends/file		\
	-I$(top_builddir)/calendar/backends/file	\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(GROUPWISE_CFLAGS)

noinst_PROGRAMS = create-account
create_account_SOURCES = create-account.c
create_account_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la           \
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la    \
	$(GROUPWISE_LIBS)

noinst_LTLIBRARIES = libecalbackendgroupwise.la

libecalbackendgroupwise_la_SOURCES =		\
	e-cal-backend-groupwise.c		\
	e-cal-backend-groupwise.h		\
	e-gw-connection.c			\
	e-gw-connection.h			\
	e-gw-message.c				\
	e-gw-message.h

libecalbackendgroupwise_la_LIBADD =					\
	$(top_builddir)/calendar/libedata-cal/libedata-cal.la		\
	$(top_builddir)/calendar/backends/file/libecalbackendfile.la	\
	$(GROUPWISE_LIBS)

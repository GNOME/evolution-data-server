INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendcontact\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(EVOLUTION_CALENDAR_CFLAGS)

noinst_LTLIBRARIES = libecalbackendcontacts.la

libecalbackendcontacts_la_SOURCES =	\
	e-cal-backend-contacts.c	\
	e-cal-backend-contacts.h

libecalbackendcontacts_la_LIBADD =	\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la


NULL =

noinst_LTLIBRARIES = libegdbus-cal.la

libegdbus_cal_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"libegdbus-cal\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	$(E_DATA_SERVER_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libegdbus_cal_la_SOURCES = \
	e-gdbus-cal-view.h \
	e-gdbus-cal-view.c \
	$(NULL)

libegdbus_cal_la_LIBADD = \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

libegdbus_cal_la_LDFLAGS = \
	$(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

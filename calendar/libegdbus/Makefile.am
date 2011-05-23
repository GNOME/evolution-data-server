# The library
noinst_LTLIBRARIES = libegdbus-cal.la

libegdbus_cal_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-cal\"	\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/calendar		\
	$(E_DATA_SERVER_CFLAGS)

libegdbus_cal_la_SOURCES =			\
	e-gdbus-cal.h				\
	e-gdbus-cal.c				\
	e-gdbus-cal-factory.h			\
	e-gdbus-cal-factory.c			\
	e-gdbus-cal-view.h			\
	e-gdbus-cal-view.c

libegdbus_cal_la_LIBADD =			\
	$(E_DATA_SERVER_LIBS)

libegdbus_cal_la_LDFLAGS = $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk

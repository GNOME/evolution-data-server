INCLUDES =                                                      \
	$(EVOLUTION_CALENDAR_CFLAGS)				\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/calendar                                \
	-I$(top_builddir)/calendar                              \
	-I$(top_srcdir)/calendar/libical/src		        \
	-I$(top_builddir)/calendar/libical/src		        \
	-I$(top_srcdir)/calendar/libical/src/libical	        \
	-I$(top_builddir)/calendar/libical/src/libical	        \
	$(SOUP_CFLAGS)					        \
	-DG_LOG_DOMAIN=\"e-cal-backend-caldav\"

extension_LTLIBRARIES = libecalbackendcaldav.la

libecalbackendcaldav_la_SOURCES =	\
	e-cal-backend-caldav-factory.c	\
	e-cal-backend-caldav-factory.h	\
	e-cal-backend-caldav.c		\
	e-cal-backend-caldav.h		

libecalbackendcaldav_la_LIBADD =						\
	$(EVOLUTION_CALENDAR_LIBS)

libecalbackendcaldav_la_LDFLAGS =		\
	-module -avoid-version

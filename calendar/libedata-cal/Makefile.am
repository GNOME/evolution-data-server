INCLUDES =						\
	-DG_LOG_DOMAIN=\"libedata-cal\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	$(EVOLUTION_CALENDAR_CFLAGS)

CORBA_GENERATED_H =				\
	Evolution-DataServer-Calendar.h

CORBA_GENERATED_C =				\
	Evolution-DataServer-Calendar-common.c		\
	Evolution-DataServer-Calendar-skels.c		\
	Evolution-DataServer-Calendar-stubs.c

CORBA_GENERATED = $(CORBA_GENERATED_H) $(CORBA_GENERATED_C)

idls =						\
	$(srcdir)/../idl/Evolution-DataServer-Calendar.idl

idl_flags = -I $(srcdir) $(IDL_INCLUDES)

$(CORBA_GENERATED_H): $(idls)
	$(ORBIT_IDL) $(idl_flags) $(srcdir)/../idl/Evolution-DataServer-Calendar.idl

$(CORBA_GENERATED_C): $(CORBA_GENERATED_H)

# The libraray
lib_LTLIBRARIES = libedata-cal-1.2.la

libedata_cal_1_2_la_SOURCES =		\
	$(CORBA_GENERATED_C)		\
	e-cal-backend.c			\
	e-cal-backend-cache.c		\
	e-cal-backend-factory.c		\
	e-cal-backend-sexp.c		\
	e-cal-backend-sync.c		\
	e-cal-backend-util.c		\
	e-data-cal.c			\
	e-data-cal-factory.c		\
	e-data-cal-view.c

libedata_cal_1_2_la_LIBADD =					\
	$(top_builddir)/calendar/libecal/libecal-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS)

libedata_cal_1_2_la_LDFLAGS = 								\
	-version-info $(LIBEDATACAL_CURRENT):$(LIBEDATACAL_REVISION):$(LIBEDATACAL_AGE) $(NO_UNDEFINED)

libedata_calincludedir = $(privincludedir)/libedata-cal

libedata_calinclude_HEADERS = 		\
	$(CORBA_GENERATED_H)		\
	e-cal-backend.h			\
	e-cal-backend-cache.h		\
	e-cal-backend-factory.h		\
	e-cal-backend-sync.h		\
	e-cal-backend-util.h		\
	e-cal-backend-sexp.h		\
	e-data-cal.h			\
	e-data-cal-common.h		\
	e-data-cal-factory.h		\
	e-data-cal-view.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-cal-$(API_VERSION).pc

BUILT_SOURCES = $(CORBA_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)


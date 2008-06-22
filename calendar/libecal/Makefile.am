INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecal\"			\
	-I$(top_srcdir)/calendar			\
	-I$(srcdir) -I$(top_srcdir) 			\
	-I. 						\
	-I.. 						\
	-I$(top_builddir)				\
	-I$(top_builddir)/calendar/libecal		\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	$(EVOLUTION_CALENDAR_CFLAGS)

# The corba stubs and skels
CORBA_GENERATED_C =				\
	Evolution-DataServer-Calendar-common.c		\
	Evolution-DataServer-Calendar-skels.c		\
	Evolution-DataServer-Calendar-stubs.c
CORBA_GENERATED_H =			\
	Evolution-DataServer-Calendar.h

CORBA_GENERATED = $(CORBA_GENERATED_C) $(CORBA_GENERATED_H)

idls =						\
	$(srcdir)/../idl/Evolution-DataServer-Calendar.idl

idl_flags = $(IDL_INCLUDES)

$(CORBA_GENERATED_H): $(idls)
	$(ORBIT_IDL) $(idl_flags) $(srcdir)/../idl/Evolution-DataServer-Calendar.idl
$(CORBA_GENERATED_C): $(CORBA_GENERATED_H)

# The marshallers
MARSHAL_GENERATED = e-cal-marshal.c e-cal-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libecal-1.2.la

libecal_1_2_la_SOURCES =	\
	$(CORBA_GENERATED_C)	\
	$(CORBA_GENERATED_H)	\
	$(MARSHAL_GENERATED)	\
	e-cal.c			\
	e-cal-component.c	\
	e-cal-listener.c	\
	e-cal-listener.h	\
	e-cal-recur.c		\
	e-cal-time-util.c	\
        e-cal-check-timezones.c \
	e-cal-util.c		\
	e-cal-view.c		\
	e-cal-view-listener.c	\
	e-cal-view-listener.h	\
	e-cal-view-private.h

libecal_1_2_la_LIBADD =									\
	$(top_builddir)/calendar/libical/src/libical/libical-evolution.la		\
	$(top_builddir)/calendar/libical/src/libicalvcal/libicalvcal-evolution.la	\
	$(top_builddir)/calendar/libical/src/libicalss/libicalss-evolution.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS)

libecal_1_2_la_LDFLAGS = 								\
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE) $(NO_UNDEFINED)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS =	\
	e-cal.h			\
	e-cal-component.h	\
	e-cal-recur.h		\
	e-cal-time-util.h	\
        e-cal-check-timezones.h \
	e-cal-types.h		\
	e-cal-util.h		\
	e-cal-view.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libecal-$(API_VERSION).pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	e-cal-marshal.list				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)


INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-DG_LOG_DOMAIN=\"libecal\"			\
	-I$(top_srcdir)/calendar			\
	-I$(srcdir) -I$(top_srcdir) 			\
	-I. 						\
	-I.. 						\
	-I$(top_builddir)				\
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
lib_LTLIBRARIES = libecal.la

libecal_la_SOURCES =		\
	$(CORBA_GENERATED_C)	\
	$(MARSHAL_GENERATED)	\
	e-cal.c			\
	e-cal-component.c	\
	e-cal-listener.c	\
	e-cal-recur.c		\
	e-cal-time-util.c	\
	e-cal-util.c		\
	e-cal-view.c		\
	e-cal-view-listener.c

libecal_la_LIBADD =									\
	$(EVOLUTION_CALENDAR_LIBS)							\
	$(top_builddir)/calendar/libical/src/libical/libical-evolution.la		\
	$(top_builddir)/calendar/libical/src/libicalvcal/libicalvcal-evolution.la	\
	$(top_builddir)/calendar/libical/src/libicalss/libicalss-evolution.la	\
	$(top_builddir)/libedataserver/libedataserver.la

libecal_la_LDFLAGS = 								\
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS =	\
	$(CORBA_GENERATED_H)	\
	e-cal.h			\
	e-cal-component.h	\
	e-cal-listener.h	\
	e-cal-recur.h		\
	e-cal-time-util.h	\
	e-cal-types.h		\
	e-cal-util.h		\
	e-cal-view.h		\
	e-cal-view-listener.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libecal-1.0.pc

BUILT_SOURCES = $(CORBA_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	e-cal-marshal.list		\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

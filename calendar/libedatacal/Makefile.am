INCLUDES =						\
	-DG_LOG_DOMAIN=\"libedatacal\"			\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(EVOLUTION_CALENDAR_CFLAGS)

AM_CFLAGS = 								\
	-DG_DISABLE_DEPRECATED=1					\
	-DGNOME_DISABLE_DEPRECATED=1

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
lib_LTLIBRARIES = libedatacal.la

libedatacal_la_SOURCES =		\
	$(CORBA_GENERATED_C)		\
	e-cal-backend.c			 \
	e-cal-backend-file-events.c	\
	e-cal-backend-file-todos.c	\
	e-cal-backend-file.c		\
	e-cal-backend-http.c		\
	e-cal-backend-http.h		\
	e-cal-backend-sexp.c		\
	e-cal-backend-sync.c		\
	e-cal-backend-util.c		\
	e-data-cal.c			\
	e-data-cal-factory.c		\
	e-data-cal-view.c

libedatacal_la_LIBADD =						\
	$(top_builddir)/calendar/libecal/libecal.la		\
	$(top_builddir)/libedataserver/libedataserver.la

libedatacal_la_LDFLAGS = 								\
	-version-info $(LIBEDATACAL_CURRENT):$(LIBEDATACAL_REVISION):$(LIBEDATACAL_AGE)	\
	-no-undefined

libedatacalincludedir = $(includedir)/evolution-data-server-1.0/libedatacal

libedatacalinclude_HEADERS = 		\
	$(CORBA_GENERATED_H)		\
	e-cal-backend.h			\
	e-cal-backend-file-events.h	\
	e-cal-backend-file-todos.h	\
	e-cal-backend-file.h		\
	e-cal-backend-sync.h		\
	e-cal-backend-util.h		\
	e-cal-backend-sexp.h		\
	e-data-cal.h			\
	e-data-cal-common.h		\
	e-data-cal-factory.h		\
	e-data-cal-view.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedatacal-1.0.pc

BUILT_SOURCES = $(CORBA_GENERATED)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 				\
	$(pkgconfig_DATA:.pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)


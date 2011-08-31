# The marshallers
MARSHAL_GENERATED = e-cal-marshal.c e-cal-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libecal-1.2.la

libecal_INCLUDES = \
	-I$(top_srcdir) 			\
	-I$(top_srcdir)/calendar		\
	-I$(top_srcdir)/calendar/libegdbus	\
	-I$(top_srcdir)/calendar/libedata-cal	\
	-I$(top_builddir)			\
	-I$(top_builddir)/calendar/libecal

libecal_1_2_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(libecal_INCLUDES)			\
	-DG_LOG_DOMAIN=\"libecal\"		\
	$(LIBICAL_CFLAGS)			\
	$(EVOLUTION_CALENDAR_CFLAGS)

libecal_1_2_la_SOURCES =			\
	$(MARSHAL_GENERATED)			\
	e-cal.c					\
	e-cal-client.c				\
	e-cal-client-view.c			\
	e-cal-client-view-private.h		\
	e-cal-component.c			\
	e-cal-recur.c				\
	e-cal-time-util.c			\
	e-cal-check-timezones.c 		\
	e-cal-system-timezone.c			\
	e-cal-system-timezone.h			\
	e-cal-util.c				\
	e-cal-view.c				\
	e-cal-view-private.h

libecal_1_2_la_LIBADD = 					\
	$(top_builddir)/libedataserver/libedataserver-1.2.la 	\
	$(top_builddir)/calendar/libegdbus/libegdbus-cal.la	\
	$(LIBICAL_LIBS)						\
	$(EVOLUTION_CALENDAR_LIBS)

libecal_1_2_la_LDFLAGS = 					\
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE) $(NO_UNDEFINED)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS =	\
	e-cal.h			\
	e-cal-client.h		\
	e-cal-client-view.h	\
	e-cal-component.h	\
	e-cal-recur.h		\
	e-cal-time-util.h	\
        e-cal-check-timezones.h \
	e-cal-system-timezone.h	\
	e-cal-types.h		\
	e-cal-util.h		\
	e-cal-view.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libecal-$(API_VERSION).pc

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_builddir)/libedataserver --identifier-prefix E --pkg-export libecal-1.2 \
	--c-include="libecal/e-cal.h"
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_builddir)/libedataserver

if HAVE_INTROSPECTION

introspection_sources = \
	$(filter-out %-private.h, $(libecalinclude_HEADERS) $(libecal_1_2_la_SOURCES))

ECalendar-1.2.gir: libecal-1.2.la
ECalendar_1_2_gir_INCLUDES = GObject-2.0 EDataServer-1.2 GLib-2.0
ECalendar_1_2_gir_CFLAGS = $(libecal_INCLUDES) $(LIBICAL_CFLAGS) $(EVOLUTION_CALENDAR_CFLAGS)
ECalendar_1_2_gir_LIBS = libecal-1.2.la $(top_builddir)/calendar/libegdbus/libegdbus-cal.la $(top_builddir)/libedataserver/libedataserver-1.2.la $(LIBICAL_LIBS)
ECalendar_1_2_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += ECalendar-1.2.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

EXTRA_DIST = 						\
	e-cal-marshal.list				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

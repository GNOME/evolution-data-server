ENUM_TYPES = e-cal-types.h

e-cal-enumtypes.h: e-cal-enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-cal-enumtypes.h.template $(ENUM_TYPES)) > $@

e-cal-enumtypes.c: e-cal-enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-cal-enumtypes.c.template $(ENUM_TYPES)) > $@

ENUM_GENERATED = e-cal-enumtypes.h e-cal-enumtypes.c

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
	-I$(top_builddir)/calendar		\
	-I$(top_builddir)/calendar/libecal

libecal_1_2_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(libecal_INCLUDES)			\
	-DLIBECAL_COMPILATION			\
	-DG_LOG_DOMAIN=\"libecal\"		\
	$(EVOLUTION_CALENDAR_CFLAGS)		\
	$(CAMEL_CFLAGS)				\
	$(CODE_COVERAGE_CFLAGS)			\
	$(NULL)

libecal_1_2_la_SOURCES =			\
	$(ENUM_GENERATED)			\
	$(MARSHAL_GENERATED)			\
	e-cal.c					\
	e-cal-client.c				\
	e-cal-client-view.c			\
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
	$(EVOLUTION_CALENDAR_LIBS)				\
	$(CAMEL_LIBS)

libecal_1_2_la_LDFLAGS = 					\
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS =	\
	libecal.h		\
	e-cal.h			\
	e-cal-client.h		\
	e-cal-client-view.h	\
	e-cal-component.h	\
	e-cal-enumtypes.h	\
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

BUILT_SOURCES = $(ENUM_GENERATED) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	e-cal-enumtypes.h.template			\
	e-cal-enumtypes.c.template			\
	e-cal-marshal.list

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

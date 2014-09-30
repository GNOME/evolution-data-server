NULL =

ENUM_TYPES = e-cal-types.h

e-cal-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef E_CAL_ENUMTYPES_H\n#define E_CAL_ENUMTYPES_H\n" \
		--ftail "#endif /* E_CAL_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

e-cal-enumtypes.c: $(top_srcdir)/enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"e-cal-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = \
	e-cal-enumtypes.h \
	e-cal-enumtypes.c \
	$(NULL)

lib_LTLIBRARIES = libecal-1.2.la

libecal_INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	-I$(top_srcdir)/calendar/libegdbus \
	-I$(top_srcdir)/calendar/libedata-cal \
	-I$(top_srcdir)/private \
	-I$(top_builddir) \
	-I$(top_builddir)/calendar \
	-I$(top_builddir)/calendar/libecal \
	-I$(top_builddir)/private \
	$(NULL)

libecal_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(libecal_INCLUDES) \
	-DLIBECAL_COMPILATION \
	-DG_LOG_DOMAIN=\"libecal\" \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libecal_1_2_la_SOURCES = \
	$(ENUM_GENERATED) \
	e-cal.c \
	e-cal-client.c \
	e-cal-client-view.c \
	e-cal-component.c \
	e-cal-recur.c \
	e-cal-time-util.c \
	e-cal-check-timezones.c \
	e-cal-system-timezone.c \
	e-cal-system-timezone.h \
	e-cal-util.c \
	e-cal-view.c \
	e-cal-view-private.h \
	e-timezone-cache.c \
	$(NULL)

libecal_1_2_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/calendar/libegdbus/libegdbus-cal.la \
	$(top_builddir)/private/libedbus-private.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

libecal_1_2_la_LDFLAGS = \
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS = \
	libecal.h \
	e-cal.h \
	e-cal-client.h \
	e-cal-client-view.h \
	e-cal-component.h \
	e-cal-enumtypes.h \
	e-cal-recur.h \
	e-cal-time-util.h \
        e-cal-check-timezones.h \
	e-cal-system-timezone.h \
	e-cal-types.h \
	e-cal-util.h \
	e-cal-view.h \
	e-timezone-cache.h \
	$(NULL)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libecal-$(API_VERSION).pc

BUILT_SOURCES = $(ENUM_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

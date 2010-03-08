# The marshallers
MARSHAL_GENERATED = e-cal-marshal.c e-cal-marshal.h
@EVO_MARSHAL_RULE@

# The library
lib_LTLIBRARIES = libecal-1.2.la

libecal_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)							\
	-DG_LOG_DOMAIN=\"libecal\"				\
	-I$(top_srcdir) 						\
	-I$(top_srcdir)/calendar				\
	-I$(top_builddir)						\
	-I$(top_builddir)/calendar/libecal		\
	$(LIBICAL_CFLAGS)						\
	$(EVOLUTION_CALENDAR_CFLAGS)

libecal_1_2_la_SOURCES =	\
	$(MARSHAL_GENERATED)	\
	$(DBUS_GENERATED_H)	\
	e-cal.c			\
	e-cal-component.c	\
	e-cal-recur.c		\
	e-cal-time-util.c	\
        e-cal-check-timezones.c \
	e-cal-system-timezone.c	\
	e-cal-system-timezone.h	\
	e-cal-util.c		\
	e-cal-view.c		\
	e-cal-view-private.h

libecal_1_2_la_LIBADD = 					\
	$(top_builddir)/libedataserver/libedataserver-1.2.la 	\
	$(LIBICAL_LIBS)						\
	$(EVOLUTION_CALENDAR_LIBS)

libecal_1_2_la_LDFLAGS = 					\
	-version-info $(LIBECAL_CURRENT):$(LIBECAL_REVISION):$(LIBECAL_AGE) $(NO_UNDEFINED)

libecalincludedir = $(privincludedir)/libecal

libecalinclude_HEADERS =	\
	e-cal.h			\
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

DBUS_GENERATED_H = e-data-cal-factory-bindings.h e-data-cal-bindings.h e-data-cal-view-bindings.h

%-bindings.h: $(top_srcdir)/calendar/libedata-cal/%.xml
	@DBUS_BINDING_TOOL@ --mode=glib-client --output=$@ --prefix=$(subst -,_,$*) $^

BUILT_SOURCES = $(DBUS_GENERATED_H) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = 						\
	e-cal-marshal.list				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

MARSHAL_GENERATED = e-data-cal-marshal.c e-data-cal-marshal.h
@EVO_MARSHAL_RULE@

DBUS_GENERATED_H = e-data-cal-glue.h e-data-cal-factory-glue.h e-data-cal-view-glue.h
%-glue.h: %.xml
	@DBUS_BINDING_TOOL@ --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $^

include $(top_srcdir)/glib-gen.mak
glib_enum_headers=e-data-cal-types.h
glib_enum_define=E_DATA_CAL
glib_enum_prefix=e_data_cal

ENUM_GENERATED = e-data-cal-enumtypes.h e-data-cal-enumtypes.c

# The libraray
lib_LTLIBRARIES = libedata-cal-1.2.la

libedata_cal_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libedata-cal\"			\
	-DBASE_VERSION=\""$(BASE_VERSION)"\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	$(LIBICAL_CFLAGS)				\
	$(EVOLUTION_CALENDAR_CFLAGS)

libedata_cal_1_2_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	$(ENUM_GENERATED)		\
	$(DBUS_GENERATED_H)		\
	e-cal-backend.c			\
	e-cal-backend-cache.c		\
	e-cal-backend-factory.c		\
	e-cal-backend-sexp.c		\
	e-cal-backend-sync.c		\
	e-cal-backend-util.c		\
	e-cal-backend-store.c		\
	e-cal-backend-file-store.c	\
	e-data-cal.c			\
	e-data-cal-view.c

libedata_cal_1_2_la_LIBADD =					\
	$(top_builddir)/calendar/libecal/libecal-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la 	\
	$(top_builddir)/libebackend/libebackend-1.2.la 		\
	$(LIBICAL_LIBS)						\
	$(EVOLUTION_CALENDAR_LIBS)

libedata_cal_1_2_la_LDFLAGS = 								\
	-version-info $(LIBEDATACAL_CURRENT):$(LIBEDATACAL_REVISION):$(LIBEDATACAL_AGE) $(NO_UNDEFINED)

libedata_calincludedir = $(privincludedir)/libedata-cal

libedata_calinclude_HEADERS = 		\
	e-data-cal-types.h		\
	e-cal-backend.h			\
	e-cal-backend-cache.h		\
	e-cal-backend-factory.h		\
	e-cal-backend-sync.h		\
	e-cal-backend-util.h		\
	e-cal-backend-sexp.h		\
	e-data-cal.h			\
	e-data-cal-common.h		\
	e-data-cal-factory.h		\
	e-cal-backend-store.h		\
	e-cal-backend-file-store.h	\
	e-data-cal-view.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedata-cal-$(API_VERSION).pc

service_in_files = org.gnome.evolution.dataserver.Calendar.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@

BUILT_SOURCES = $(DBUS_GENERATED_H) $(MARSHAL_GENERATED) $(ENUM_GENERATED)
CLEANFILES = $(BUILT_SOURCES) $(service_DATA)
DISTCLEANFILES = $(pkgconfig_DATA) $(service_DATA)

EXTRA_DIST = 						\
	e-data-cal-marshal.list				\
	e-data-cal-factory.xml				\
	e-data-cal-view.xml				\
	e-data-cal.xml					\
	$(service_in_files)				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)


factorydir = $(libexecdir)
factory_PROGRAMS = e-calendar-factory

e_calendar_factory_SOURCES = \
	e-data-cal-factory.c \
	e-data-cal-factory.h \
	e-cal-backend-loader-factory.c \
	e-cal-backend-loader-factory.h
e_calendar_factory_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libedata-cal\"			\
	-DE_PRGNAME=\"e-calendar-factory\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	$(LIBICAL_CFLAGS)				\
	$(EVOLUTION_CALENDAR_CFLAGS)
e_calendar_factory_LDADD =				\
	$(builddir)/libedata-cal-1.2.la			\
	$(top_builddir)/libebackend/libebackend-1.2.la

-include $(top_srcdir)/git.mk

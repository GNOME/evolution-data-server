NULL =

dbus_service_in_files = org.gnome.evolution.dataserver.Calendar.service.in
dbus_service_DATA = $(dbus_service_in_files:.service.in=.service)

systemd_user_in_files = evolution-calendar-factory.service.in
if WITH_SYSTEMD_USER_UNITS
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)
endif

# this works for both D-Bus and systemd services
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = \
	$(dbus_service_DATA) \
	$(systemd_user_DATA) \
	$(NULL)

EXTRA_DIST = \
	$(dbus_service_in_files) \
	$(systemd_user_in_files) \
	$(NULL)

libexec_PROGRAMS = evolution-calendar-factory

evolution_calendar_factory_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\"evolution-calendar-factory\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir) \
	-I$(top_builddir)/calendar \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(FACTORY_GTK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

evolution_calendar_factory_SOURCES = \
	evolution-calendar-factory.c \
	$(NULL)

evolution_calendar_factory_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(LIBSECRET_LIBS) \
	$(FACTORY_GTK_LIBS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_LIBS) \
	$(NULL)

evolution_calendar_factory_LDFLAGS = \
	$(AM_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk

NULL =

service_in_files = org.gnome.evolution.dataserver.UserPrompter.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = $(service_DATA)
EXTRA_DIST = $(service_in_files)

libexec_PROGRAMS = evolution-user-prompter

evolution_user_prompter_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/private \
	-I$(top_builddir) \
	-I$(top_builddir)/private \
	-DG_LOG_DOMAIN=\"evolution-user-prompter\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(GNOME_PLATFORM_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

evolution_user_prompter_SOURCES = \
	evolution-user-prompter.c \
	$(NULL)

evolution_user_prompter_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(GNOME_PLATFORM_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

-include $(top_srcdir)/git.mk

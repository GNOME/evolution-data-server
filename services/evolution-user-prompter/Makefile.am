NULL =

dbus_service_in_files = org.gnome.evolution.dataserver.UserPrompter.service.in
dbus_service_DATA = $(dbus_service_in_files:.service.in=.service)

systemd_user_in_files = evolution-user-prompter.service.in
if WITH_SYSTEMD_USER_UNITS
systemd_user_DATA = $(systemd_user_in_files:.service.in=.service)
endif

# this works for both
@EVO_SUBST_SERVICE_RULE@

CLEANFILES = \
	$(dbus_service_DATA) \
	$(systemd_user_DATA) \
	$(NULL)
EXTRA_DIST = \
	$(dbus_service_in_files) \
	$(systemd_user_in_files) \
	$(NULL)

PROMPT_USER_SOURCES = prompt-user-gtk.c
PROMPT_USER_CFLAGS = $(GNOME_PLATFORM_CFLAGS) $(GTK_CFLAGS)
PROMPT_USER_LIBS = $(GNOME_PLATFORM_LIBS) $(GTK_LIBS)

libexec_PROGRAMS = evolution-user-prompter

evolution_user_prompter_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/private \
	-I$(top_builddir) \
	-I$(top_builddir)/private \
	-DG_LOG_DOMAIN=\"evolution-user-prompter\" \
	-DLOCALEDIR=\"$(localedir)\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(PROMPT_USER_CFLAGS) \
	$(NULL)

evolution_user_prompter_SOURCES = \
	evolution-user-prompter.c \
	prompt-user.h \
	$(PROMPT_USER_SOURCES) \
	$(NULL)

evolution_user_prompter_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(PROMPT_USER_LIBS) \
	$(NULL)

-include $(top_srcdir)/git.mk

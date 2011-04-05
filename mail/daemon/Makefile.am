factorydir = $(libexecdir)

factory_PROGRAMS = e-mail-factory
e_mail_factory_SOURCES = main.c	\
			 e-mail-local.c	\
			 e-mail-local.h	\
			 e-mail-store.c	\
			 e-mail-store.h	\
			 e-dbus-manager.c	\
			 e-dbus-manager.h	\
			 e-mail-data-store.c	\
			 e-mail-data-store.h	\
			 e-mail-data-session.c	\
			 e-mail-data-session.h	\
			 e-mail-data-folder.c	\
			 e-mail-data-folder.h	\
			 mail-tools.c	\
			 mail-tools.h	\
			 mail-ops.c	\
			 mail-ops.h	\
			 mail-folder-cache.c	\
			 mail-folder-cache.h	\
			 mail-config.c	\
			 mail-config.h	\
			 mail-send-recv.c	\
			 mail-send-recv.h	\
			 mail-session.c	\
			 mail-session.h	\
			 utils.c	\
			 utils.h

noinst_PROGRAMS = test-email-client

test_email_client_SOURCES = test-email-client.c
test_email_client_CPPFLAGS = \
		$(E_MAIL_FACTORY_CFLAGS)	\
		$(GNOME_PLATFORM_CFLAGS)	\
		$(CAMEL_CFLAGS)			\
		-Wall -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""	\
		-I$(top_srcdir)/mail/					\
		-I$(top_srcdir)/mail/libemailutils				\
		-I$(top_srcdir)/mail/libegdbus				\
		-I$(top_srcdir)						\
		-I$(top_builddir)					

test_email_client_LDADD = \
		$(top_builddir)/mail/libemailutils/libemail-utils.la	\
		$(top_builddir)/mail/libegdbus/libegdbus-mail.la	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/camel/libcamel-provider-1.2.la		\
		$(top_builddir)/libedataserver/libedataserver-1.2.la	\
		$(top_builddir)/libedataserverui/libedataserverui-1.2.la	\
		$(E_MAIL_FACTORY_LIBS)				\
		$(GNOME_PLATFORM_LIBS)	\
		$(CAMEL_LIBS)			\
		-lffi


#desktopentry_DATA = e-mail-factory.desktop
#icon_DATA = 

#see gcc man page for description: http://www.google.com/search?q=man+gcc
#AM_CFLAGS = @E_MAIL_FACTORY_CFLAGS@ -Wall -Werror -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""
#AM_LDFLAGS = @E_MAIL_FACTORY_LIBS@

e_mail_factory_CPPFLAGS = \
		$(E_MAIL_FACTORY_CFLAGS)	\
		$(CAMEL_CFLAGS)			\
		$(GNOME_PLATFORM_CFLAGS)	\
		-Wall -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""	\
		-I$(top_srcdir)/mail/					\
		-I$(top_srcdir)/mail/libemailutils				\
		-I$(top_srcdir)/mail/libegdbus				\
		-I$(top_srcdir)						\
		-I$(top_builddir)					

e_mail_factory_LDADD = \
		$(CAMEL_LIBS)		\
		$(GNOME_PLATFORM_LIBS)	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/camel/libcamel-provider-1.2.la		\
		$(top_builddir)/libedataserver/libedataserver-1.2.la	\
		$(top_builddir)/libedataserverui/libedataserverui-1.2.la	\
		$(top_builddir)/mail/libemailutils/libemail-utils.la	\
		$(top_builddir)/mail/libegdbus/libegdbus-mail.la	\
		$(E_MAIL_FACTORY_LIBS)				\
		-lffi

service_in_files = org.gnome.evolution.dataserver.Mail.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@


EXTRA_DIST = e-mail-data-folder.xml  	\
	     e-mail-data-session.xml  	\
	     e-mail-data-store.xml	\
	     $(service_in_files)


CLEANFILES = *~ $(service_DATA)


factorydir = $(libexecdir)

factory_PROGRAMS = evolution-mail-factory
evolution_mail_factory_SOURCES = 	\
		main.c			\
		mail-send-recv.c	\
		utils.c

noinst_PROGRAMS = test-email-client

test_email_client_SOURCES = test-email-client.c
test_email_client_CPPFLAGS = \
		$(GNOME_PLATFORM_CFLAGS)	\
		$(CAMEL_CFLAGS)			\
		-Wall -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""	\
		-I$(top_srcdir)/mail/					\
		-I$(top_srcdir)/mail/libemailutils				\
		-I$(top_srcdir)/mail/libegdbus				\
		-I$(top_srcdir)						\
		-I$(top_builddir)					

test_email_client_LDADD = \
		$(top_builddir)/mail/libemail-engine/libemail-engine.la	\
		$(top_builddir)/mail/libemail-utils/libemail-utils.la	\
		$(top_builddir)/mail/libegdbus/libegdbus-mail.la	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/libedataserver/libedataserver-1.2.la	\
		$(top_builddir)/libedataserverui/libedataserverui-3.0.la	\
		$(GNOME_PLATFORM_LIBS)	\
		$(CAMEL_LIBS)			


evolution_mail_factory_CPPFLAGS = \
		$(CAMEL_CFLAGS)			\
		$(GNOME_PLATFORM_CFLAGS)	\
		-Wall -g3 -O0 -ggdb -DPKGDATADIR="\"$(pkgdatadir)\""	\
		-I$(top_srcdir)/mail/					\
		-I$(top_srcdir)/mail/libemailutils				\
		-I$(top_srcdir)/mail/libegdbus				\
		-I$(top_srcdir)						\
		-I$(top_builddir)					

evolution_mail_factory_LDADD = \
		$(CAMEL_LIBS)		\
		$(GNOME_PLATFORM_LIBS)	\
		$(top_builddir)/camel/libcamel-1.2.la			\
		$(top_builddir)/libedataserver/libedataserver-1.2.la	\
		$(top_builddir)/libedataserverui/libedataserverui-3.0.la	\
		$(top_builddir)/mail/libemail-utils/libemail-utils.la	\
		$(top_builddir)/mail/libemail-engine/libemail-engine.la	\
		$(top_builddir)/mail/libegdbus/libegdbus-mail.la	

service_in_files = org.gnome.evolution.dataserver.Mail.service.in
servicedir = $(datadir)/dbus-1/services
service_DATA = $(service_in_files:.service.in=.service)
@EVO_SUBST_SERVICE_RULE@


EXTRA_DIST = $(service_in_files)


CLEANFILES = *~ $(service_DATA)


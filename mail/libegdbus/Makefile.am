# The library
noinst_LTLIBRARIES = libegdbus-mail.la

libegdbus_mail_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-mail\"	

libegdbus_mail_la_SOURCES =

libegdbus_mail_la_LIBADD =			\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la
	

libegdbus_mail_la_LDFLAGS = $(NO_UNDEFINED)


-include $(top_srcdir)/git.mk

GDBUS_XML_FILES =					\
	../daemon/e-mail-data-session.xml	\
	../deamon/e-mail-data-store.xml		\
	../daemon/e-mail-data-folder.xml

gdbus-files: $(GDBUS_XML_FILES)
	~/opt/master/bin/gdbus-codegen --namespace=EGdbus --strip-prefix=org.gnome.evolution.dataserver.mail --generate-c-code=$^ $^.xml

#~/opt/master/bin/gdbus-codegen --namespace=EGdbusSession --strip-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailsession ../src/e-mail-data-session.xml
#~/opt/master/bin/gdbus-codegen --namespace=EGdbusFolder --strip-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailfolder ../src/e-mail-data-folder.xml 
#~/opt/master/bin/gdbus-codegen --namespace=EGdbusStore --strip-prefix=org.gnome.evolution.dataserver.mail. --generate-c-code=e-gdbus-emailstore ../src/e-mail-data-store.xml

# The library
noinst_LTLIBRARIES = libegdbus-mail.la

libegdbus_mail_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-mail\"	\
	$(E_MAIL_GDBUS_UTILS_CFLAGS)

libegdbus_mail_la_SOURCES =			\
	e-gdbus-emailfolder.h			\
	e-gdbus-emailfolder.c			\
	e-gdbus-emailstore.h		\
	e-gdbus-emailstore.c		\
	e-gdbus-emailsession.h		\
	e-gdbus-emailsession.c		

libegdbus_mail_la_LIBADD =			\
	$(E_MAIL_GDBUS_UTILS_LIBS)		\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la
	

libegdbus_mail_la_LDFLAGS = $(NO_UNDEFINED)


-include $(top_srcdir)/git.mk

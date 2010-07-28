GDBUS_XML_FILES =				\
	../libedata-cal/e-data-cal-factory.xml	\
	../libedata-cal/e-data-cal-view.xml	\
	../libedata-cal/e-data-cal.xml

gdbus-files: $(GDBUS_XML_FILES)
	gdbus-codegen --namespace=EGdbus --strip-prefix=org.gnome.evolution.dataserver.calendar --output-prefix=e-gdbus $(GDBUS_XML_FILES)
	rm e-gdbus-bindings.h
	rm e-gdbus-bindings.stamp

# The library
noinst_LTLIBRARIES = libegdbus-cal.la

libegdbus_cal_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-cal\"	\
	-I$(top_srcdir)/calendar		\
	$(GNOME_PLATFORM_CFLAGS)

libegdbus_cal_la_SOURCES =			\
	e-gdbus-egdbuscal.h			\
	e-gdbus-egdbuscal.c			\
	e-gdbus-egdbuscalfactory.h		\
	e-gdbus-egdbuscalfactory.c		\
	e-gdbus-egdbuscalview.h			\
	e-gdbus-egdbuscalview.c			\
	e-gdbus-marshallers.h			\
	e-gdbus-marshallers.c			\
	e-gdbus-typemappers.h

libegdbus_cal_la_LIBADD =			\
	$(GNOME_PLATFORM_LIBS)

libegdbus_cal_la_LDFLAGS = $(NO_UNDEFINED)

EXTRA_DIST = e-gdbus-marshallers.list

-include $(top_srcdir)/git.mk

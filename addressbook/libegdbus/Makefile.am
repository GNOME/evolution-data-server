GDBUS_XML_FILES =					\
	../libedata-book/e-data-book-factory.xml	\
	../libedata-book/e-data-book-view.xml		\
	../libedata-book/e-data-book.xml

gdbus-files: $(GDBUS_XML_FILES)
	gdbus-codegen --namespace=EGdbus --strip-prefix=org.gnome.evolution.dataserver.addressbook --output-prefix=e-gdbus $(GDBUS_XML_FILES)
	rm e-gdbus-bindings.h
	rm e-gdbus-bindings.stamp

# The library
noinst_LTLIBRARIES = libegdbus-book.la

libegdbus_book_la_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	-DG_LOG_DOMAIN=\"libegdbus-book\"	\
	-I$(top_srcdir)/addressbook		\
	$(E_DATA_SERVER_CFLAGS)

libegdbus_book_la_SOURCES =			\
	e-gdbus-egdbusbook.h			\
	e-gdbus-egdbusbook.c			\
	e-gdbus-egdbusbookfactory.h		\
	e-gdbus-egdbusbookfactory.c		\
	e-gdbus-egdbusbookview.h		\
	e-gdbus-egdbusbookview.c		\
	e-gdbus-marshallers.h			\
	e-gdbus-marshallers.c			\
	e-gdbus-typemappers.h

libegdbus_book_la_LIBADD =			\
	$(E_DATA_SERVER_LIBS)

libegdbus_book_la_LDFLAGS = $(NO_UNDEFINED)

EXTRA_DIST = e-gdbus-marshallers.list

-include $(top_srcdir)/git.mk

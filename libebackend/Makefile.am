lib_LTLIBRARIES = libebackend-1.2.la

libebackend_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"e-data-server\"			\
	$(DB_CFLAGS)						\
	$(SQLITE3_CFLAGS)					\
	$(E_BACKEND_CFLAGS)					\
	$(GIO_UNIX_CFLAGS)

libebackend_1_2_la_SOURCES =		\
	e-backend.c			\
	e-backend-factory.c		\
	e-data-factory.c		\
	e-dbus-server.c			\
	e-extensible.c			\
	e-extension.c			\
	e-offline-listener.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-module.c			\
	e-sqlite3-vfs.c			\
	e-file-cache.c

libebackend_1_2_la_LIBADD = 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS)				\
	$(SQLITE3_LIBS)					\
	$(GIO_UNIX_LIBS)				\
	$(DB_LIBS)

libebackend_1_2_la_LDFLAGS = \
	-version-info $(LIBEBACKEND_CURRENT):$(LIBEBACKEND_REVISION):$(LIBEBACKEND_AGE) $(NO_UNDEFINED)

libebackendincludedir = $(privincludedir)/libebackend

libebackendinclude_HEADERS =		\
	e-backend.h			\
	e-backend-factory.h		\
	e-data-factory.h		\
	e-dbus-server.h			\
	e-extensible.h			\
	e-extension.h			\
	e-offline-listener.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-module.h			\
	e-sqlite3-vfs.h			\
	e-file-cache.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebackend-$(API_VERSION).pc

gsettings_SCHEMAS = \
	org.gnome.evolution.eds-shell.gschema.xml

@GSETTINGS_RULES@

EXTRA_DIST = 						\
	$(gsettings_SCHEMAS)				\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

CLEANFILES = $(gsettings_SCHEMAS:.xml=.valid)

DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk

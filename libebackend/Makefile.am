ENUM_TYPES = e-backend-enums.h

e-backend-enumtypes.h: e-backend-enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-backend-enumtypes.h.template $(ENUM_TYPES)) > $@

e-backend-enumtypes.c: e-backend-enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template e-backend-enumtypes.c.template $(ENUM_TYPES)) > $@

ENUM_GENERATED = e-backend-enumtypes.h e-backend-enumtypes.c

BUILT_SOURCES = $(ENUM_GENERATED)

lib_LTLIBRARIES = libebackend-1.2.la

libebackend_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/private					\
	-I$(top_builddir)					\
	-I$(top_builddir)/private				\
	-DLIBEBACKEND_COMPILATION				\
	-DG_LOG_DOMAIN=\"libebackend\"				\
	-DMODULE_DIRECTORY=\"$(moduledir)\"			\
	-DE_DATA_SERVER_PRIVDATADIR=\"$(privdatadir)\"		\
	-DSYSTEM_WIDE_RO_SOURCES_DIRECTORY=\"$(ro_sourcesdir)\"	\
	-DSYSTEM_WIDE_RW_SOURCES_DIRECTORY=\"$(rw_sourcesdir)\"	\
	$(DB_CFLAGS)						\
	$(CAMEL_CFLAGS)						\
	$(SQLITE3_CFLAGS)					\
	$(E_BACKEND_CFLAGS)					\
	$(GCR_BASE_CFLAGS)					\
	$(GIO_UNIX_CFLAGS)					\
	$(CODE_COVERAGE_CFLAGS)					\
	$(NULL)

libebackend_1_2_la_SOURCES =		\
	$(BUILT_SOURCES)		\
	e-authentication-mediator.c	\
	e-authentication-session.c	\
	e-backend.c			\
	e-backend-factory.c		\
	e-collection-backend.c		\
	e-collection-backend-factory.c	\
	e-data-factory.c		\
	e-dbus-server.c			\
	e-extensible.c			\
	e-extension.c			\
	e-offline-listener.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-module.c			\
	e-server-side-source.c		\
	e-source-registry-server.c	\
	e-sqlite3-vfs.c			\
	e-file-cache.c

libebackend_1_2_la_LIBADD = 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/private/libedbus-private.la	\
	$(E_BACKEND_LIBS)				\
	$(CAMEL_LIBS)					\
	$(SQLITE3_LIBS)					\
	$(GCR_BASE_LIBS)				\
	$(GIO_UNIX_LIBS)				\
	$(DB_LIBS)

libebackend_1_2_la_LDFLAGS = \
	-version-info $(LIBEBACKEND_CURRENT):$(LIBEBACKEND_REVISION):$(LIBEBACKEND_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libebackendincludedir = $(privincludedir)/libebackend

libebackendinclude_HEADERS =		\
	libebackend.h			\
	e-authentication-mediator.h	\
	e-authentication-session.h	\
	e-backend.h			\
	e-backend-enums.h		\
	e-backend-enumtypes.h		\
	e-backend-factory.h		\
	e-collection-backend.h		\
	e-collection-backend-factory.h	\
	e-data-factory.h		\
	e-dbus-server.h			\
	e-extensible.h			\
	e-extension.h			\
	e-offline-listener.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-module.h			\
	e-server-side-source.h		\
	e-source-registry-server.h	\
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
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	e-backend-enumtypes.h.template			\
	e-backend-enumtypes.c.template

CLEANFILES = $(gsettings_SCHEMAS:.xml=.valid)

DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk

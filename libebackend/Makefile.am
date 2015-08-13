NULL =

ENUM_TYPES = e-backend-enums.h

if HAVE_LIBDB
LIBDB_C_FILES = \
	e-dbhash.c \
	e-db3-utils.c
LIBDB_H_FILES = \
	e-db3-utils.h \
	e-dbhash.h
endif

e-backend-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef E_BACKEND_ENUMTYPES_H\n#define E_BACKEND_ENUMTYPES_H\n" \
		--ftail "#endif /* E_BACKEND_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

e-backend-enumtypes.c: $(top_srcdir)/enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"e-backend-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = \
	e-backend-enumtypes.h \
	e-backend-enumtypes.c \
	$(NULL)

BUILT_SOURCES = $(ENUM_GENERATED)

lib_LTLIBRARIES = libebackend-1.2.la

libebackend_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/private \
	-I$(top_builddir) \
	-I$(top_builddir)/private \
	-DLIBEBACKEND_COMPILATION \
	-DG_LOG_DOMAIN=\"libebackend\" \
	-DMODULE_DIRECTORY=\"$(moduledir)\" \
	-DE_DATA_SERVER_PRIVDATADIR=\"$(privdatadir)\" \
	-DSYSTEM_WIDE_RO_SOURCES_DIRECTORY=\"$(ro_sourcesdir)\" \
	-DSYSTEM_WIDE_RW_SOURCES_DIRECTORY=\"$(rw_sourcesdir)\" \
	$(DB_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	$(E_BACKEND_CFLAGS) \
	$(GCR_BASE_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libebackend_1_2_la_SOURCES = \
	$(BUILT_SOURCES) \
	e-backend.c \
	e-backend-factory.c \
	e-cache-reaper.c \
	e-cache-reaper-utils.c \
	e-cache-reaper-utils.h \
	e-collection-backend.c \
	e-collection-backend-factory.c \
	e-data-factory.c \
	e-dbus-server.c \
	e-oauth2-support.c \
	e-offline-listener.c \
	e-server-side-source.c \
	e-server-side-source-credentials-provider.c \
	e-source-registry-server.c \
	e-sqlite3-vfs.c \
	e-subprocess-factory.c \
	e-user-prompter.c \
	e-user-prompter-server.c \
	e-user-prompter-server-extension.c \
	e-file-cache.c \
	$(LIBDB_C_FILES) \
	$(NULL)

libebackend_1_2_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/private/libedbus-private.la \
	$(E_BACKEND_LIBS) \
	$(CAMEL_LIBS) \
	$(SQLITE3_LIBS) \
	$(GCR_BASE_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(SOUP_LIBS) \
	$(DB_LIBS) \
	$(NULL)

libebackend_1_2_la_LDFLAGS = \
	-version-info $(LIBEBACKEND_CURRENT):$(LIBEBACKEND_REVISION):$(LIBEBACKEND_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libebackendincludedir = $(privincludedir)/libebackend

libebackendinclude_HEADERS = \
	libebackend.h \
	e-backend.h \
	e-backend-enums.h \
	e-backend-enumtypes.h \
	e-backend-factory.h \
	e-cache-reaper.h \
	e-collection-backend.h \
	e-collection-backend-factory.h \
	e-data-factory.h \
	e-dbus-server.h \
	e-oauth2-support.h \
	e-offline-listener.h \
	e-server-side-source.h \
	e-server-side-source-credentials-provider.h \
	e-source-registry-server.h \
	e-sqlite3-vfs.h \
	e-subprocess-factory.h \
	e-user-prompter.h \
	e-user-prompter-server.h \
	e-user-prompter-server-extension.h \
	e-file-cache.h \
	$(LIBDB_H_FILES) \
	$(NULL)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebackend-$(API_VERSION).pc

gsettings_SCHEMAS = \
	org.gnome.evolution.eds-shell.gschema.xml \
	$(NULL)

@GSETTINGS_RULES@

EXTRA_DIST = \
	$(gsettings_SCHEMAS) \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	$(NULL)

CLEANFILES = $(gsettings_SCHEMAS:.xml=.valid)

DISTCLEANFILES = $(pkgconfig_DATA)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

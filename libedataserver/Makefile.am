# The marshallers
MARSHAL_GENERATED = e-gdbus-marshallers.c e-gdbus-marshallers.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserver-1.2.la

libedataserver_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"				\
	-DE_DATA_SERVER_LOCALEDIR=\""$(localedir)"\"			\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"		\
	-DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\"			\
	-DE_DATA_SERVER_UI_UIDIR=\""$(uidir)"\"				\
	-DDEFAULT_EDS_DBUS_TIMEOUT=$(DEFAULT_EDS_DBUS_TIMEOUT)		\
	$(E_DATA_SERVER_CFLAGS)						\
	$(SOUP_CFLAGS)

libedataserver_1_2_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	e-account-list.c		\
	e-account.c			\
	e-categories.c			\
	e-client.c			\
	e-client-private.h		\
	e-credentials.c			\
	e-flag.c			\
	e-gdbus-templates.h		\
	e-gdbus-templates.c		\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-operation-pool.c		\
	e-proxy.c			\
	e-sexp.c			\
	e-source-group.c		\
	e-source-list.c			\
	e-source.c			\
	e-debug-log.c			\
	e-time-utils.c			\
	e-uid.c				\
	e-url.c				\
	e-data-server-util.c		\
	e-xml-utils.c			\
	e-xml-hash-utils.c		\
	libedataserver-private.h	\
	eds-version.c

libedataserver_1_2_la_LIBADD = 				\
	$(E_DATA_SERVER_LIBS)				\
	$(ICONV_LIBS)					\
	$(SOCKET_LIBS)					\
	$(SOUP_LIBS)

libedataserver_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE) $(NO_UNDEFINED)

libedataserverincludedir = $(privincludedir)/libedataserver

libedataserverinclude_HEADERS =		\
	e-account-list.h		\
	e-account.h			\
	e-categories.h			\
	e-client.h			\
	e-credentials.h			\
	e-flag.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-operation-pool.h		\
	e-proxy.h			\
	e-sexp.h			\
	e-source-group.h		\
	e-source-list.h			\
	e-source.h			\
	e-debug-log.h			\
	e-time-utils.h			\
	e-uid.h				\
	e-url.h				\
	e-data-server-util.h		\
	e-xml-utils.h			\
	e-xml-hash-utils.h		\
	eds-version.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-$(API_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	eds-version.h.in

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = eds-version.h $(pkgconfig_DATA)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --identifier-prefix E --pkg-export \
	libedataserver-1.2  --c-include="libedataserver/e-account.h" \
	--c-include="libedataserver/e-source.h"
INTROSPECTION_COMPILER_ARGS =

if HAVE_INTROSPECTION
introspection_sources = \
	$(filter-out %-private.h e-sexp.h, $(libedataserverinclude_HEADERS) \
	$(libedataserver_1_2_la_SOURCES))

EDataServer-1.2.gir: libedataserver-1.2.la
EDataServer_1_2_gir_INCLUDES = GObject-2.0 libxml2-2.0
EDataServer_1_2_gir_CFLAGS = -I$(top_srcdir) $(E_DATA_SERVER_CFLAGS) \
	$(SOUP_CFLAGS)
EDataServer_1_2_gir_LIBS = libedataserver-1.2.la
EDataServer_1_2_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += EDataServer-1.2.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

-include $(top_srcdir)/git.mk

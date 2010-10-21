lib_LTLIBRARIES = libedataserver-1.2.la
noinst_PROGRAMS = test-source-list

libedataserver_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"				\
	-DE_DATA_SERVER_LOCALEDIR=\""$(localedir)"\"			\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"		\
	-DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\"			\
	-DE_DATA_SERVER_UI_UIDIR=\""$(uidir)"\"			\
	$(E_DATA_SERVER_CFLAGS)						\
	$(SOUP_CFLAGS)

libedataserver_1_2_la_SOURCES =		\
	e-account-list.c		\
	e-account.c			\
	e-categories.c			\
	e-flag.c			\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-proxy.c			\
	e-soap-message.c		\
	e-soap-response.c		\
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
	e-flag.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-proxy.h			\
	e-soap-message.h		\
	e-soap-response.h		\
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

test_source_list_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-DG_LOG_DOMAIN=\"e-data-server\"	\
	$(E_DATA_SERVER_CFLAGS)			\
	$(SOUP_CFLAGS)

test_source_list_SOURCES = test-source-list.c

test_source_list_LDADD = libedataserver-1.2.la $(E_DATA_SERVER_LIBS)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-$(API_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)	\
	eds-version.h.in

CLEANFILES =
DISTCLEANFILES = eds-version.h $(pkgconfig_DATA)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --identifier-prefix E
INTROSPECTION_COMPILER_ARGS =

if HAVE_INTROSPECTION
introspection_sources = \
	$(filter-out %-private.h e-sexp.h, $(libedataserverinclude_HEADERS) $(libedataserver_1_2_la_SOURCES))

EDataServer-1.2.gir: libedataserver-1.2.la
EDataServer_1_2_gir_INCLUDES = GObject-2.0 libxml2-2.0
EDataServer_1_2_gir_CFLAGS = -I$(top_srcdir) $(E_DATA_SERVER_CFLAGS) $(SOUP_CFLAGS)
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

ENUM_TYPES = e-source-enums.h

e-source-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef E_SOURCE_ENUMTYPES_H\n#define E_SOURCE_ENUMTYPES_H\n" \
		--ftail "#endif /* E_SOURCE_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

e-source-enumtypes.c: $(top_srcdir)/enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"e-source-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = e-source-enumtypes.h e-source-enumtypes.c

BUILT_SOURCES = $(ENUM_GENERATED)

lib_LTLIBRARIES = libedataserver-1.2.la
noinst_LTLIBRARIES = libedataserver-private.la

libedataserver_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/private \
	-I$(top_builddir) \
	-I$(top_builddir)/private \
	-DLIBEDATASERVER_COMPILATION \
	-DG_LOG_DOMAIN=\"e-data-server\" \
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\" \
	-DE_DATA_SERVER_LOCALEDIR=\""$(localedir)"\" \
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\" \
	-DE_DATA_SERVER_IMAGESDIR=\"$(imagesdir)\" \
	-DE_DATA_SERVER_PRIVDATADIR=\"$(privdatadir)\" \
	-DE_DATA_SERVER_UI_UIDIR=\""$(uidir)"\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(GCR_BASE_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(ICU_CFLAGS) \
	$(NULL)

libedataserver_1_2_la_SOURCES = \
	$(BUILT_SOURCES) \
	e-alphabet-index-private.h \
	e-cancellable-locks.c \
	e-categories.c \
	e-client.c \
	e-client-private.h \
	e-collator.c \
	e-credentials.c \
	e-flag.c \
	e-gdbus-templates.c \
	e-iterator.c \
	e-list.c \
	e-list-iterator.c \
	e-memory.c \
	e-operation-pool.c \
	e-proxy.c \
	e-sexp.c \
	e-source.c \
	e-source-extension.c \
	e-source-address-book.c \
	e-source-alarms.c \
	e-source-authentication.c \
	e-source-authenticator.c \
	e-source-autocomplete.c \
	e-source-backend.c \
	e-source-calendar.c \
	e-source-camel.c \
	e-source-collection.c \
	e-source-goa.c \
	e-source-mail-account.c \
	e-source-mail-composition.c \
	e-source-mail-identity.c \
	e-source-mail-signature.c \
	e-source-mail-submission.c \
	e-source-mail-transport.c \
	e-source-mdn.c \
	e-source-offline.c \
	e-source-openpgp.c \
	e-source-proxy.c \
	e-source-refresh.c \
	e-source-registry.c \
	e-source-resource.c \
	e-source-revision-guards.c \
	e-source-security.c \
	e-source-selectable.c \
	e-source-smime.c \
	e-source-uoa.c \
	e-source-webdav.c \
	e-debug-log.c \
	e-time-utils.c \
	e-transliterator-private.h \
	e-uid.c \
	e-url.c \
	e-data-server-util.c \
	e-xml-utils.c \
	e-xml-hash-utils.c \
	libedataserver-private.h \
	eds-version.c \
	$(NULL)

libedataserver_1_2_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/private/libedbus-private.la \
	$(E_DATA_SERVER_LIBS) \
	$(GCR_BASE_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(ICONV_LIBS) \
	$(SOCKET_LIBS) \
	$(SOUP_LIBS) \
	$(ICU_LIBS) \
	$(NULL)

libedataserver_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libedataserverincludedir = $(privincludedir)/libedataserver

libedataserverinclude_HEADERS = \
	libedataserver.h \
	e-cancellable-locks.h \
	e-categories.h \
	e-client.h \
	e-collator.h \
	e-credentials.h \
	e-flag.h \
	e-gdbus-templates.h \
	e-iterator.h \
	e-list.h \
	e-list-iterator.h \
	e-memory.h \
	e-operation-pool.h \
	e-proxy.h \
	e-sexp.h \
	e-source.h \
	e-source-address-book.h \
	e-source-alarms.h \
	e-source-authentication.h \
	e-source-authenticator.h \
	e-source-autocomplete.h \
	e-source-backend.h \
	e-source-calendar.h \
	e-source-camel.h \
	e-source-collection.h \
	e-source-enums.h \
	e-source-enumtypes.h \
	e-source-extension.h \
	e-source-goa.h \
	e-source-mail-account.h \
	e-source-mail-composition.h \
	e-source-mail-identity.h \
	e-source-mail-signature.h \
	e-source-mail-submission.h \
	e-source-mail-transport.h \
	e-source-mdn.h \
	e-source-offline.h \
	e-source-openpgp.h \
	e-source-proxy.h \
	e-source-refresh.h \
	e-source-registry.h \
	e-source-resource.h \
	e-source-revision-guards.h \
	e-source-security.h \
	e-source-selectable.h \
	e-source-smime.h \
	e-source-uoa.h \
	e-source-webdav.h \
	e-debug-log.h \
	e-time-utils.h \
	e-uid.h \
	e-url.h \
	e-data-server-util.h \
	e-xml-utils.h \
	e-xml-hash-utils.h \
	eds-version.h \
	$(NULL)

# We put the C++ code into a separate static library, so that we can use
# the C linker for libebook-contacts.
libedataserver_private_la_SOURCES = \
	e-alphabet-index-private.cpp \
	e-transliterator-private.cpp \
	$(NULL)
libedataserver_private_la_CPPFLAGS = $(libedataserver_1_2_la_CPPFLAGS)
if OS_WIN32
libedataserver_1_2_la_LIBADD += libedataserver-private.la -lstdc++
else
libedataserver_1_2_la_LIBADD += @predeps_CXX@ libedataserver-private.la @postdeps_CXX@
endif

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-$(API_VERSION).pc

EXTRA_DIST = \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	eds-version.h.in \
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES = \
	eds-version.h \
	$(pkgconfig_DATA) \
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = \
	--identifier-prefix E \
	--pkg-export libedataserver-1.2 \
	--c-include="libedataserver/libedataserver.h" \
	$(NULL)
INTROSPECTION_COMPILER_ARGS =

if HAVE_INTROSPECTION
introspection_sources = \
	$(filter-out %-private.h e-sexp.h, $(libedataserverinclude_HEADERS) \
	$(libedataserver_1_2_la_SOURCES)) \
	$(NULL)

EDataServer-1.2.gir: libedataserver-1.2.la
EDataServer_1_2_gir_INCLUDES = GObject-2.0 libxml2-2.0 Gio-2.0
EDataServer_1_2_gir_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DLIBEDATASERVER_COMPILATION \
	$(E_DATA_SERVER_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)
EDataServer_1_2_gir_LIBS = \
	libedataserver-1.2.la \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(NULL)
EDataServer_1_2_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += EDataServer-1.2.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

-include $(top_srcdir)/git.mk

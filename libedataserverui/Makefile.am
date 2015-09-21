BUILT_SOURCES =

lib_LTLIBRARIES = libedataserverui-1.2.la

libedataserverui_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DLIBEDATASERVERUI_COMPILATION \
	-DG_LOG_DOMAIN=\"e-data-server-ui\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(GNOME_PLATFORM_CFLAGS) \
	$(GCR_BASE_CFLAGS) \
	$(GCR_CFLAGS) \
	$(GTK_CFLAGS) \
	$(GOOGLE_AUTH_CFLAGS) \
	$(NULL)

libedataserverui_1_2_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(CAMEL_LIBS) \
	$(CODE_COVERAGE_LIBS) \
	$(GNOME_PLATFORM_LIBS) \
	$(GCR_BASE_LIBS) \
	$(GCR_LIBS) \
	$(GTK_LIBS) \
	$(GOOGLE_AUTH_LIBS) \
	$(NULL)

libedataserverui_1_2_la_LDFLAGS = \
	-version-info $(LIBEDATASERVERUI_CURRENT):$(LIBEDATASERVERUI_REVISION):$(LIBEDATASERVERUI_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libedataserveruiincludedir = $(privincludedir)/libedataserverui

libedataserveruiinclude_HEADERS = \
	libedataserverui.h \
	e-cell-renderer-color.h \
	e-credentials-prompter.h \
	e-credentials-prompter-impl.h \
	e-credentials-prompter-impl-google.h \
	e-credentials-prompter-impl-password.h \
	e-trust-prompt.h \
	e-webdav-discover-widget.h \
	$(NULL)

libedataserverui_1_2_la_SOURCES = \
	$(BUILT_SOURCES) \
	e-cell-renderer-color.c \
	e-credentials-prompter.c \
	e-credentials-prompter-impl.c \
	e-credentials-prompter-impl-google.c \
	e-credentials-prompter-impl-password.c \
	e-trust-prompt.c \
	e-webdav-discover-widget.c \
	$(NULL)

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserverui-$(API_VERSION).pc

EXTRA_DIST = \
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES = \
	$(pkgconfig_DATA) \
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk

INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"				\
	$(E_DATA_SERVER_UI_CFLAGS)

# The marshallers
MARSHAL_GENERATED = e-data-server-ui-marshal.c e-data-server-ui-marshal.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserverui-1.2.la
noinst_PROGRAMS = test-source-selector test-source-option-menu

libedataserverui_1_2_la_SOURCES =	\
	$(MARSHAL_GENERATED)		\
	e-source-selector.c		\
	e-source-option-menu.c

libedataserverui_1_2_la_LIBADD = 			\
	$(E_DATA_SERVER_LIBS)

libedataserverui_1_2_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVERUI_CURRENT):$(LIBEDATASERVERUI_REVISION):$(LIBEDATASERVERUI_AGE)

libedataserveruiincludedir = $(privincludedir)/libedataserverui

libedataserveruiinclude_HEADERS =		\
	e-source-selector.h		\
	e-source-option-menu.h

# Test programs
test_source_selector_SOURCES = test-source-selector.c
test_source_selector_LDADD = 					\
	libedataserverui-1.2.la 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)

test_source_option_menu_SOURCES = test-source-option-menu.c
test_source_option_menu_LDADD = 				\
	libedataserverui-1.2.la 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)

%-$(BASE_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserverui-$(BASE_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(BASE_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)
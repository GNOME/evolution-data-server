INCLUDES =								\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/addressbook					\
	-I$(top_builddir)/addressbook					\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"				\
	-DE_DATA_SERVER_UI_GLADEDIR=\""$(gladedir)"\"			\
	$(E_DATA_SERVER_UI_CFLAGS)

# The marshallers
MARSHAL_GENERATED = e-data-server-ui-marshal.c e-data-server-ui-marshal.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserverui-1.2.la
noinst_PROGRAMS = 			\
	test-source-selector		\
	test-source-option-menu		\
	test-contact-store		\
	test-name-selector

libedataserverui_1_2_la_SOURCES =	\
	$(MARSHAL_GENERATED)		\
	e-categories-dialog.c		\
	e-destination-store.c		\
	e-contact-store.c		\
	e-name-selector.c		\
	e-name-selector-dialog.c	\
	e-name-selector-entry.c		\
	e-name-selector-model.c		\
	e-source-selector.c		\
	e-source-selector-dialog.c	\
	e-source-option-menu.c

libedataserverui_1_2_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(E_DATA_SERVER_LIBS)

libedataserverui_1_2_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVERUI_CURRENT):$(LIBEDATASERVERUI_REVISION):$(LIBEDATASERVERUI_AGE)

libedataserveruiincludedir = $(privincludedir)/libedataserverui

libedataserveruiinclude_HEADERS =	\
	e-categories-dialog.h		\
	e-destination-store.h		\
	e-contact-store.h		\
	e-name-selector.h		\
	e-name-selector-dialog.h	\
	e-name-selector-entry.h		\
	e-name-selector-model.h		\
	e-source-selector.h		\
	e-source-selector-dialog.h	\
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

test_contact_store_SOURCES = test-contact-store.c
test_contact_store_LDADD =					\
	libedataserverui-1.2.la 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)

test_name_selector_SOURCES = test-name-selector.c
test_name_selector_LDADD =					\
	libedataserverui-1.2.la 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)

%-$(BASE_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserverui-$(BASE_VERSION).pc

glade_DATA = e-categories-dialog.glade e-name-selector-dialog.glade

EXTRA_DIST = 						\
	$(glade_DATA)					\
	$(pkgconfig_DATA:-$(BASE_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)

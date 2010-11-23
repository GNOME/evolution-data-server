# The marshallers
MARSHAL_GENERATED = e-data-server-ui-marshal.c e-data-server-ui-marshal.h
@EVO_MARSHAL_RULE@

noinst_PROGRAMS = 			\
	test-category-completion	\
	test-source-combo-box		\
	test-source-selector		\
	test-contact-store		\
	test-name-selector

CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/addressbook		\
	-I$(top_builddir)/addressbook		\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"	\
	-DE_DATA_SERVER_UI_UIDIR=\""$(uidir)"\"	\
	$(E_DATA_SERVER_UI_CFLAGS)		\
	$(CAMEL_CFLAGS)

csources =				\
	$(MARSHAL_GENERATED)		\
	e-categories-dialog.c		\
	e-category-completion.c		\
	e-destination-store.c		\
	e-book-auth-util.c		\
	e-contact-store.c		\
	e-name-selector.c		\
	e-name-selector-dialog.c	\
	e-name-selector-entry.c		\
	e-name-selector-model.c		\
	e-name-selector-list.c		\
	e-passwords.c			\
	e-source-selector.c		\
	e-source-selector-dialog.c	\
	e-source-combo-box.c		\
	e-tree-model-generator.c	\
	e-cell-renderer-color.c		\
	gtk-compat.h


if ENABLE_GTK3
lib_LTLIBRARIES = libedataserverui-3.0.la
libedataserverui_3_0_la_CPPFLAGS = $(CPPFLAGS)
libedataserverui_3_0_la_SOURCES = $(csources)	
libedataserverui_3_0_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(E_DATA_SERVER_UI_LIBS)				\
	$(CAMEL_LIBS)
libedataserverui_3_0_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVERUI_GTK3_CURRENT):$(LIBEDATASERVERUI_GTK3_REVISION):$(LIBEDATASERVERUI_GTK3_AGE) $(NO_UNDEFINED)
else
lib_LTLIBRARIES = libedataserverui-1.2.la
libedataserverui_1_2_la_CPPFLAGS = $(CPPFLAGS)
libedataserverui_1_2_la_SOURCES = $(csources)	
libedataserverui_1_2_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(E_DATA_SERVER_UI_LIBS)				\
	$(CAMEL_LIBS)
libedataserverui_1_2_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVERUI_CURRENT):$(LIBEDATASERVERUI_REVISION):$(LIBEDATASERVERUI_AGE) $(NO_UNDEFINED)
endif

libedataserveruiincludedir = $(privincludedir)/libedataserverui

libedataserveruiinclude_HEADERS =	\
	e-categories-dialog.h		\
	e-category-completion.h		\
	e-destination-store.h		\
	e-book-auth-util.h		\
	e-contact-store.h		\
	e-name-selector.h		\
	e-name-selector-dialog.h	\
	e-name-selector-entry.h		\
	e-name-selector-model.h		\
	e-name-selector-list.h		\
	e-passwords.h			\
	e-source-selector.h		\
	e-source-selector-dialog.h	\
	e-source-combo-box.h		\
	e-tree-model-generator.h	\
	e-cell-renderer-color.h

TEST_EDATASERVERUI_CPPFLAGS= \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/addressbook		\
	-I$(top_builddir)/addressbook		\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"	\
	$(CAMEL_CFLAGS)				\
	$(E_DATA_SERVER_UI_CFLAGS)

if ENABLE_GTK3
TEST_EDATASERVERUI_LDFLAGS = \
	libedataserverui-3.0.la 				\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)
else
TEST_EDATASERVERUI_LDFLAGS = \
	libedataserverui-1.2.la 				\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_UI_LIBS)
endif

# Test programs
test_category_completion_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_category_completion_SOURCES = test-category-completion.c
test_category_completion_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_source_selector_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_source_selector_SOURCES = test-source-selector.c
test_source_selector_LDADD =  $(TEST_EDATASERVERUI_LDFLAGS)

test_source_combo_box_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_source_combo_box_SOURCES = test-source-combo-box.c
test_source_combo_box_LDADD =  $(TEST_EDATASERVERUI_LDFLAGS)

test_contact_store_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_contact_store_SOURCES = test-contact-store.c
test_contact_store_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_name_selector_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_name_selector_SOURCES = test-name-selector.c
test_name_selector_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

%-$(LIBEDATASERVERUI_API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserverui-$(LIBEDATASERVERUI_API_VERSION).pc

ui_DATA = e-categories-dialog.ui e-name-selector-dialog.ui

BUILT_SOURCES = $(MARSHAL_GENERATED)

EXTRA_DIST = 						\
	$(ui_DATA)					\
	e-data-server-ui-marshal.list			\
	$(pkgconfig_DATA:-$(LIBEDATASERVERUI_API_VERSION).pc=.pc.in)

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk

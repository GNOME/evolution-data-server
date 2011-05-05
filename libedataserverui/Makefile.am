# The marshallers
MARSHAL_GENERATED = e-data-server-ui-marshal.c e-data-server-ui-marshal.h
@EVO_MARSHAL_RULE@

CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/addressbook		\
	-I$(top_builddir)/addressbook		\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"	\
	-DE_DATA_SERVER_UI_UIDIR=\""$(uidir)"\"	\
	$(E_DATA_SERVER_UI_CFLAGS)		\
	$(GNOME_KEYRING_CFLAGS)			\
	$(CAMEL_CFLAGS)

lib_LTLIBRARIES = libedataserverui-3.0.la

libedataserveruiinclude_HEADERS =	\
	e-categories-dialog.h		\
	e-categories-editor.h		\
	e-categories-selector.h		\
	e-category-completion.h		\
	e-category-editor.h		\
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

if OS_WIN32
e_passwords_build = e-passwords-win32.c
e_passwords_dist = e-passwords.c
else
e_passwords_build = e-passwords.c
e_passwords_dist = e-passwords-win32.c
endif

libedataserverui_3_0_la_SOURCES =	\
	$(MARSHAL_GENERATED)		\
	e-categories-dialog.c		\
	e-categories-editor.c		\
	e-categories-selector.c		\
	e-category-completion.c		\
	e-category-editor.c		\
	e-destination-store.c		\
	e-book-auth-util.c		\
	e-contact-store.c		\
	e-name-selector.c		\
	e-name-selector-dialog.c	\
	e-name-selector-entry.c		\
	e-name-selector-model.c		\
	e-name-selector-list.c		\
	$(e_passwords_build)		\
	e-source-selector.c		\
	e-source-selector-dialog.c	\
	e-source-combo-box.c		\
	e-tree-model-generator.c	\
	e-cell-renderer-color.c

libedataserverui_3_0_la_CPPFLAGS = $(CPPFLAGS)

libedataserverui_3_0_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(top_builddir)/camel/libcamel-1.2.la			\
	$(E_DATA_SERVER_UI_LIBS)				\
	$(GNOME_KEYRING_LIBS)					\
	$(CAMEL_LIBS)

libedataserverui_3_0_la_LDFLAGS = \
	-version-info $(LIBEDATASERVERUI_CURRENT):$(LIBEDATASERVERUI_REVISION):$(LIBEDATASERVERUI_AGE) $(NO_UNDEFINED)

libedataserveruiincludedir = $(privincludedir)/libedataserverui

%-3.0.pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserverui-3.0.pc

BUILT_SOURCES = $(MARSHAL_GENERATED)

EXTRA_DIST = 						\
	$(e_passwords_dist)				\
	e-data-server-ui-marshal.list			\
	$(pkgconfig_DATA:-3.0.pc=.pc.in)

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk

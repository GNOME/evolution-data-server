# Fix the code to not use E_DATA_SERVER_UI_CFLAGS

INCLUDES =							\
	-DG_LOG_DOMAIN=\"evolution-exchange-storage\"		\
	-DPREFIX=\"$(prefix)\"					\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"	 			\
	-DLIBDIR=\""$(datadir)"\"	 			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)\""		\
	-DCONNECTOR_GLADEDIR=\""$(gladedir)"\"			\
	$(KRB5_CFLAGS)						\
	$(LDAP_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(E_DATA_SERVER_CFLAGS)					\
	$(E_DATA_SERVER_UI_CFLAGS)				\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/servers/exchange/lib			\
	-I$(top_srcdir)/servers/exchange/xntlm					

lib_LTLIBRARIES =						\
	libexchange-storage-1.2.la

libexchange_storage_1_2_la_SOURCES =				\
	$(MARSHAL_GENERATED)					\
	e-folder.c						\
	e-folder.h						\
	e-folder-exchange.c					\
	e-folder-exchange.h					\
	e-folder-tree.c						\
	e-folder-tree.h						\
	e-folder-type-registry.c				\
	e-folder-type-registry.h				\
	e-storage.c						\
	e-storage.h						\
	exchange-account.c					\
	exchange-account.h					\
	exchange-folder-size.c					\
	exchange-folder-size.h					\
	exchange-hierarchy-favorites.c				\
	exchange-hierarchy-favorites.h				\
	exchange-hierarchy-gal.c				\
	exchange-hierarchy-gal.h				\
	exchange-hierarchy-somedav.c				\
	exchange-hierarchy-somedav.h				\
	exchange-hierarchy-webdav.c				\
	exchange-hierarchy-webdav.h				\
	exchange-hierarchy.c					\
	exchange-hierarchy.h					\
	exchange-oof.c						\
	exchange-oof.h						\
	exchange-types.h					

e-shell-marshal.h: e-shell-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=e_shell_marshal $(srcdir)/e-shell-marshal.list --header > e-shell-marshal.h.tmp \
	&& mv e-shell-marshal.h.tmp e-shell-marshal.h ) \
	|| ( rm -f e-shell-marshal.h.tmp && exit 1 )

e-shell-marshal.c: e-shell-marshal.h
	( (echo '#include "e-shell-marshal.h"'; @GLIB_GENMARSHAL@ --prefix=e_shell_marshal $(srcdir)/e-shell-marshal.list --body) > e-shell-marshal.c.tmp \
	&& mv e-shell-marshal.c.tmp e-shell-marshal.c ) \
	|| ( rm -f e-shell-marshal.c.tmp && exit 1 )

MARSHAL_GENERATED = e-shell-marshal.c e-shell-marshal.h

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES = $(MARSHAL_GENERATED)
NODIST_FILES = $(MARSHAL_GENERATED)

libexchange_storage_1_2_la_LIBADD =				\
	$(E_DATA_SERVER_LIBS)					\
	$(E_DATA_SERVER_UI_LIBS)				\
	$(LDAP_LIBS)						\
	$(SOUP_LIBS)

libexchange_storage_1_2_la_LDFLAGS =				\
	$(KRB5_LDFLAGS)						\
	$(top_builddir)/servers/exchange/lib/libexchange.la	\
	$(top_builddir)/servers/exchange/xntlm/libxntlm.la			
	-version-info $(LIBEXCHANGE_STORAGE_CURRENT):$(LIBEXCHANGE_STORAGE_REVISION):$(LIBEXCHANGE_STORAGE_AGE)

libexchange_storageincludedir = $(privincludedir)/exchange

libexchange_storageinclude_HEADERS = 				\
	e-folder.h						\
	e-folder-exchange.h					\
	e-storage.h						\
	exchange-account.h					\
	exchange-constants.h					\
	exchange-folder-size.h					\
	exchange-hierarchy.h					\
	exchange-hierarchy-somedav.h				\
	exchange-hierarchy-webdav.h				\
	exchange-oof.h						\
	exchange-types.h					

%-$(API_VERSION).pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libexchange-storage-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in) \
		e-shell-marshal.list

DISTCLEANFILES = $(pkgconfig_DATA)

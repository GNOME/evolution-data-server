privlibexec_SCRIPTS = csv2vcard

privlibexec_PROGRAMS = addressbook-export

addressbook_export_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	-DG_LOG_DOMAIN=\"addressbook-export\"	\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	-DLOCALEDIR=\""$(localedir)"\"		\
	-DPREFIX=\""$(prefix)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DDATADIR=\""$(datadir)"\"		\
	-DLIBDIR=\""$(libdir)"\"		\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/addressbook		\
	-I$(top_builddir)			\
	-I$(top_builddir)/addressbook		\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) 	\
	$(CODE_COVERAGE_CFLAGS)

addressbook_export_SOURCES =			\
	addressbook-export.c

addressbook_export_LDADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS)

addressbook_export_LDFLAGS =	\
	$(AM_LDFLAGS)		\
	$(CODE_COVERAGE_LDFLAGS)

if OS_WIN32
addressbook_export_LDFLAGS += -mwindows
endif

EXTRA_DIST = $(privlibexec_SCRIPTS)

-include $(top_srcdir)/git.mk

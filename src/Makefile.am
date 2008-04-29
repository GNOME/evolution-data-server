INCLUDES =						\
	-DG_LOG_DOMAIN=\"evolution-data-server\"	\
	-DPREFIX=\"$(prefix)\"				\
	-DSYSCONFDIR=\"$(sysconfdir)\"			\
	-DDATADIR=\"$(datadir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_srcdir)/calendar			\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	$(SOUP_CFLAGS)					\
	$(E_FACTORY_CFLAGS)				\
	$(E_DATA_SERVER_CFLAGS)


IDLS = \
	Evolution-DataServer.idl

IDL_GENERATED_H = \
	Evolution-DataServer.h

IDL_GENERATED_C =				\
	Evolution-DataServer-common.c		\
	Evolution-DataServer-skels.c		\
	Evolution-DataServer-stubs.c

IDL_GENERATED = $(IDL_GENERATED_H) $(IDL_GENERATED_C)

$(IDL_GENERATED_H): $(IDLS)
	$(ORBIT_IDL) -I $(srcdir) $(IDL_INCLUDES) $(srcdir)/Evolution-DataServer.idl

$(IDL_GENERATED_C): $(IDL_GENERATED_H)


noinst_PROGRAMS =			\
	evolution-data-server

evolution_data_server_SOURCES = \
	$(IDL_GENERATED)		\
	server-interface-check.c	\
	server-interface-check.h	\
	server-logging.c		\
	server-logging.h		\
	server.c                        \
	offline-listener.c              \
	offline-listener.h              

if ENABLE_CALENDAR
E_DATA_CAL_LIBS=$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la
endif

evolution_data_server_LDADD =                     			\
        $(top_builddir)/libebackend/libebackend-1.2.la			\
        $(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la  \
        $(E_DATA_CAL_LIBS)                                      	\
        $(E_FACTORY_LIBS)						\
	$(E_DATA_SERVER_LIBS)

install-evolution-data-servers:
	$(mkinstalldirs) $(DESTDIR)$(libexecdir)
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) evolution-data-server $(DESTDIR)$(libexecdir)/evolution-data-server-$(BASE_VERSION)

SERVER_IN_FILE=GNOME_Evolution_DataServer.server.in.in

server_in_files = GNOME_Evolution_DataServer.server.in.in
server_DATA = $(server_in_files:.server.in.in=_$(API_VERSION).server)
@EVO_SUBST_SERVER_RULE@
@EVO_NAME_SERVER_RULE@
@INTLTOOL_SERVER_RULE@

idl_DATA = $(IDLS)

EXTRA_DIST = 						\
	$(server_in_files)				\
	$(idl_DATA)

BUILT_SOURCES = $(IDL_GENERATED) $(server_DATA)
CLEANFILES = $(BUILT_SOURCES)

install-exec-local: install-evolution-data-servers

uninstall-local:
	rm -f $(DESTDIR)$(libexecdir)/evolution-data-server-$(BASE_VERSION)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

if ENABLE_PURIFY
PLINK = $(LIBTOOL) --mode=link $(PURIFY) $(CCLD) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

all-local: evolution-data-server.pure

evolution-data-server.pure: evolution-data-server
	@rm -f evolution-data-server.pure
	$(PLINK) $(evolution_data_server_LDFLAGS) $(evolution_data_server_OBJECTS) $(evolution_data_server_LDADD) $(LIBS)

endif

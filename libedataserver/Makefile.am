INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	-I$(top_srcdir)/libdb/dist					\
	$(E_DATA_SERVER_CFLAGS)

# The marshallers
MARSHAL_GENERATED = e-data-server-marshal.c e-data-server-marshal.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserver.la
noinst_PROGRAMS = test-source-list

libedataserver_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	e-account-list.c		\
	e-account.c			\
	e-component-listener.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-file-cache.c			\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-sexp.c			\
	e-source-group.c		\
	e-source-list.c			\
	e-source.c			\
	e-uid.c				\
	e-url.c				\
	e-util.c			\
	e-xml-hash-utils.c		\
	md5-utils.c

libedataserver_la_LIBADD = 				\
	$(E_DATA_SERVER_LIBS)				\
	$(top_builddir)/libdb/dist/libdb-4.1.la

libedataserver_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE)

libedataserverincludedir = $(privincludedir)/libedataserver

libedataserverinclude_HEADERS =		\
	e-account-list.h		\
	e-account.h			\
	e-component-listener.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-file-cache.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-sexp.h			\
	e-source-group.h		\
	e-source-list.h			\
	e-source.h			\
	e-uid.h				\
	e-url.h				\
	e-util.h			\
	e-xml-hash-utils.h		\
	md5-utils.h

test_source_list_SOURCES = test-source-list.c
test_source_list_LDADD = libedataserver.la $(E_DATA_SERVER_LIBS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-1.0.pc

EXTRA_DIST = 				\
	$(pkgconfig_DATA:.pc=.pc.in)

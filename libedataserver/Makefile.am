SUBDIRS = ename

INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	-DG_DISABLE_DEPRECATED						\
	$(DB3_CFLAGS)							\
	$(E_DATA_SERVER_CFLAGS)

# The marshallers
MARSHAL_GENERATED = e-data-server-marshal.c e-data-server-marshal.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserver.la

libedataserver_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	e-account-list.c		\
	e-account.c			\
	e-component-listener.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-msgport.c			\
	e-sexp.c			\
	e-uid.c				\
	e-url.c				\
	e-util.c			\
	e-xml-hash-utils.c		\
	md5-utils.c

libedataserver_la_LIBADD = 				\
	$(DB3_LDADD)					\
	$(E_DATA_SERVER_LIBS)

libedataserver_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE)	\
	-no-undefined

libedataserverincludedir = $(includedir)/evolution-data-server-1.0/libedataserver

libedataserverinclude_HEADERS =		\
	e-account-list.h		\
	e-account.h			\
	e-component-listener.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-msgport.h			\
	e-sexp.h			\
	e-uid.h				\
	e-url.h				\
	e-util.h			\
	e-xml-hash-utils.h		\
	md5-utils.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-1.0.pc

EXTRA_DIST = 				\
	$(pkgconfig_DATA:.pc=.pc.in)

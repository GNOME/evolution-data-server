INCLUDES =								\
	-I$(top_srcdir)							\
	-DG_LOG_DOMAIN=\"e-data-server\"				\
	-I$(top_srcdir)/libdb/dist					\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"		\
	$(NSS_CFLAGS)							\
	$(E_DATA_SERVER_CFLAGS)

# The marshallers
MARSHAL_GENERATED = e-data-server-marshal.c e-data-server-marshal.h
@EVO_MARSHAL_RULE@

lib_LTLIBRARIES = libedataserver-1.2.la
noinst_PROGRAMS = test-source-list

libedataserver_1_2_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	e-account-list.c		\
	e-account.c			\
	e-component-listener.c		\
	e-data-server-module.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-file-cache.c			\
	e-iconv.c			\
	e-iterator.c			\
	e-list.c			\
	e-list-iterator.c		\
	e-memory.c			\
	e-msgport.c			\
	e-sexp.c			\
	e-source-group.c		\
	e-source-list.c			\
	e-source.c			\
	e-time-utils.c			\
	e-uid.c				\
	e-url.c				\
	e-util.c			\
	e-trie.c			\
	e-xml-hash-utils.c		\
	md5-utils.c

libedataserver_1_2_la_LIBADD = 				\
	$(NSS_LIBS)					\
	$(E_DATA_SERVER_LIBS)				\
	$(ICONV_LIBS)					\
	$(top_builddir)/libdb/dist/libdb-4.1.la

libedataserver_1_2_la_LDFLAGS = 										\
	-version-info $(LIBEDATASERVER_CURRENT):$(LIBEDATASERVER_REVISION):$(LIBEDATASERVER_AGE)

libedataserverincludedir = $(privincludedir)/libedataserver

libedataserverinclude_HEADERS =		\
	e-account-list.h		\
	e-account.h			\
	e-component-listener.h		\
	e-data-server-module.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-file-cache.h			\
	e-iconv.h			\
	e-iterator.h			\
	e-list.h			\
	e-list-iterator.h		\
	e-memory.h			\
	e-msgport.h			\
	e-sexp.h			\
	e-source-group.h		\
	e-source-list.h			\
	e-source.h			\
	e-time-utils.h			\
	e-trie.h			\
	e-uid.h				\
	e-url.h				\
	e-util.h			\
	e-xml-hash-utils.h		\
	md5-utils.h

test_source_list_SOURCES = test-source-list.c
test_source_list_LDADD = libedataserver-1.2.la $(E_DATA_SERVER_LIBS)

%-$(BASE_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libedataserver-$(BASE_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(BASE_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)
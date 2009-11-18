noinst_PROGRAMS = create-account soap-test

SERVERS_GROUPWISE_CPPFLAGS= \
	-DG_LOG_DOMAIN=\"libegroupwise\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(E_DATA_SERVER_CFLAGS)

create_account_CPPFLAGS = $(SERVERS_GROUPWISE_CPPFLAGS)
create_account_SOURCES = create-account.c
create_account_LDADD = 						\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(SOUP_LIBS)						\
	$(E_DATA_SERVER_LIBS)

soap_test_CPPFLAGS = $(SERVERS_GROUPWISE_CPPFLAGS)
soap_test_SOURCES = soap-test.c
soap_test_LDADD = 						\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(SOUP_LIBS)						\
	libegroupwise-1.2.la					\
	$(E_DATA_SERVER_LIBS)

build-timestamp.h : $(top_builddir)/config.h
	echo "const char *build_timestamp = \"Evolution-Data-Server-@BASE_VERSION@-`date +%F`\";" > $@

BUILT_SOURCES = build-timestamp.h
NODIST_FILES = build-timestamp.h
CLEANFILES = build-timestamp.h

lib_LTLIBRARIES = libegroupwise-1.2.la

libegroupwise_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libegroupwise\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(SOUP_CFLAGS)					\
	$(E_DATA_SERVER_CFLAGS)

libegroupwise_1_2_la_SOURCES =			\
	e-gw-connection.c			\
	e-gw-connection.h			\
	e-gw-container.c			\
	e-gw-container.h			\
	e-gw-item.c				\
	e-gw-item.h				\
	e-gw-sendoptions.h			\
	e-gw-sendoptions.c			\
	e-gw-message.c				\
	e-gw-message.h                          \
	e-gw-filter.c                           \
	e-gw-filter.h                           \
	e-gw-recur-utils.c                      \
	e-gw-recur-utils.h			\
	e-gw-proxy.h				\
	e-gw-proxy.c				\
	soup-soap-message.c			\
	soup-soap-response.c			\
	build-timestamp.h

libegroupwise_1_2_la_LIBADD =						\
	../../libedataserver/libedataserver-1.2.la			\
	$(E_DATA_SERVER_LIBS)						\
	$(SOUP_LIBS)

libegroupwise_1_2_la_LDFLAGS =                                               \
        -version-info $(LIBEGROUPWISE_CURRENT):$(LIBEGROUPWISE_REVISION):$(LIBEGROUPWISE_AGE) $(NO_UNDEFINED)

libegroupwiseincludedir = $(privincludedir)/groupwise

libegroupwiseinclude_HEADERS =                  \
        e-gw-connection.h                       \
	e-gw-container.h                        \
	e-gw-message.h                          \
	e-gw-item.h                             \
	e-gw-sendoptions.h			\
	e-gw-filter.h                        	\
	e-gw-recur-utils.h			\
	e-gw-proxy.h				\
	soup-soap-message.h			\
	soup-soap-response.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libegroupwise-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


-include $(top_srcdir)/git.mk

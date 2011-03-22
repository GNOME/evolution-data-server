noinst_PROGRAMS = test-source-list

test_source_list_CPPFLAGS =					\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"e-data-server\"			\
	$(E_DATA_SERVER_CFLAGS)

test_source_list_SOURCES = test-source-list.c

test_source_list_LDADD =					\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_LIBS)

-include $(top_srcdir)/git.mk

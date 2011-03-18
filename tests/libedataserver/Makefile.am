TESTS = e-source-test

noinst_PROGRAMS = $(TESTS)

e_source_test_CPPFLAGS =					\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"e-data-server\"			\
	$(E_DATA_SERVER_CFLAGS)					\
	$(GIO_UNIX_CFLAGS)

e_source_test_LDADD =						\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_LIBS)					\
	$(GIO_UNIX_LIBS)

-include $(top_srcdir)/git.mk

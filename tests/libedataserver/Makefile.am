TESTS = \
	e-phone-utils-test \
	e-source-test \
	$(NULL)

noinst_PROGRAMS = $(TESTS)

TEST_CPPFLAGS =							\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"e-data-server\"			\
	$(E_DATA_SERVER_CFLAGS)					\
	$(GIO_UNIX_CFLAGS)

TEST_LDADD =							\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_LIBS)					\
	$(GIO_UNIX_LIBS)

e_phone_utils_test_CPPFLAGS = $(TEST_CPPFLAGS)
e_phone_utils_test_LDADD = $(TEST_LDADD)

e_source_test_CPPFLAGS = $(TEST_CPPFLAGS)
e_source_test_LDADD = $(TEST_LDADD)

-include $(top_srcdir)/git.mk

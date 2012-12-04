TESTS = e-source-test

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

e_source_test_CPPFLAGS = $(TEST_CPPFLAGS)
e_source_test_LDADD = $(TEST_LDADD)

if ENABLE_PHONENUMBER
TESTS += e-phone-utils-test
e_phone_utils_test_CPPFLAGS = $(TEST_CPPFLAGS)
e_phone_utils_test_LDADD = $(TEST_LDADD)
endif ENABLE_PHONENUMBER

-include $(top_srcdir)/git.mk

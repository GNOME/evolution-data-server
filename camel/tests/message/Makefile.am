MESSAGE_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS)						\
	-I$(includedir)						\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/camel/tests/lib		\
	-DG_LOG_DOMAIN=\"evolution-tests\"	\
	$(CAMEL_CFLAGS)

MESSAGE_TESTS_LDADD= \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a			\
	$(top_builddir)/camel/libcamel-${API_VERSION}.la 		\
	$(INTLLIBS)

check_PROGRAMS = \
	test1		\
	test2		\
	test3		\
	test4

test1_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)
test2_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)
test3_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)
test4_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)

test1_LDADD = $(MESSAGE_TESTS_LDADD)
test2_LDADD = $(MESSAGE_TESTS_LDADD)
test3_LDADD = $(MESSAGE_TESTS_LDADD)
test4_LDADD = $(MESSAGE_TESTS_LDADD)

CLEANFILES = test3.msg test3-2.msg test3-3.msg

-include $(top_srcdir)/git.mk

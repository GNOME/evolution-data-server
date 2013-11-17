MESSAGE_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(includedir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir)/camel/tests/lib \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	$(CAMEL_CFLAGS) \
	$(NULL)

MESSAGE_TESTS_LDADD= \
	$(top_builddir)/camel/tests/lib/libcameltest.a \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(INTLLIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

check_PROGRAMS = \
	test1 \
	test2 \
	test4 \
	$(NULL)

test1_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)
test2_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)
test4_CPPFLAGS = $(MESSAGE_TESTS_CPPFLAGS)

test1_LDADD = $(MESSAGE_TESTS_LDADD)
test2_LDADD = $(MESSAGE_TESTS_LDADD)
test4_LDADD = $(MESSAGE_TESTS_LDADD)

-include $(top_srcdir)/git.mk

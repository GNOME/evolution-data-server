STREAM_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS)					\
	-I$(includedir)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/camel/tests/lib 		\
	-DG_LOG_DOMAIN=\"evolution-tests\"		\
	$(CAMEL_CFLAGS)

STREAM_TESTS_LDADD= \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la 		\
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a	\
	$(INTLLIBS)

check_PROGRAMS =  	\
	test1 test2 test3

test1_CPPFLAGS = $(STREAM_TESTS_CPPFLAGS)
test1_LDADD = $(STREAM_TESTS_LDADD)
test2_CPPFLAGS = $(STREAM_TESTS_CPPFLAGS)
test2_LDADD = $(STREAM_TESTS_LDADD)
test3_CPPFLAGS = $(STREAM_TESTS_CPPFLAGS)
test3_LDADD = $(STREAM_TESTS_LDADD)

#TESTS = test1 test2 test3

-include $(top_srcdir)/git.mk

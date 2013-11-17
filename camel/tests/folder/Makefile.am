NULL =

FOLDER_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(includedir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir)/camel/tests/lib \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	$(CAMEL_CFLAGS) \
	$(NULL)

FOLDER_TESTS_LDADD= \
	$(top_builddir)/camel/tests/lib/libcameltest.a \
	$(top_builddir)/camel/tests/lib/libcameltest-provider.a \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la \
	$(INTLLIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

check_PROGRAMS = \
	test1 \
	test2 \
	test3 \
	test4 \
	test5 \
	test6 \
	test7 \
	test8 \
	test9 \
	test10 \
	test11 \
	$(NULL)

test1_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test2_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test3_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test4_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test5_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test6_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test7_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test8_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test9_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test10_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)
test11_CPPFLAGS = $(FOLDER_TESTS_CPPFLAGS)

test1_LDADD = $(FOLDER_TESTS_LDADD)
test2_LDADD = $(FOLDER_TESTS_LDADD)
test3_LDADD = $(FOLDER_TESTS_LDADD)
test4_LDADD = $(FOLDER_TESTS_LDADD)
test5_LDADD = $(FOLDER_TESTS_LDADD)
test6_LDADD = $(FOLDER_TESTS_LDADD)
test7_LDADD = $(FOLDER_TESTS_LDADD)
test8_LDADD = $(FOLDER_TESTS_LDADD)
test9_LDADD = $(FOLDER_TESTS_LDADD)
test10_LDADD = $(FOLDER_TESTS_LDADD)
test11_LDADD = $(FOLDER_TESTS_LDADD)

-include $(top_srcdir)/git.mk

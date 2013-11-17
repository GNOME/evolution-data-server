NULL =

MISC_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(includedir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir)/camel/tests/lib \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	$(CAMEL_CFLAGS) \
	$(NULL)

MISC_TESTS_LDADD= \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a \
	$(INTLLIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

check_PROGRAMS = \
	test1 \
	test2 \
	url \
	url-scan \
	utf7 \
	split \
	rfc2047 \
	$(NULL)

test1_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
test1_LDADD = $(MISC_TESTS_LDADD)
test2_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
test2_LDADD = $(MISC_TESTS_LDADD)
url_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
url_LDADD = $(MISC_TESTS_LDADD)
url_scan_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
url_scan_LDADD  = $(MISC_TESTS_LDADD)
utf7_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
utf7_LDADD = $(MISC_TESTS_LDADD)
split_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
split_LDADD = $(MISC_TESTS_LDADD)
rfc2047_CPPFLAGS = $(MISC_TESTS_CPPFLAGS)
rfc2047_LDADD = $(MISC_TESTS_LDADD)

-include $(top_srcdir)/git.mk

NULL =

MIMEFILTER_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(includedir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir)/camel/tests/lib \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	-DSOURCEDIR=\"$(srcdir)\" \
	$(CAMEL_CFLAGS) \
	$(NULL)

MIMEFILTER_TESTS_LDADD= \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a \
	$(INTLLIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

EXTRA_DIST = \
	crlf-1.in \
	crlf-1.out \
	charset-iso-2022-jp.0.in \
	charset-iso-2022-jp.0.out \
	charset-gb2312.0.in \
	charset-gb2312.0.out \
	$(NULL)

check_PROGRAMS = \
	test1 \
	test-crlf \
	test-charset \
	test-tohtml \
	$(NULL)

test1_CPPFLAGS = $(MIMEFILTER_TESTS_CPPFLAGS)
test1_LDADD = $(MIMEFILTER_TESTS_LDADD)
test_crlf_CPPFLAGS = $(MIMEFILTER_TESTS_CPPFLAGS)
test_crlf_LDFLAGS = $(MIMEFILTER_TESTS_LDADD)
test_charset_CPPFLAGS = $(MIMEFILTER_TESTS_CPPFLAGS)
test_charset_LDFLAGS = $(MIMEFILTER_TESTS_LDADD)
test_tohtml_CPPFLAGS = $(MIMEFILTER_TESTS_CPPFLAGS)
test_tohtml_LDFLAGS = $(MIMEFILTER_TESTS_LDADD)

-include $(top_srcdir)/git.mk

NULL =

SMIME_TESTS_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(includedir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir)/camel/tests/lib \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	-DTEST_DATA_DIR=\"$(top_srcdir)/camel/tests/data\"\
	$(CAMEL_CFLAGS) \
	$(NULL)

SMIME_TESTS_LDADD= \
	$(top_builddir)/camel/tests/lib/libcameltest.a \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la \
	$(INTLLIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

check_PROGRAMS = \
	pgp
#	pgp-mime	
#	pkcs7

pgp_CPPFLAGS = $(SMIME_TESTS_CPPFLAGS)
pgp_LDADD = $(SMIME_TESTS_LDADD)

# pgp_mime_CPPFLAGS = $(SMIME_TESTS_CPPFLAGS)
# pgp_mime_LDADD = $(SMIME_TESTS_LDADD)
# pkcs7_CPPFLAGS = $(SMIME_TESTS_CPPFLAGS)
# pkcs7_LDADD = $(SMIME_TESTS_LDADD)

-include $(top_srcdir)/git.mk

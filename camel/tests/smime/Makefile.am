
INCLUDES =						\
	-I$(includedir)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/intl			 	\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/camel/tests/lib 		\
	-DG_LOG_DOMAIN=\"evolution-tests\"		\
	-DTEST_DATA_DIR=\"$(top_srcdir)/camel/tests/data\"\
	$(CAMEL_CFLAGS)

LDADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a	\
	$(top_builddir)/camel/libcamel-provider-${API_VERSION}.la 		\
	$(top_builddir)/camel/libcamel-${API_VERSION}.la 		\
	$(INTLLIBS)

check_PROGRAMS =  	\
	pgp
#	pgp-mime	
#	pkcs7

TESTS = pgp #pgp-mime pkcs7

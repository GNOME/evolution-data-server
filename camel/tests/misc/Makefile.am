
INCLUDES =						\
	-I$(includedir)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/intl			 	\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/camel/tests/lib 		\
	-DG_LOG_DOMAIN=\"evolution-tests\"		\
	$(CAMEL_CFLAGS)

LDADD = \
	$(top_builddir)/camel/libcamel-provider-${BASE_VERSION}.la 	\
	$(top_builddir)/camel/libcamel-${BASE_VERSION}.la 		\
	$(top_builddir)/libedataserver/libedataserver-${BASE_VERSION}.la \
	$(top_builddir)/camel/tests/lib/libcameltest.a	\
	$(INTLLIBS)

check_PROGRAMS =  	\
	test1		\
	test2		\
	url		\
	url-scan	\
	utf7		\
	split

TESTS = url utf7 split url-scan test2




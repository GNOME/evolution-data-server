check_LIBRARIES = libcameltest.a libcameltest-provider.a

libcameltest_a_SOURCES = \
	camel-test.c camel-test.h \
	messages.c messages.h \
	addresses.c addresses.h \
	folders.c folders.h \
	session.c session.h \
	streams.c streams.h \
	address-data.h

libcameltest_provider_a_CPPFLAGS = \
	$(AM_CPPFLAGS)								\
	-I$(includedir)								\
	-I$(top_srcdir)								\
	-I$(top_srcdir)/camel						\
	-DCAMEL_BUILD_DIR=\"$(top_builddir)/camel\"	\
	$(CAMEL_CFLAGS)

libcameltest_provider_a_SOURCES = \
	camel-test-provider.c camel-test-provider.h

libcameltest_a_CPPFLAGS = \
	$(AM_CPPFLAGS)								\
	-I$(includedir)								\
	-I$(top_srcdir)								\
	-I$(top_srcdir)/camel						\
	-DCAMEL_BUILD_DIR=\"$(top_builddir)/camel\"	\
	$(CAMEL_CFLAGS)

libcameltest_a_LIBADD =				\
	$(top_builddir)/camel/libcamel-${API_VERSION}.la		\
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la

libcameltest_provider_a_LIBADD =				\
	$(top_builddir)/camel/libcamel-provider-${API_VERSION}.la \
	$(top_builddir)/camel/libcamel-${API_VERSION}.la		\
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la

-include $(top_srcdir)/git.mk

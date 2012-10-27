NULL =

@GNOME_CODE_COVERAGE_RULES@

TESTS = \
	e-source-test \
	e-source-registry-test \
	$(NULL)

noinst_PROGRAMS = $(TESTS)

test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"e-data-server\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(NULL)

test_LDADD = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

e_source_test_SOURCES = \
	e-source-test.c \
	$(NULL)

e_source_registry_test_SOURCES = \
	e-source-registry-test.c \
	e-test-dbus-utils.c \
	e-test-dbus-utils.h \
	$(NULL)

e_source_test_CPPFLAGS = $(test_CPPFLAGS)
e_source_test_LDADD = $(test_LDADD)

e_source_registry_test_CPPFLAGS = $(test_CPPFLAGS)
e_source_registry_test_LDADD = $(test_LDADD)

-include $(top_srcdir)/git.mk

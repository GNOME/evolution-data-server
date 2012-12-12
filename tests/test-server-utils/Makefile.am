NULL =

common_cflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-DG_LOG_DOMAIN=\"e-test-server-utils\" \
	-DEDS_TEST_DBUS_SERVICE_DIR=\""$(abs_top_builddir)/tests/test-server-utils/services"\" \
	-DEDS_TEST_WORK_DIR=\""$(abs_top_builddir)/tests/test-server-utils/cache"\" \
	-DEDS_TEST_SCHEMA_DIR=\""$(abs_top_builddir)/data"\" \
	-DEDS_TEST_ADDRESS_BOOK_DIR=\""$(abs_top_builddir)/addressbook/backends/file/.libs"\" \
	-DEDS_TEST_CALENDAR_DIR=\""$(abs_top_builddir)/calendar/backends/file/.libs"\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(NULL)

common_libs = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

######################################
#         Utility Library            #
######################################
noinst_LTLIBRARIES = libetestserverutils.la

libetestserverutils_la_CPPFLAGS = $(common_cflags)
libetestserverutils_la_LIBADD = $(common_libs)
libetestserverutils_la_SOURCES = \
	e-test-server-utils.c \
	e-test-server-utils.h \
	$(NULL)

######################################
#         Local Unit Tests           #
######################################
noinst_PROGRAMS = $(TESTS)

TESTS = test-fixture

test_fixture_CPPFLAGS = $(common_cflags)
test_fixture_LDADD    = $(common_libs) libetestserverutils.la
test_fixture_SOURCES  = test-fixture.c

-include $(top_srcdir)/git.mk

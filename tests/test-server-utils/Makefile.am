NULL =

SUBDIRS = services

@GNOME_CODE_COVERAGE_RULES@

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
	-DEDS_TEST_REGISTRY_DIR=\""$(abs_top_builddir)/modules/cache-reaper/.libs"\" \
	-DEDS_TEST_CAMEL_DIR=\""$(abs_top_builddir)/camel/providers/local/.libs"\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(NULL)

common_libs = \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(E_DATA_SERVER_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

######################################
#         Utility Library            #
######################################
if EDS_INSTALLED_TESTS_ENABLED
lib_LTLIBRARIES = libetestserverutils.la
else
noinst_LTLIBRARIES = libetestserverutils.la
endif

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

######################################
# The test directory needs to be     #
# cleaned in order to pass distcheck #
######################################
clean-local: clean-local-check
.PHONY: clean-local-check
clean-local-check:
	-rm -rf $(abs_top_builddir)/tests/test-server-utils/cache

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

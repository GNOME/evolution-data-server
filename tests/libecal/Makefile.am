SUBDIRS = client

@GNOME_CODE_COVERAGE_RULES@

noinst_LTLIBRARIES = libecal-test-utils.la
libecal_test_utils_la_SOURCES = \
	ecal-test-utils.c \
	ecal-test-utils.h \
	$(NULL)

libecal_test_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DLOCALEDIR=\""$(localedir)"\" \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/test-server-utils \
	-I$(top_builddir)/tests/test-server-utils \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

libecal_test_utils_la_LIBADD = \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

TEST_ECAL_LIBS = \
	$(libecal_test_utils_la_LIBADD) \
	libecal-test-utils.la \
	$(NULL)

# ordered by relative complexity
TESTS = \
	test-ecal-get-timezone \
	test-ecal-add-timezone \
	test-ecal-set-default-timezone \
	test-ecal-get-alarm-email-address \
	test-ecal-get-cal-address \
	test-ecal-get-ldap-attribute \
	test-ecal-get-capabilities \
	test-ecal-get-default-object \
	test-ecal-create-object \
	test-ecal-create-object--2 \
	test-ecal-get-objects-for-uid \
	test-ecal-remove-object \
	test-ecal-get-object-list \
	test-ecal-modify-object \
	test-ecal-send-objects \
	test-ecal-receive-objects \
	test-ecal-get-query \
	$(NULL)

# test-ecal-get-free-busy:
#   broken by design, the API needs to be fixed.
BROKEN_TESTS = \
	test-ecal-get-free-busy \
	$(NULL)

# The test program
noinst_PROGRAMS =  \
	$(TESTS) \
	$(BROKEN_TESTS) \
	$(NULL)


TEST_ECAL_CPPFLAGS = \
	$(libecal_test_utils_la_CPPFLAGS) \
	$(NULL)

# ordered alphanumerically
test_ecal_add_timezone_LDADD=$(TEST_ECAL_LIBS)
test_ecal_add_timezone_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_create_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_create_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_create_object__2_LDADD=$(TEST_ECAL_LIBS)
test_ecal_create_object__2_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_alarm_email_address_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_alarm_email_address_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_cal_address_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_cal_address_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_default_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_default_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_free_busy_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_free_busy_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_ldap_attribute_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_ldap_attribute_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_capabilities_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_capabilities_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_object_list_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_object_list_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_objects_for_uid_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_objects_for_uid_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_query_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_query_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_timezone_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_timezone_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_modify_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_modify_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_receive_objects_LDADD=$(TEST_ECAL_LIBS)
test_ecal_receive_objects_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_remove_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_remove_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_send_objects_LDADD=$(TEST_ECAL_LIBS)
test_ecal_send_objects_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_set_default_timezone_LDADD=$(TEST_ECAL_LIBS)
test_ecal_set_default_timezone_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

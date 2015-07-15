
TEST_CPPFLAGS= \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/test-server-utils \
	-I$(top_builddir)/tests/test-server-utils \
	-DSRCDIR=\""$(abs_srcdir)"\" \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

# ordered by relative complexity
TESTS = \
	test-cal-client-refresh \
	test-cal-client-add-timezone \
	test-cal-client-create-object \
	test-cal-client-remove-object \
	test-cal-client-get-object-list \
	test-cal-client-modify-object \
	test-cal-client-send-objects \
	test-cal-client-receive-objects \
        test-cal-client-bulk-methods \
	test-cal-client-get-attachment-uris \
	test-cal-client-get-view \
	test-cal-client-revision-view \
	test-cal-client-get-revision \
	test-cal-client-get-free-busy \
	$(NULL)

# The test program
noinst_PROGRAMS = $(TESTS) $(BROKEN_TESTS)

# ordered alphanumerically
test_cal_client_add_timezone_LDADD=$(TEST_LIBS)
test_cal_client_add_timezone_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_create_object_LDADD=$(TEST_LIBS)
test_cal_client_create_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_get_attachment_uris_LDADD=$(TEST_LIBS)
test_cal_client_get_attachment_uris_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_get_free_busy_LDADD=$(TEST_LIBS)
test_cal_client_get_free_busy_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_get_object_list_LDADD=$(TEST_LIBS)
test_cal_client_get_object_list_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_get_view_LDADD=$(TEST_LIBS)
test_cal_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_revision_view_LDADD=$(TEST_LIBS)
test_cal_client_revision_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_modify_object_LDADD=$(TEST_LIBS)
test_cal_client_modify_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_get_revision_LDADD=$(TEST_LIBS)
test_cal_client_get_revision_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_receive_objects_LDADD=$(TEST_LIBS)
test_cal_client_receive_objects_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_bulk_methods_LDADD=$(TEST_LIBS)
test_cal_client_bulk_methods_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_refresh_LDADD=$(TEST_LIBS)
test_cal_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_remove_object_LDADD=$(TEST_LIBS)
test_cal_client_remove_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_cal_client_send_objects_LDADD=$(TEST_LIBS)
test_cal_client_send_objects_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

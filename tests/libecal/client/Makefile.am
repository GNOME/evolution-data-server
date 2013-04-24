
TEST_CPPFLAGS=							\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook     			\
	-I$(top_builddir)/addressbook   			\
	-I$(top_srcdir)/calendar				\
	-I$(top_builddir)/calendar				\
	-I$(top_srcdir)/tests/test-server-utils     		\
	-I$(top_builddir)/tests/test-server-utils   		\
	-DSRCDIR=\""$(abs_srcdir)"\"				\
	$(EVOLUTION_CALENDAR_CFLAGS)				\
	$(CAMEL_CFLAGS)						\
	$(NULL)

TEST_LIBS =							\
	$(top_builddir)/calendar/libecal/libecal-1.2.la		\
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la	\
	$(EVOLUTION_CALENDAR_LIBS)				\
	$(CAMEL_LIBS)						\
	$(NULL)

# ordered by relative complexity
TESTS = 					\
	test-client-refresh			\
	test-client-add-timezone		\
	test-client-create-object		\
	test-client-remove-object		\
	test-client-get-object-list		\
	test-client-modify-object		\
	test-client-send-objects		\
	test-client-receive-objects		\
        test-client-bulk-methods		\
	test-client-get-attachment-uris		\
	test-client-get-view			\
	test-client-revision-view		\
	test-client-get-revision		\
	$(NULL)

# test-client-get-free-busy:
#   broken by design, the API needs to be fixed.
#
BROKEN_TESTS =		\
	test-client-get-free-busy

# The test program
noinst_PROGRAMS = $(TESTS) $(BROKEN_TESTS)	\
	$(NULL)

# ordered alphanumerically
test_client_add_timezone_LDADD=$(TEST_LIBS)
test_client_add_timezone_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_create_object_LDADD=$(TEST_LIBS)
test_client_create_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_attachment_uris_LDADD=$(TEST_LIBS)
test_client_get_attachment_uris_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_free_busy_LDADD=$(TEST_LIBS)
test_client_get_free_busy_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_object_list_LDADD=$(TEST_LIBS)
test_client_get_object_list_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_view_LDADD=$(TEST_LIBS)
test_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_revision_view_LDADD=$(TEST_LIBS)
test_client_revision_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_modify_object_LDADD=$(TEST_LIBS)
test_client_modify_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_revision_LDADD=$(TEST_LIBS)
test_client_get_revision_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_receive_objects_LDADD=$(TEST_LIBS)
test_client_receive_objects_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_bulk_methods_LDADD=$(TEST_LIBS)
test_client_bulk_methods_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_refresh_LDADD=$(TEST_LIBS)
test_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_object_LDADD=$(TEST_LIBS)
test_client_remove_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_send_objects_LDADD=$(TEST_LIBS)
test_client_send_objects_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

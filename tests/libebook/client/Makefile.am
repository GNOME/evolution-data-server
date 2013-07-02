noinst_LTLIBRARIES = libclient-test-utils.la

libclient_test_utils_la_SOURCES = client-test-utils.c client-test-utils.h

libclient_test_utils_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook				\
	-I$(top_builddir)/addressbook				\
	-I$(top_srcdir)/calendar     				\
	-I$(top_builddir)/calendar   				\
	-I$(top_srcdir)/tests/test-server-utils     		\
	-I$(top_builddir)/tests/test-server-utils   		\
	-DSRCDIR=\""$(abs_srcdir)"\"				\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)				\
	$(CAMEL_CFLAGS)						\
	$(NULL)

libclient_test_utils_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(CAMEL_LIBS)						\
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS =								\
	test-client-refresh					\
	test-client-add-contact					\
	test-client-get-contact					\
	test-client-get-contact-uids				\
	test-client-modify-contact				\
	test-client-remove-contact				\
	test-client-remove-contact-by-uid			\
	test-client-remove-contacts				\
	test-client-add-and-get-sync				\
	test-client-add-and-get-async				\
	test-client-self					\
	test-client-preserve-uid				\
	test-client-photo-is-uri				\
	test-client-e164-param					\
        test-client-custom-summary				\
	test-client-get-revision				\
	test-client-write-write					\
	test-client-get-view					\
	test-client-uid-only-view				\
	test-client-revision-view				\
	test-client-view-operations		 		\
	test-client-suppress-notifications			\
	test-client-cursor-create				\
	$(NULL)

# The noinst tests are functional tests, not unit tests.
#
# Either they were designed as functional tests or have
# not been ported to use ETestServerFixture yet.
noinst_PROGRAMS =						\
	$(TESTS)						\
	$(NULL)

TEST_CPPFLAGS =							\
	$(libclient_test_utils_la_CPPFLAGS)			\
	$(EVOLUTION_ADDRESSBOOK_CPPFLAGS)			\
	$(NULL)

TEST_LIBS =							\
	$(libclient_test_utils_la_LIBADD)			\
	libclient-test-utils.la					\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(NULL)

test_client_add_and_get_sync_LDADD=$(TEST_LIBS)
test_client_add_and_get_sync_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_add_and_get_async_LDADD=$(TEST_LIBS)
test_client_add_and_get_async_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_add_contact_LDADD=$(TEST_LIBS)
test_client_add_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_preserve_uid_LDADD=$(TEST_LIBS)
test_client_preserve_uid_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_contact_LDADD=$(TEST_LIBS)
test_client_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_contact_uids_LDADD=$(TEST_LIBS)
test_client_get_contact_uids_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_e164_param_LDADD=$(TEST_LIBS)
test_client_e164_param_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_custom_summary_LDADD=$(TEST_LIBS)
test_client_custom_summary_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_revision_LDADD=$(TEST_LIBS)
test_client_get_revision_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_view_LDADD=$(TEST_LIBS)
test_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_uid_only_view_LDADD=$(TEST_LIBS)
test_client_uid_only_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_revision_view_LDADD=$(TEST_LIBS)
test_client_revision_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_view_operations_LDADD=$(TEST_LIBS)
test_client_view_operations_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_suppress_notifications_LDADD=$(TEST_LIBS)
test_client_suppress_notifications_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_modify_contact_LDADD=$(TEST_LIBS)
test_client_modify_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_refresh_LDADD=$(TEST_LIBS)
test_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contact_LDADD=$(TEST_LIBS)
test_client_remove_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contact_by_uid_LDADD=$(TEST_LIBS)
test_client_remove_contact_by_uid_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contacts_LDADD=$(TEST_LIBS)
test_client_remove_contacts_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_photo_is_uri_LDADD=$(TEST_LIBS)
test_client_photo_is_uri_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_write_write_LDADD=$(TEST_LIBS)
test_client_write_write_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_self_LDADD=$(TEST_LIBS)
test_client_self_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_cursor_create_LDADD=$(TEST_LIBS)
test_client_cursor_create_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

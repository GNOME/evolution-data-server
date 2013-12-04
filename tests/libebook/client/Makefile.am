noinst_LTLIBRARIES = libclient-test-utils.la

libclient_test_utils_la_SOURCES = \
	client-test-utils.c \
	client-test-utils.h \
	$(NULL)

libclient_test_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/test-server-utils \
	-I$(top_builddir)/tests/test-server-utils \
	-I$(top_srcdir)/private \
	-I$(top_builddir)/private \
	-DSRCDIR=\""$(abs_srcdir)"\" \
	-DINSTALLED_TEST_DIR=\""$(pkglibexecdir)/installed-tests"\" \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

libclient_test_utils_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(top_builddir)/private/libedbus-private.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS = \
	test-book-client-refresh \
	test-book-client-add-contact \
	test-book-client-get-contact \
	test-book-client-get-contact-uids \
	test-book-client-modify-contact \
	test-book-client-remove-contact \
	test-book-client-remove-contact-by-uid \
	test-book-client-remove-contacts \
	test-book-client-add-and-get-sync \
	test-book-client-add-and-get-async \
	test-book-client-self \
	test-book-client-preserve-uid \
	test-book-client-photo-is-uri \
	test-book-client-e164-param \
        test-book-client-custom-summary \
	test-book-client-get-revision \
	test-book-client-write-write \
	test-book-client-get-view \
	test-book-client-uid-only-view \
	test-book-client-revision-view \
	test-book-client-view-operations \
	test-book-client-suppress-notifications \
	test-book-client-cursor-create \
	test-book-client-cursor-operations \
	$(NULL)

# The noinst tests are functional tests, not unit tests.
#
# Either they were designed as functional tests or have
# not been ported to use ETestServerFixture yet.
noinst_PROGRAMS = $(TESTS)

TEST_CPPFLAGS = \
	$(libclient_test_utils_la_CPPFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CPPFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(libclient_test_utils_la_LIBADD) \
	libclient-test-utils.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(NULL)

test_book_client_add_and_get_sync_LDADD=$(TEST_LIBS)
test_book_client_add_and_get_sync_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_add_and_get_async_LDADD=$(TEST_LIBS)
test_book_client_add_and_get_async_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_add_contact_LDADD=$(TEST_LIBS)
test_book_client_add_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_preserve_uid_LDADD=$(TEST_LIBS)
test_book_client_preserve_uid_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_get_contact_LDADD=$(TEST_LIBS)
test_book_client_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_get_contact_uids_LDADD=$(TEST_LIBS)
test_book_client_get_contact_uids_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_e164_param_LDADD=$(TEST_LIBS)
test_book_client_e164_param_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_custom_summary_LDADD=$(TEST_LIBS)
test_book_client_custom_summary_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_get_revision_LDADD=$(TEST_LIBS)
test_book_client_get_revision_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_get_view_LDADD=$(TEST_LIBS)
test_book_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_uid_only_view_LDADD=$(TEST_LIBS)
test_book_client_uid_only_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_revision_view_LDADD=$(TEST_LIBS)
test_book_client_revision_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_view_operations_LDADD=$(TEST_LIBS)
test_book_client_view_operations_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_suppress_notifications_LDADD=$(TEST_LIBS)
test_book_client_suppress_notifications_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_modify_contact_LDADD=$(TEST_LIBS)
test_book_client_modify_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_refresh_LDADD=$(TEST_LIBS)
test_book_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_remove_contact_LDADD=$(TEST_LIBS)
test_book_client_remove_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_remove_contact_by_uid_LDADD=$(TEST_LIBS)
test_book_client_remove_contact_by_uid_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_remove_contacts_LDADD=$(TEST_LIBS)
test_book_client_remove_contacts_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_photo_is_uri_LDADD=$(TEST_LIBS)
test_book_client_photo_is_uri_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_write_write_LDADD=$(TEST_LIBS)
test_book_client_write_write_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_self_LDADD=$(TEST_LIBS)
test_book_client_self_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_cursor_create_LDADD=$(TEST_LIBS)
test_book_client_cursor_create_CPPFLAGS=$(TEST_CPPFLAGS)
test_book_client_cursor_operations_LDADD=$(TEST_LIBS)
test_book_client_cursor_operations_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@

# Here we list all the $(TESTS) except
# for test-book-client-cursor-operations, that
# test requires that we set the locale
# on the system bus, or set EDS_TESTING=1
# both in the test environment AND the D-Bus
# session environment.
#
# test-book-client-self also fails for some reason
# when running installed, not sure why.
INSTALLED_TESTS = \
	test-book-client-refresh \
	test-book-client-add-contact \
	test-book-client-get-contact \
	test-book-client-get-contact-uids \
	test-book-client-modify-contact \
	test-book-client-remove-contact \
	test-book-client-remove-contact-by-uid \
	test-book-client-remove-contacts \
	test-book-client-add-and-get-sync \
	test-book-client-add-and-get-async \
	test-book-client-preserve-uid \
	test-book-client-photo-is-uri \
	test-book-client-e164-param \
        test-book-client-custom-summary \
	test-book-client-get-revision \
	test-book-client-write-write \
	test-book-client-get-view \
	test-book-client-uid-only-view \
	test-book-client-revision-view \
	test-book-client-view-operations \
	test-book-client-suppress-notifications \
	test-book-client-cursor-create \
	$(NULL)

INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

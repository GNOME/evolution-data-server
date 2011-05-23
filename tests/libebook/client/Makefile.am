noinst_LTLIBRARIES = libclient-test-utils.la

libclient_test_utils_la_SOURCES = client-test-utils.c client-test-utils.h

libclient_test_utils_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook				\
	-I$(top_builddir)/addressbook				\
	-DSRCDIR=\""$(srcdir)"\"				\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)				\
	$(NULL)

libclient_test_utils_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS =								\
	test-client-remove					\
	test-client-examine					\
	test-client-refresh					\
	test-client-add-contact					\
	test-client-get-contact					\
	test-client-get-view					\
	test-client-modify-contact				\
	test-client-remove-contact				\
	test-client-remove-contact-by-uid			\
	test-client-remove-contacts				\
	test-client-stress-factory--serial			\
	test-client-stress-factory--fifo			\
	test-client-stress-factory--single-book			\
	$(NULL)

noinst_PROGRAMS =						\
	$(TESTS)						\
	test-client						\
	test-client-async					\
	test-client-nonexistent-id				\
	test-client-search					\
	test-client-self					\
	test-client-stress-views				\
	$(NULL)

TEST_CPPFLAGS =							\
	$(libclient_test_utils_la_CPPFLAGS)			\
	$(EVOLUTION_ADDRESSBOOK_CPPFLAGS)			\
	$(NULL)

TEST_LIBS =							\
	$(libclient_test_utils_la_LIBS)				\
	libclient-test-utils.la					\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(NULL)

test_client_LDADD=$(TEST_LIBS)
test_client_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_async_LDADD=$(TEST_LIBS)
test_client_async_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_add_contact_LDADD=$(TEST_LIBS)
test_client_add_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_examine_LDADD=$(TEST_LIBS)
test_client_examine_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_contact_LDADD=$(TEST_LIBS)
test_client_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_view_LDADD=$(TEST_LIBS)
test_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_modify_contact_LDADD=$(TEST_LIBS)
test_client_modify_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_refresh_LDADD=$(TEST_LIBS)
test_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_LDADD=$(TEST_LIBS)
test_client_remove_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contact_LDADD=$(TEST_LIBS)
test_client_remove_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contact_by_uid_LDADD=$(TEST_LIBS)
test_client_remove_contact_by_uid_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_contacts_LDADD=$(TEST_LIBS)
test_client_remove_contacts_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__fifo_LDADD=$(TEST_LIBS)
test_client_stress_factory__fifo_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__serial_LDADD=$(TEST_LIBS)
test_client_stress_factory__serial_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__single_book_LDADD=$(TEST_LIBS)
test_client_stress_factory__single_book_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_nonexistent_id_LDADD=$(TEST_LIBS)
test_client_nonexistent_id_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_search_LDADD=$(TEST_LIBS)
test_client_search_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_self_LDADD=$(TEST_LIBS)
test_client_self_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_views_LDADD=$(TEST_LIBS)
test_client_stress_views_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

noinst_LTLIBRARIES = libebook-test-utils.la
libebook_test_utils_la_SOURCES = ebook-test-utils.c ebook-test-utils.h

# FIXME: reduct redundancy
libebook_test_utils_la_CPPFLAGS = \
        $(AM_CPPFLAGS)                  \
        -I$(top_srcdir)                 \
        -I$(top_builddir)               \
        -I$(top_srcdir)/addressbook     \
        -I$(top_builddir)/addressbook   \
        $(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(NULL)

libebook_test_utils_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)                           \
	$(NULL)

TEST_LIBS =							\
	$(libebook_test_utils_la_LIBADD) \
	libebook-test-utils.la	         \
	$(NULL)

noinst_PROGRAMS = \
	test-changes                         \
	test-categories                      \
	test-date                            \
	test-ebook                           \
	test-ebook-async                     \
	test-ebook-add-contact               \
	test-ebook-get-contact               \
	test-ebook-remove                    \
	test-ebook-remove-contact            \
	test-ebook-remove-contact-by-id      \
	test-ebook-remove-contacts           \
	test-ebook-view                      \
	test-nonexistent-id                  \
	test-photo                           \
	test-query                           \
	test-self                            \
	test-string                          \
	test-undefinedfield                  \
	test-untyped-phones                  \
	test-search                          \
	test-stress-bookviews                \
	$(NULL)

TEST_CPPFLAGS= \
	$(libebook_test_utils_la_CPPFLAGS) \
	$(NULL)

test_search_LDADD=$(TEST_LIBS)
test_search_CPPFLAGS=$(TEST_CPPFLAGS)
test_date_LDADD=$(TEST_LIBS)
test_date_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_LDADD=$(TEST_LIBS)
test_ebook_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_async_LDADD=$(TEST_LIBS)
test_ebook_async_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_add_contact_LDADD=$(TEST_LIBS)
test_ebook_add_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_contact_LDADD=$(TEST_LIBS)
test_ebook_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_LDADD=$(TEST_LIBS)
test_ebook_remove_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contact_LDADD=$(TEST_LIBS)
test_ebook_remove_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contact_by_id_LDADD=$(TEST_LIBS)
test_ebook_remove_contact_by_id_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contacts_LDADD=$(TEST_LIBS)
test_ebook_remove_contacts_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_view_LDADD=$(TEST_LIBS)
test_ebook_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_changes_LDADD=$(TEST_LIBS)
test_changes_CPPFLAGS=$(TEST_CPPFLAGS)
test_categories_LDADD=$(TEST_LIBS)
test_categories_CPPFLAGS=$(TEST_CPPFLAGS)
test_nonexistent_id_LDADD=$(TEST_LIBS)
test_nonexistent_id_CPPFLAGS=$(TEST_CPPFLAGS)
test_photo_LDADD=$(TEST_LIBS)
test_photo_CPPFLAGS=$(TEST_CPPFLAGS)
test_query_LDADD=$(TEST_LIBS)
test_query_CPPFLAGS=$(TEST_CPPFLAGS)
test_self_LDADD=$(TEST_LIBS)
test_self_CPPFLAGS=$(TEST_CPPFLAGS)
test_string_LDADD=$(TEST_LIBS)
test_string_CPPFLAGS=$(TEST_CPPFLAGS)
test_undefinedfield_LDADD=$(TEST_LIBS)
test_undefinedfield_CPPFLAGS=$(TEST_CPPFLAGS)
test_untyped_phones_LDADD=$(TEST_LIBS)
test_untyped_phones_CPPFLAGS=$(TEST_CPPFLAGS)
test_stress_bookviews_LDADD=$(TEST_LIBS)
test_stress_bookviews_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

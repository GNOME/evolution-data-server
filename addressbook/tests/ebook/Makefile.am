INCLUDES = 				\
	-I$(top_srcdir)/addressbook	\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

TEST_LIBS=						\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

noinst_PROGRAMS= test-changes test-categories test-date test-ebook test-ebook-async test-ebook-view test-photo test-self test-string test-undefinedfield test-untyped-phones

test_date_LDADD=$(TEST_LIBS)
test_ebook_LDADD=$(TEST_LIBS)
test_ebook_async_LDADD=$(TEST_LIBS)
test_ebook_view_LDADD=$(TEST_LIBS)
test_changes_LDADD=$(TEST_LIBS)
test_categories_LDADD=$(TEST_LIBS)
test_photo_LDADD=$(TEST_LIBS)
test_self_LDADD=$(TEST_LIBS)
test_string_LDADD=$(TEST_LIBS)
test_undefinedfield_LDADD=$(TEST_LIBS)
test_untyped_phones_LDADD=$(TEST_LIBS)

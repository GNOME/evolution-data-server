SUBDIRS = vcard client

@GNOME_CODE_COVERAGE_RULES@

### EBook Tests (deprecated) ###

noinst_LTLIBRARIES = libebook-test-utils.la
libebook_test_utils_la_SOURCES = \
	ebook-test-utils.c \
	ebook-test-utils.h \
	$(NULL)

libebook_test_utils_la_CPPFLAGS = \
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
	-DINSTALLED_TEST_DIR=\""$(pkglibexecdir)/installed-tests"\" \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

libebook_test_utils_la_LIBADD = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

TEST_CPPFLAGS = \
	$(libebook_test_utils_la_CPPFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(libebook_test_utils_la_LIBADD) \
	libebook-test-utils.la \
	$(NULL)

### EBookClient Tests ###

# Should be kept ordered approximately from least to most difficult/complex
TESTS = \
	test-ebook-add-contact \
	test-ebook-get-contact \
	test-ebook-commit-contact \
	test-ebook-remove-contact \
	test-ebook-get-required-fields \
	test-ebook-get-static-capabilities \
	test-ebook-get-supported-fields \
	test-ebook-get-supported-auth-methods \
	test-ebook-remove-contact-by-id \
	test-ebook-remove-contacts \
	test-ebook-get-book-view \
	$(NULL)

noinst_PROGRAMS = $(TESTS)

TEST_VCARDS = \
	$(srcdir)/data/vcards/name-only.vcf \
	$(srcdir)/data/vcards/simple-1.vcf \
	$(srcdir)/data/vcards/simple-2.vcf \
	$(srcdir)/data/vcards/sorted-1.vcf \
	$(srcdir)/data/vcards/sorted-2.vcf \
	$(srcdir)/data/vcards/sorted-3.vcf \
	$(srcdir)/data/vcards/sorted-4.vcf \
	$(srcdir)/data/vcards/sorted-5.vcf \
	$(srcdir)/data/vcards/sorted-6.vcf \
	$(srcdir)/data/vcards/sorted-7.vcf \
	$(srcdir)/data/vcards/sorted-8.vcf \
	$(srcdir)/data/vcards/sorted-9.vcf \
	$(srcdir)/data/vcards/sorted-10.vcf \
	$(srcdir)/data/vcards/sorted-11.vcf \
	$(srcdir)/data/vcards/sorted-12.vcf \
	$(srcdir)/data/vcards/sorted-13.vcf \
	$(srcdir)/data/vcards/sorted-14.vcf \
	$(srcdir)/data/vcards/sorted-15.vcf \
	$(srcdir)/data/vcards/sorted-16.vcf \
	$(srcdir)/data/vcards/sorted-17.vcf \
	$(srcdir)/data/vcards/sorted-18.vcf \
	$(srcdir)/data/vcards/sorted-19.vcf \
	$(srcdir)/data/vcards/sorted-20.vcf \
	$(srcdir)/data/vcards/custom-1.vcf \
	$(srcdir)/data/vcards/custom-2.vcf \
	$(srcdir)/data/vcards/custom-3.vcf \
	$(srcdir)/data/vcards/custom-4.vcf \
	$(srcdir)/data/vcards/custom-5.vcf \
	$(srcdir)/data/vcards/custom-6.vcf \
	$(srcdir)/data/vcards/custom-7.vcf \
	$(srcdir)/data/vcards/custom-8.vcf \
	$(srcdir)/data/vcards/custom-9.vcf \
	$(srcdir)/data/vcards/custom-10.vcf \
	$(srcdir)/data/vcards/custom-11.vcf \
	$(srcdir)/data/vcards/custom-12.vcf \
	$(srcdir)/data/vcards/custom-13.vcf \
	$(srcdir)/data/vcards/custom-14.vcf \
	$(srcdir)/data/vcards/custom-15.vcf \
	$(NULL)

if EDS_INSTALLED_TESTS_ENABLED
vcardsdir   = ${pkglibexecdir}/installed-tests/vcards
vcards_DATA = $(TEST_VCARDS)
endif

EXTRA_DIST = $(TEST_VCARDS)



test_ebook_commit_contact_LDADD=$(TEST_LIBS)
test_ebook_commit_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_book_view_LDADD=$(TEST_LIBS)
test_ebook_get_book_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_contact_LDADD=$(TEST_LIBS)
test_ebook_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_required_fields_LDADD=$(TEST_LIBS)
test_ebook_get_required_fields_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_static_capabilities_LDADD=$(TEST_LIBS)
test_ebook_get_static_capabilities_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_supported_auth_methods_LDADD=$(TEST_LIBS)
test_ebook_get_supported_auth_methods_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_get_supported_fields_LDADD=$(TEST_LIBS)
test_ebook_get_supported_fields_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_add_contact_LDADD=$(TEST_LIBS)
test_ebook_add_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contact_LDADD=$(TEST_LIBS)
test_ebook_remove_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contact_by_id_LDADD=$(TEST_LIBS)
test_ebook_remove_contact_by_id_CPPFLAGS=$(TEST_CPPFLAGS)
test_ebook_remove_contacts_LDADD=$(TEST_LIBS)
test_ebook_remove_contacts_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

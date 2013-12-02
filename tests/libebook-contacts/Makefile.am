
@GNOME_CODE_COVERAGE_RULES@

NULL =

### EBook Tests (deprecated) ###

TEST_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-DSRCDIR=\""$(abs_srcdir)"\" \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS = \
	test-contact-types \
	test-vcard-parsing \
	test-untyped-phones \
	test-query \
	test-phone-number \
	$(NULL)

# test-bulk-methods

test_contact_types_LDADD=$(TEST_LIBS)
test_contact_types_CPPFLAGS=$(TEST_CPPFLAGS)
test_vcard_parsing_LDADD=$(TEST_LIBS)
test_vcard_parsing_CPPFLAGS=$(TEST_CPPFLAGS)
test_untyped_phones_LDADD=$(TEST_LIBS)
test_untyped_phones_CPPFLAGS=$(TEST_CPPFLAGS)
test_query_LDADD=$(TEST_LIBS)
test_query_CPPFLAGS=$(TEST_CPPFLAGS)
test_phone_number_LDADD=$(TEST_LIBS)
test_phone_number_CPPFLAGS=$(TEST_CPPFLAGS)

noinst_PROGRAMS = $(TESTS)

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

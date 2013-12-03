
noinst_LTLIBRARIES = libdata-test-utils.la

libdata_test_utils_la_SOURCES = \
	data-test-utils.c \
	data-test-utils.h \
	$(NULL)

libdata_test_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/libedataserver \
	-I$(top_builddir)/tests/libedataserver \
	-I$(top_srcdir)/tests/test-server-utils \
	-I$(top_builddir)/tests/test-server-utils \
	-I$(top_srcdir)/private \
	-I$(top_builddir)/private \
	-DBACKENDDIR=\"$(ebook_backenddir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\""$(abs_srcdir)"\" \
	-DBUILDDIR=\""$(abs_topbuilddir)"\" \
	-DINSTALLED_TEST_DIR=\""$(pkglibexecdir)/installed-tests"\" \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

libdata_test_utils_la_LIBADD = \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(top_builddir)/private/libedbus-private.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
#
# Note that these tests must be run in order:
#   test-sqlite-cursor-posix-initial,
#   test-sqlite-cursor-en-US-migrated,
#   test-sqlite-cursor-posix-migrated
#
# This is because each migrated test changes the
# locale and reloads the same addressbook of the previous test. 
TESTS = \
	test-sqlite-get-contact \
	test-sqlite-create-cursor \
	test-sqlite-cursor-move-by-posix \
	test-sqlite-cursor-move-by-en-US \
	test-sqlite-cursor-move-by-fr-CA \
	test-sqlite-cursor-move-by-de-DE \
	test-sqlite-cursor-set-target \
	test-sqlite-cursor-calculate \
	test-sqlite-cursor-set-sexp \
	test-sqlite-cursor-change-locale

noinst_PROGRAMS = $(TESTS)

TEST_CPPFLAGS = \
	$(libdata_test_utils_la_CPPFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CPPFLAGS) \
	$(NULL)

TEST_LIBS = \
	$(libdata_test_utils_la_LIBADD) \
	libdata-test-utils.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(NULL)

test_sqlite_get_contact_LDADD=$(TEST_LIBS)
test_sqlite_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_create_cursor_LDADD=$(TEST_LIBS)
test_sqlite_create_cursor_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_move_by_posix_LDADD=$(TEST_LIBS)
test_sqlite_cursor_move_by_posix_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_move_by_en_US_LDADD=$(TEST_LIBS)
test_sqlite_cursor_move_by_en_US_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_move_by_fr_CA_LDADD=$(TEST_LIBS)
test_sqlite_cursor_move_by_fr_CA_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_move_by_de_DE_LDADD=$(TEST_LIBS)
test_sqlite_cursor_move_by_de_DE_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_set_target_LDADD=$(TEST_LIBS)
test_sqlite_cursor_set_target_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_calculate_LDADD=$(TEST_LIBS)
test_sqlite_cursor_calculate_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_set_sexp_LDADD=$(TEST_LIBS)
test_sqlite_cursor_set_sexp_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_change_locale_LDADD=$(TEST_LIBS)
test_sqlite_cursor_change_locale_CPPFLAGS=$(TEST_CPPFLAGS)

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

-include $(top_srcdir)/git.mk

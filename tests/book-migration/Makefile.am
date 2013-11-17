SUBDIRS = db

noinst_PROGRAMS = \
	setup-migration-test \
	$(TESTS) \
	$(NULL)

TEST_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/test-server-utils \
	-I$(top_builddir)/tests/test-server-utils \
	-I$(top_srcdir)/tests/libebook/client \
	-I$(top_builddir)/tests/libebook/client \
	-I$(top_srcdir)/private \
	-I$(top_builddir)/private \
	-DEDS_TEST_WORK_DIR=\""$(abs_top_builddir)/tests/test-server-utils/cache"\" \
	-DEDS_TEST_SQLITE_BOOKS=\""$(abs_top_srcdir)/tests/book-migration/db"\" \
	-DEDS_TEST_BUILT_BOOKS=\""$(abs_top_builddir)/tests/book-migration/db"\" \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

# If db_load is detected at configure time, then we've built
# an addressbook.db to test the migration from 3.6 -> Current.
#
# Instead of committing a BDB file directly which might have
# compatibility issues, we use a text dump (for SQLite we
# rely on compatilbility).
if HAVE_DB_LOAD
TEST_CPPFLAGS += -DTEST_VERSIONS_WITH_BDB
endif

TEST_LIBS = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(top_builddir)/tests/libebook/client/libclient-test-utils.la \
	$(top_builddir)/private/libedbus-private.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS = \
	test-migration \
	$(NULL)

EXTRA_DIST = \
	$(srcdir)/vcards/migrate-1.vcf \
	$(srcdir)/vcards/migrate-2.vcf \
	$(srcdir)/vcards/migrate-3.vcf \
	$(srcdir)/vcards/migrate-4.vcf \
	$(srcdir)/vcards/migrate-5.vcf \
	$(srcdir)/vcards/migrate-6.vcf \
	$(srcdir)/vcards/migrate-7.vcf \
	$(srcdir)/vcards/migrate-8.vcf \
	$(srcdir)/vcards/migrate-9.vcf \
	$(srcdir)/vcards/migrate-10.vcf \
	$(srcdir)/vcards/migrate-11.vcf \
	$(srcdir)/vcards/migrate-12.vcf \
	$(srcdir)/vcards/migrate-13.vcf \
	$(srcdir)/vcards/migrate-14.vcf \
	$(srcdir)/vcards/migrate-15.vcf \
	$(srcdir)/vcards/migrate-16.vcf \
	$(srcdir)/vcards/migrate-17.vcf \
	$(srcdir)/vcards/migrate-18.vcf \
	$(srcdir)/vcards/migrate-19.vcf \
	$(srcdir)/vcards/migrate-20.vcf \
	$(NULL)

# This rule should be run once every stable release and then the
# newly created 'contacts.db' file added to git.
#
# For instance, when the EDS version is 3.12, the file:
#   $(top_srcdir)/tests/book-migration/db/3.12/contacts.db
# will be created as a result of running 'make setup-migration'.
#
# Note that the 'setup-migration-test' program can be compiled
# with EDS versions back to 3.0. If you really need to rebuild
# the older test sandboxes, then setup-migration-test can be
# manually compiled and used with older builds of EDS.
setup-migration: setup-migration-test
	@echo -n "Setting up new migration sandbox in $(top_srcdir)/tests/book-migration/db/$(EDS_MAJOR_VERSION).$(EDS_MINOR_VERSION)... " && \
	$(top_builddir)/tests/book-migration/setup-migration-test \
	--use-test-sandbox --book-id $(EDS_MAJOR_VERSION).$(EDS_MINOR_VERSION) \
	--contacts-directory $(top_srcdir)/tests/book-migration/vcards && \
	mkdir -p $(top_srcdir)/tests/book-migration/db/$(EDS_MAJOR_VERSION).$(EDS_MINOR_VERSION) && \
	cp $(top_builddir)/tests/test-server-utils/cache/evolution/addressbook/$(EDS_MAJOR_VERSION).$(EDS_MINOR_VERSION)/contacts.db \
	$(top_srcdir)/tests/book-migration/db/$(EDS_MAJOR_VERSION).$(EDS_MINOR_VERSION) && echo "Done."

test_migration_LDADD=$(TEST_LIBS)
test_migration_CPPFLAGS=$(TEST_CPPFLAGS)

setup_migration_test_LDADD=$(TEST_LIBS)
setup_migration_test_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

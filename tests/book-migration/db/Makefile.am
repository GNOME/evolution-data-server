
EXTRA_DIST = \
	3.0/addressbook.dump 3.0/addressbook.db.summary \
	3.2/addressbook.dump 3.2/contacts.db \
	3.4/addressbook.dump 3.4/contacts.db \
	3.6/addressbook.dump 3.6/contacts.db \
	3.8/contacts.db \
	3.10/contacts.db \
	3.12/contacts.db \
	$(NULL)

# Use db_load with our BDB dump text file
if HAVE_DB_LOAD

GENERATED_DBS = \
	$(top_builddir)/tests/book-migration/db/3.0/addressbook.db \
	$(top_builddir)/tests/book-migration/db/3.2/addressbook.db \
	$(top_builddir)/tests/book-migration/db/3.4/addressbook.db \
	$(top_builddir)/tests/book-migration/db/3.6/addressbook.db \
	$(NULL)

noinst_DATA = $(GENERATED_DBS)
CLEANFILES = $(GENERATED_DBS)

$(top_builddir)/tests/book-migration/db/3.0/addressbook.db: $(top_srcdir)/tests/book-migration/db/3.0/addressbook.dump
	$(AM_V_GEN) mkdir -p $(top_builddir)/tests/book-migration/db/3.0 && $(DB_LOAD) -f $< $@

$(top_builddir)/tests/book-migration/db/3.2/addressbook.db: $(top_srcdir)/tests/book-migration/db/3.2/addressbook.dump
	$(AM_V_GEN) mkdir -p $(top_builddir)/tests/book-migration/db/3.2 && $(DB_LOAD) -f $< $@

$(top_builddir)/tests/book-migration/db/3.4/addressbook.db: $(top_srcdir)/tests/book-migration/db/3.4/addressbook.dump
	$(AM_V_GEN) mkdir -p $(top_builddir)/tests/book-migration/db/3.4 && $(DB_LOAD) -f $< $@

$(top_builddir)/tests/book-migration/db/3.6/addressbook.db: $(top_srcdir)/tests/book-migration/db/3.6/addressbook.dump
	$(AM_V_GEN) mkdir -p $(top_builddir)/tests/book-migration/db/3.6 && $(DB_LOAD) -f $< $@

endif

INCLUDES = 				\
	-I$(top_srcdir)/addressbook	\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_PROGRAMS= test-changes test-date test-ebook test-photo test-string

test_date_LDADD = 					\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

test_ebook_LDADD = 					\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

test_changes_LDADD = 					\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

test_photo_LDADD = 					\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

test_string_LDADD = 					\
	$(top_builddir)/addressbook/libebook/libebook.la\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

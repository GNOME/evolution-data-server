noinst_LTLIBRARIES = libdata-test-utils.la

libdata_test_utils_la_SOURCES =					\
	data-test-utils.c					\
	data-test-utils.h					\
	$(NULL)

libdata_test_utils_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook				\
	-I$(top_builddir)/addressbook				\
	-I$(top_srcdir)/calendar     				\
	-I$(top_builddir)/calendar   				\
	-I$(top_srcdir)/tests/libedataserver     		\
	-I$(top_builddir)/tests/libedataserver   		\
	-I$(top_srcdir)/tests/test-server-utils     		\
	-I$(top_builddir)/tests/test-server-utils   		\
	-DBACKENDDIR=\"$(ebook_backenddir)\"			\
	-DDATADIR=\"$(datadir)\"				\
	-DSRCDIR=\""$(abs_srcdir)"\"				\
	-DBUILDDIR=\""$(abs_topbuilddir)"\"			\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)				\
	$(CAMEL_CFLAGS)						\
	$(NULL)

libdata_test_utils_la_LIBADD = 				\
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la		\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la	\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(CAMEL_LIBS)						\
	$(NULL)

# Should be kept ordered approximately from least to most difficult/complex
TESTS =					\
	test-sqlite-get-contact		\
	test-sqlite-create-cursor	\
	test-sqlite-cursor-move-by	\
	test-sqlite-cursor-set-target	\
	test-sqlite-cursor-calculate	\
	test-sqlite-cursor-set-sexp

noinst_PROGRAMS = $(TESTS)

TEST_CPPFLAGS =							\
	$(libdata_test_utils_la_CPPFLAGS)			\
	$(EVOLUTION_ADDRESSBOOK_CPPFLAGS)			\
	$(NULL)

TEST_LIBS =							\
	$(libdata_test_utils_la_LIBADD)			\
	libdata-test-utils.la					\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(NULL)

test_sqlite_get_contact_LDADD=$(TEST_LIBS)
test_sqlite_get_contact_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_create_cursor_LDADD=$(TEST_LIBS)
test_sqlite_create_cursor_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_move_by_LDADD=$(TEST_LIBS)
test_sqlite_cursor_move_by_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_set_target_LDADD=$(TEST_LIBS)
test_sqlite_cursor_set_target_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_calculate_LDADD=$(TEST_LIBS)
test_sqlite_cursor_calculate_CPPFLAGS=$(TEST_CPPFLAGS)
test_sqlite_cursor_set_sexp_LDADD=$(TEST_LIBS)
test_sqlite_cursor_set_sexp_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk

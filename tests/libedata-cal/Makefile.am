NULL =

@GNOME_CODE_COVERAGE_RULES@

TESTS = \
	test-e-sexp \
	test-intervaltree \
	$(NULL)

noinst_PROGRAMS = $(TESTS)

test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

test_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(CAMEL_LIBS) \
	$(NULL)

test_e_sexp_SOURCES = \
	test-cal-backend-sexp.c \
	$(NULL)

test_intervaltree_SOURCES = \
	test-intervaltree.c \
	$(NULL)

test_e_sexp_CPPFLAGS = $(test_CPPFLAGS)
test_e_sexp_LDADD = $(test_LDADD)

test_intervaltree_CPPFLAGS = $(test_CPPFLAGS)
test_intervaltree_LDADD = $(test_LDADD)

-include $(top_srcdir)/git.mk

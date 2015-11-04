NULL =

@GNOME_CODE_COVERAGE_RULES@

TESTS_SKIP = \
	e-collator-test \
	$(NULL)

TESTS = \
	e-source-registry-test \
	$(NULL)

noinst_PROGRAMS = $(TESTS) $(TESTS_SKIP)

test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/calendar \
	-I$(top_builddir)/calendar \
	-I$(top_srcdir)/tests/test-server-utils \
	-DG_LOG_DOMAIN=\"e-data-server\" \
	-DDATADIR=\"$(datadir)\" \
	$(E_DATA_SERVER_CFLAGS) \
	$(EVOLUTION_CALENDAR_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(NULL)

test_LDADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la \
	$(top_builddir)/calendar/libecal/libecal-1.2.la \
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la \
	$(E_DATA_SERVER_LIBS) \
	$(EVOLUTION_CALENDAR_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

e_collator_test_SOURCES = \
	e-collator-test.c \
	$(NULL)

e_collator_test_CPPFLAGS = $(test_CPPFLAGS)
e_collator_test_LDADD = $(test_LDADD)

e_source_registry_test_SOURCE = \
	e-source-registry-test.c \
	$(NULL)

e_source_registry_test_CPPFLAGS = $(test_CPPFLAGS)
e_source_registry_test_LDADD = $(test_LDADD)

if HAVE_GTK
noinst_PROGRAMS += e-user-prompter-test

e_user_prompter_test_SOURCES = e-user-prompter-test.c
e_user_prompter_test_CPPFLAGS = $(test_CPPFLAGS) $(GTK_CFLAGS)
e_user_prompter_test_LDADD = $(test_LDADD) $(GTK_LIBS)
endif

-include $(top_srcdir)/git.mk

#####################################
#     Optionally Install Tests      #
#####################################
@EDS_INSTALLED_TESTS_RULE@
INSTALLED_TESTS = $(TESTS)
INSTALLED_TESTS_TYPE = session-exclusive
INSTALLED_TESTS_ENVIRONMENT = "TEST_INSTALLED_SERVICES=1"

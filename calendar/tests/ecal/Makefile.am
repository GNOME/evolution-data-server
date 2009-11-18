EXTRA_DIST =                          \
	$(test_scripts)               \
	testdata.ics

test_scripts =                         \
	test-runner.sh                 \
	cleanup.sh

# The test program
noinst_PROGRAMS = test-ecal test-recur test-search

TEST_ECAL_CPPFLAGS= \
	$(AM_CPPFLAGS)					\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	$(LIBICAL_CFLAGS)				\
	$(EVOLUTION_CALENDAR_CFLAGS)

test_ecal_SOURCES = test-ecal.c
test_ecal_CPPFLAGS = $(TEST_ECAL_CPPFLAGS)
test_ecal_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"
test_ecal_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(LIBICAL_LIBS)								\
	$(EVOLUTION_CALENDAR_LIBS)

test_recur_SOURCES = test-recur.c
test_recur_CPPFLAGS = $(TEST_ECAL_CPPFLAGS)
test_recur_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"
test_recur_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(LIBICAL_LIBS)								\
	$(EVOLUTION_CALENDAR_LIBS)

test_search_SOURCES = test-search.c
test_search_CPPFLAGS = $(TEST_ECAL_CPPFLAGS)
test_search_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"
test_search_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(LIBICAL_LIBS)								\
	$(EVOLUTION_CALENDAR_LIBS)

-include $(top_srcdir)/git.mk

INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	$(EVOLUTION_CALENDAR_CFLAGS)

EXTRA_DIST =                          \
	$(test_scripts)               \
	testdata.ics

test_scripts =                         \
	test-runner.sh                 \
	cleanup.sh

# The test program
noinst_PROGRAMS = test-ecal test-search

test_ecal_SOURCES =	\
	test-ecal.c

test_ecal_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"

test_ecal_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(top_builddir)/calendar/libical/src/libical/libical-evolution.la	\
	$(EVOLUTION_CALENDAR_LIBS)


test_search_SOURCES =	\
	test-search.c

test_search_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"

test_search_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(top_builddir)/calendar/libical/src/libical/libical-evolution.la	\
	$(EVOLUTION_CALENDAR_LIBS)

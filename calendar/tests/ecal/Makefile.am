INCLUDES =						\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	$(EVOLUTION_CALENDAR_CFLAGS)

# The test program
noinst_PROGRAMS = test-ecal

test_ecal_SOURCES =	\
	test-ecal.c

test_ecal_INCLUDES =			\
	$(INCLUDES)			\
	-DG_LOG_DOMAIN=\"test-ecal\"

test_ecal_LDADD =								\
	$(top_builddir)/calendar/libecal/libecal.la				\
	$(top_builddir)/calendar/libical/src/libical/libical-evolution.la	\
	$(EVOLUTION_CALENDAR_LIBS)

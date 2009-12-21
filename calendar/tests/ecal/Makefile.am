noinst_LTLIBRARIES = libecal-test-utils.la
libecal_test_utils_la_SOURCES = ecal-test-utils.c ecal-test-utils.h

libecal_test_utils_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	$(LIBICAL_CFLAGS)				\
	$(EVOLUTION_CALENDAR_CFLAGS)                    \
	$(NULL)

libecal_test_utils_la_LIBADD = \
        $(top_builddir)/calendar/libecal/libecal-1.2.la    \
        $(EVOLUTION_CALENDAR_LIBS)                         \
        $(NULL)

TEST_ECAL_LIBS = \
        $(libecal_test_utils_la_LIBADD) \
        libecal-test-utils.la           \
        $(NULL)

EXTRA_DIST =                          \
	$(test_scripts)               \
	testdata.ics

test_scripts =                         \
	test-runner.sh                 \
	cleanup.sh

# ordered by relative complexity
TESTS = \
        test-ecal-remove			\
        test-ecal-open				\
        test-ecal-get-alarm-email-address	\
        test-ecal-get-cal-address		\
        test-ecal-get-ldap-attribute		\
        test-ecal-get-capabilities		\
        test-ecal-get-default-object		\
        test-ecal-set-mode			\
        test-ecal-create-object			\
        test-ecal-create-object--2		\
        test-ecal-get-objects-for-uid		\
        test-ecal-remove-object			\
        $(NULL)

# The test program
noinst_PROGRAMS = $(TESTS) test-ecal test-recur test-search

TEST_ECAL_CPPFLAGS= \
	$(libecal_test_utils_la_CPPFLAGS) \
	$(NULL)

# ordered alphanumerically
test_ecal_create_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_create_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_create_object__2_LDADD=$(TEST_ECAL_LIBS)
test_ecal_create_object__2_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_alarm_email_address_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_alarm_email_address_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_cal_address_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_cal_address_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_default_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_default_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_ldap_attribute_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_ldap_attribute_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_capabilities_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_capabilities_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_get_objects_for_uid_LDADD=$(TEST_ECAL_LIBS)
test_ecal_get_objects_for_uid_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_open_LDADD=$(TEST_ECAL_LIBS)
test_ecal_open_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_remove_LDADD=$(TEST_ECAL_LIBS)
test_ecal_remove_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_remove_object_LDADD=$(TEST_ECAL_LIBS)
test_ecal_remove_object_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)
test_ecal_set_mode_LDADD=$(TEST_ECAL_LIBS)
test_ecal_set_mode_CPPFLAGS=$(TEST_ECAL_CPPFLAGS)

# monolithic tests
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

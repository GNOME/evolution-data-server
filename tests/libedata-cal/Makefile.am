noinst_PROGRAMS =							\
	test-e-sexp							\
	test-intervaltree						\
	test-intervaltree-coverage

test_e_sexp_SOURCES = test-cal-backend-sexp.c

test_e_sexp_CPPFLAGS =							\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/calendar					\
	-I$(top_builddir)/calendar					\
	$(EVOLUTION_CALENDAR_CFLAGS)					\
	$(CAMEL_CFLAGS)

test_e_sexp_LDADD =							\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/calendar/libecal/libecal-1.2.la 		\
	$(top_builddir)/libebackend/libebackend-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(E_DATA_SERVER_LIBS)						\
	$(CAMEL_LIBS)

test_intervaltree_SOURCES = test-intervaltree.c

test_intervaltree_CPPFLAGS =						\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/calendar					\
	-I$(top_builddir)/calendar					\
	$(EVOLUTION_CALENDAR_CFLAGS)					\
	$(CAMEL_CFLAGS)

test_intervaltree_LDADD =						\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/libebackend/libebackend-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(CAMEL_LIBS)

test_intervaltree_coverage_SOURCES = test-intervaltree.c

test_intervaltree_coverage_CPPFLAGS =					\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/calendar					\
	-I$(top_builddir)/calendar					\
	$(EVOLUTION_CALENDAR_CFLAGS)					\
	$(CAMEL_CFLAGS)

test_intervaltree_coverage_LDADD = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/libebackend/libebackend-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(CAMEL_LIBS)							\
	$(CODE_COVERAGE_LDFLAGS)

.PHONY: coverage
coverage: 
	mkdir -p ./coverage
	lcov --directory . --zerocounters
	./test-intervaltree-coverage
	lcov --directory . --capture --output-file ./coverage/*.info
	genhtml -o ./coverage --legend --num-spaces 2 ./coverage/*.info

clean-local:
	rm -f *.gcda *.gcno

-include $(top_srcdir)/git.mk

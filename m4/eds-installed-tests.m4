# How to use the installed tests m4
#
#   Place EDS_INSTALLED_TESTS somewhere in configure.ac
#
#   Writing your Makefile.am
#   ~~~~~~~~~~~~~~~~~~~~~~~~
#
#   Build your local unit tests in the normal way, we only
#   depend on your correctly building tests under noinst_PROGRAMS:
#
#       noinst_PROGRAMS=list of unit tests
#
#   Normally you will go ahead an assign the TESTS and TESTS_ENVIRONMENT variables
#   so that your unit tests run at `make check' time, we recommend this
#   but it's not a requirement for this macro to work.
#
#   Somewhere in your Makefile.am in this test directory, you need to declare
#   the following variables:
#
#       INSTALLED_TESTS=list of tests to install
#       INSTALLED_TESTS_TYPE=session
#       INSTALLED_TESTS_ENVIRONMENT="SOME_OPTION=foo OTHER_OPTION=bar"
#
#   First the list of tests which should be installed, followed by
#   the type of test they should be configured as. The type can
#   be 'session' or 'session-exclusive'
#
#   More information about valid types can be found here:
#      https://wiki.gnome.org/GnomeGoals/InstalledTests
#
#   The last variable is optional, but can be useful to configure
#   your test program to run in the installed environment as opposed
#   to the normal `make check' run.
#
#   Finally, place this somewhere in your Makefile.am
#
#       @EDS_INSTALLED_TESTS_RULE@
#
#   And that's it, now your unit tests will be installed along with
#   a .test metadata file into $(pkglibexecdir) if --enable-installed-tests
#   is passed to your configure script, and will be run automatically
#   by the continuous integration servers.
#
#   FIXME: Change the above link to point to real documentation, not
#   a gnome goal page which might disappear at some point.
#
# BUGS: This macro hooks into install-exec-am and install-data-am
# which are internals of Automake. This is because Automake doesnt
# consider the regular install-exec-local / install-exec-hook or
# data install components unless variables have been setup for them
# in advance.
#
# This doesnt seem to present a problem, but it is depending on
# internals of Automake instead of clear documented API.

# Place this in configure.ac to enable
# the installed tests option.
AC_DEFUN([EDS_INSTALLED_TESTS], [
AC_PREREQ([2.50])dnl
AC_REQUIRE([AM_NLS])dnl

  AC_PROG_INSTALL
  AC_PROG_MKDIR_P
  AC_PROG_LIBTOOL

  AC_ARG_ENABLE(installed-tests,
		[AC_HELP_STRING([--enable-installed-tests],
				[enable installed unit tests [default=no]])],,
  		[enable_installed_tests="no"])

  AM_CONDITIONAL([EDS_INSTALLED_TESTS_ENABLED],[test "x$enable_installed_tests" = "xyes"])
  AC_SUBST([EDS_INSTALLED_TESTS_ENABLED], [$enable_installed_tests])

  # Define the rule for makefiles
  EDS_INSTALLED_TESTS_RULE='

ifeq ($(EDS_INSTALLED_TESTS_ENABLED),yes)

install-exec-am: installed-tests-exec-hook
install-data-am: installed-tests-data-hook

META_DIRECTORY=${DESTDIR}/${datadir}/installed-tests/${PACKAGE}
EXEC_DIRECTORY=${DESTDIR}/${pkglibexecdir}/installed-tests

FINAL_TEST_ENVIRONMENT=
ifneq ($(INSTALLED_TESTS_ENVIRONMENT),)
      FINAL_TEST_ENVIRONMENT="env $(INSTALLED_TESTS_ENVIRONMENT)"
endif

installed-tests-exec-hook:
	@$(MKDIR_P) $(EXEC_DIRECTORY);
	@for test in $(INSTALLED_TESTS); do						\
	    $(LIBTOOL) --mode=install $(INSTALL) --mode=755 $$test $(EXEC_DIRECTORY);	\
	done

installed-tests-data-hook:
	@$(MKDIR_P) $(META_DIRECTORY);
	@for test in $(INSTALLED_TESTS); do							\
	    echo "Installing $$test.test to $(META_DIRECTORY)";					\
	    echo m4_escape([[Test]]) > $(META_DIRECTORY)/$$test.test;				\
	    echo "Exec=$(FINAL_TEST_ENVIRONMENT) $(pkglibexecdir)/installed-tests/$$test"	\
	                                           >> $(META_DIRECTORY)/$$test.test;		\
	    echo "Type=$(INSTALLED_TESTS_TYPE)" >> $(META_DIRECTORY)/$$test.test;		\
	done
endif
'

  # substitute @EDS_INSTALLED_TESTS_RULE@ in Makefiles
  AC_SUBST([EDS_INSTALLED_TESTS_RULE])
  m4_ifdef([_AM_SUBST_NOTMAKE], [_AM_SUBST_NOTMAKE([EDS_INSTALLED_TESTS_RULE])])
])

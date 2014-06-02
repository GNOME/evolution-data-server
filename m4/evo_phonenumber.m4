dnl EVO_PHONENUMBER_ARGS([default])
dnl
dnl Checks configure script options for requesting libphonenumber support.
dnl Adds a --with-phonenumber option to explicitly enable and disable
dnl phonenumber support, but also for pointing to libphonenumber's install
dnl prefix.
dnl
dnl Must be called before any other macro that might use the C++ compiler.
AC_DEFUN([EVO_PHONENUMBER_ARGS],[
	AC_BEFORE([$0], [AC_COMPILE_IFELSE])
	AC_BEFORE([$0], [AC_LINK_IFELSE])
	AC_BEFORE([$0], [AC_PROG_CXX])
	AC_BEFORE([$0], [AC_RUN_IFELSE])
	AC_BEFORE([$0], [LT_INIT])

	evo_phonenumber_prefix=

	AC_MSG_CHECKING([whether to enable phonenumber support])

	AC_ARG_WITH([phonenumber],
		[AS_HELP_STRING([--with-phonenumber@<:@=PREFIX@:>@],
		                [use libphonenumber at PREFIX])],
		[evo_phonenumber_prefix=$withval],
		[with_phonenumber=m4_default([$1],[check])])

	AC_MSG_RESULT([$with_phonenumber])

	AS_VAR_IF([with_phonenumber],[no],,[evo_with_cxx=yes])
])

dnl EVO_PHONENUMBER_SUPPORT
dnl
dnl Check for Google's libphonenumber. Adds a --with-phonenumber option
dnl to explicitly enable and disable phonenumber support, but also for
dnl pointing to libphonenumber's install prefix.
dnl
dnl You most probably want to place a call to EVO_PHONENUMBER_ARGS near
dnl to the top of your configure script.
AC_DEFUN([EVO_PHONENUMBER_SUPPORT],[
	AC_REQUIRE([EVO_PHONENUMBER_ARGS])

	msg_phonenumber=no

	PHONENUMBER_INCLUDES=
	PHONENUMBER_LIBS=

	AS_VAR_IF([with_phonenumber], [no],, [
		AC_LANG_PUSH(C++)

		PHONENUMBER_INCLUDES="-DI18N_PHONENUMBERS_USE_BOOST"
		PHONENUMBER_LIBS="-lphonenumber"

		AS_VAR_IF([evo_phonenumber_prefix],,,[
			PHONENUMBER_INCLUDES="-I$evo_phonenumber_prefix/include $PHONENUMBER_INCLUDES"
			PHONENUMBER_LIBS="-L$evo_phonenumber_prefix/lib $PHONENUMBER_LIBS"
		])

		evo_cxxflags_saved="$CXXFLAGS"
		CXXFLAGS="$CXXFLAGS $PHONENUMBER_INCLUDES"

		evo_libs_saved="$LIBS"
		evo_libphonenumber_usable=no

		AC_MSG_CHECKING([if libphonenumber is usable])

		for lib in boost_thread-mt boost_thread; do
			LIBS="$evo_libs_saved $PHONENUMBER_LIBS -l$lib"

			AC_LINK_IFELSE(
				[AC_LANG_PROGRAM(
					[[#include <phonenumbers/phonenumberutil.h>]],
					[[i18n::phonenumbers::PhoneNumberUtil::GetInstance();]])],
				[with_phonenumber=yes
				 evo_libphonenumber_usable=yes
				 PHONENUMBER_LIBS="$PHONENUMBER_LIBS -l$lib"
				 break])
		done

		AS_VAR_IF([evo_libphonenumber_usable], [no],
			[AS_VAR_IF(
				[with_phonenumber], [check], [with_phonenumber=no],
				[AC_MSG_ERROR([libphonenumber cannot be used. Use --with-phonenumber to specify the library prefix.])])
			])

		AS_VAR_IF([evo_phonenumber_prefix],,
		          [msg_phonenumber=$with_phonenumber],
		          [msg_phonenumber=$evo_phonenumber_prefix])

		AC_MSG_RESULT([$with_phonenumber])

		AS_VAR_IF(
			[with_phonenumber],[yes],
			[AC_MSG_CHECKING([whether ParseAndKeepRawInput() is needed])
			 AC_RUN_IFELSE(
				[AC_LANG_PROGRAM(
					[[#include <phonenumbers/phonenumberutil.h>]],
					[[namespace pn = i18n::phonenumbers;

					  pn::PhoneNumber n;

					  if (pn::PhoneNumberUtil::GetInstance ()->
						Parse("049(800)46663", "DE", &n) == pn::PhoneNumberUtil::NO_PARSING_ERROR
							&& n.has_country_code_source ()
							&& n.country_code_source () == 49)
						return EXIT_SUCCESS;

					  return EXIT_FAILURE;]]
					)],

				[AC_MSG_RESULT([no])],
				[AC_MSG_RESULT([yes])

				 AC_DEFINE_UNQUOTED(
					[PHONENUMBER_RAW_INPUT_NEEDED], 1,
					[Whether Parse() or ParseAndKeepRawInput() must be used to get the country-code source])
				])
			])


		CXXFLAGS="$evo_cxxflags_saved"
		LIBS="$evo_libs_saved"

		AC_LANG_POP(C++)
	])

	AM_CONDITIONAL([ENABLE_PHONENUMBER],
	               [test "x$with_phonenumber" != "xno"])

	AS_VAR_IF([with_phonenumber], [yes],
	          [AC_DEFINE([ENABLE_PHONENUMBER], 1, [Enable phonenumber parsing])])

	AC_SUBST([PHONENUMBER_INCLUDES])
	AC_SUBST([PHONENUMBER_LIBS])
])

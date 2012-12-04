dnl EVO_PHONENUMBER_SUPPORT([default])
dnl Check for Google's libphonenumber. Adds a --with-phonenumber option
dnl to explicitly enable and disable phonenumber support, but also for
dnl pointing to libphonenumber's install prefix.
AC_DEFUN([EVO_PHONENUMBER_SUPPORT],[
	AC_MSG_CHECKING([whether to enable phonenumber support])

	evo_phonenumber_prefix=
	msg_phonenumber=no

	PHONENUMBER_INCLUDES=
	PHONENUMBER_LIBS=

	AC_ARG_WITH([phonenumber],
		[AS_HELP_STRING([--with-phonenumber@<:@=PREFIX@:>@],
		                [use libphonenumber at PREFIX])],
		[evo_phonenumber_prefix=$withval],
		[with_phonenumber=m4_default([$1],[check])])

	AC_MSG_RESULT([$with_phonenumber])

	AS_VAR_IF([with_phonenumber], [no],, [
		AC_LANG_PUSH(C++)

		PHONENUMBER_LIBS="-lphonenumber -lboost_thread"

		AS_VAR_IF([evo_phonenumber_prefix],,,[
			PHONENUMBER_INCLUDES="-I$evo_phonenumber_prefix/include"
			PHONENUMBER_LIBS="-L$evo_phonenumber_prefix/lib $PHONENUMBER_LIBS"
		])

		evo_cxxflags_saved="$CXXFLAGS"
		CXXFLAGS="$CXXFLAGS $PHONENUMBER_INCLUDES"

		evo_libs_saved="$LIBS"
		LIBS="$LIBS $PHONENUMBER_LIBS"

		AC_MSG_CHECKING([if libphonenumber is usable])
		AC_LINK_IFELSE(
			[AC_LANG_PROGRAM(
				[[#include <phonenumbers/phonenumberutil.h>]],
				[[i18n::phonenumbers::PhoneNumberUtil::GetInstance();]])],
			[with_phonenumber=yes],
			[AS_VAR_IF([with_phonenumber], [check], [with_phonenumber=no], [
				AC_MSG_ERROR([libphonenumber cannot be used. Use --with-phonenumber to specify the library prefix.])])
			])

		CXXFLAGS="$evo_cxxflags_saved"
		LDFLAGS="$evo_ldflags_saved"
		LIBS="$evo_libs_saved"

		AS_VAR_IF([evo_phonenumber_prefix],,
		          [msg_phonenumber=$with_phonenumber],
		          [msg_phonenumber=$evo_phonenumber_prefix])

		AC_MSG_RESULT([$with_phonenumber])
		AC_LANG_POP(C++)
	])

	AM_CONDITIONAL([ENABLE_PHONENUMBER],
	               [test "x$with_phonenumber" != "xno"])

	AS_VAR_IF([with_phonenumber], [yes],
	          [AC_DEFINE([ENABLE_PHONENUMBER], 1, [Enable phonenumber parsing])])

	AC_SUBST([PHONENUMBER_INCLUDES])
	AC_SUBST([PHONENUMBER_LIBS])
])

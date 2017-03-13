
dnl Evolution-Data-Server version
m4_define([eds_major_version], [3])
m4_define([eds_minor_version], [22])
m4_define([eds_micro_version], [7])

m4_define([eds_version],
	[eds_major_version.eds_minor_version.eds_micro_version])

dnl Base Version: This is for API/version tracking for things like
dnl Bonobo server files.  This should always be the major/minor of
dnl the stable version or stable version to be.  Note, this is set
dnl the way it is so that GETTEXT_PACKAGE will be parsed correctly.
BASE_VERSION=3.22
m4_define([base_version], [3.22])

dnl This number is meaningless, but we're now stuck with it in our
dnl library names for backward compatibility.
m4_define([api_version], [1.2])

dnl Autoconf / Automake Initialization
AC_PREREQ(2.62)
AC_INIT([evolution-data-server],[eds_version],[http://bugzilla.gnome.org/enter_bug.cgi?product=Evolution-Data-Server])
AM_INIT_AUTOMAKE([gnu 1.10 tar-ustar dist-xz no-dist-gzip -Wall -Wno-portability foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

dnl This is for the autoconf tests only - it set's the language we use
AC_LANG(C)

dnl Automake 1.11 - Silent Build Rules
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Required Package Versions

dnl Keep these two definitions in agreement.
m4_define([glib_minimum_version], [2.46])
m4_define([glib_encoded_version], [GLIB_VERSION_2_46])

dnl Keep these two definitions in agreement.
m4_define([gdk_minimum_version], [3.10])
m4_define([gdk_encoded_version], [GDK_VERSION_3_10])

dnl Keep these two definitions in agreement.
m4_define([soup_minimum_version], [2.42])
m4_define([soup_encoded_version], [SOUP_VERSION_2_42])

m4_define([gcr_minimum_version], [3.4])
m4_define([libsecret_minimum_version], [0.5])
m4_define([libxml_minimum_version], [2.0.0])		dnl XXX Just a Guess
m4_define([libgdata_minimum_version], [0.10])
m4_define([sqlite_minimum_version], [3.7.17])
m4_define([libical_minimum_version], [0.43])

dnl Optional Packages
m4_define([goa_minimum_version], [3.8])
m4_define([gweather_minimum_version], [3.10])
m4_define([libaccounts_glib_minimum_version], [1.4])
m4_define([libsignon_glib_minimum_version], [1.8])
m4_define([webkit2gtk_minimum_version], [2.11.91])
m4_define([json_glib_minimum_version], [1.0.4])

AC_SUBST([BASE_VERSION],[base_version])
AC_SUBST([API_VERSION],[api_version])
AC_DEFINE_UNQUOTED(BASE_VERSION, ["$BASE_VERSION"], [Base version (Major.Minor)])
AC_DEFINE_UNQUOTED(API_VERSION, ["$API_VERSION"], [API version (Major.Minor)])

EDS_MAJOR_VERSION=eds_major_version
EDS_MINOR_VERSION=eds_minor_version
EDS_MICRO_VERSION=eds_micro_version

GLIB_GSETTINGS

dnl ******************************
dnl D-Bus versioning
dnl ******************************
ADDRESS_BOOK_DBUS_SERVICE_NAME="org.gnome.evolution.dataserver.AddressBook9"
CALENDAR_DBUS_SERVICE_NAME="org.gnome.evolution.dataserver.Calendar7"
SOURCES_DBUS_SERVICE_NAME="org.gnome.evolution.dataserver.Sources5"
USER_PROMPTER_DBUS_SERVICE_NAME="org.gnome.evolution.dataserver.UserPrompter0"

AC_DEFINE_UNQUOTED(
       ADDRESS_BOOK_DBUS_SERVICE_NAME,
       ["$ADDRESS_BOOK_DBUS_SERVICE_NAME"],
       [D-Bus service name for the address book factory])

AC_DEFINE_UNQUOTED(
       CALENDAR_DBUS_SERVICE_NAME,
       ["$CALENDAR_DBUS_SERVICE_NAME"],
       [D-Bus service name for the calendar factory])

AC_DEFINE_UNQUOTED(
	SOURCES_DBUS_SERVICE_NAME,
	["$SOURCES_DBUS_SERVICE_NAME"],
	[D-Bus service name for the source registry])

AC_DEFINE_UNQUOTED(
	USER_PROMPTER_DBUS_SERVICE_NAME,
	["$USER_PROMPTER_DBUS_SERVICE_NAME"],
	[D-Bus service name for the user prompter])

AC_SUBST(ADDRESS_BOOK_DBUS_SERVICE_NAME)
AC_SUBST(CALENDAR_DBUS_SERVICE_NAME)
AC_SUBST(SOURCES_DBUS_SERVICE_NAME)
AC_SUBST(USER_PROMPTER_DBUS_SERVICE_NAME)

dnl ******************************
dnl Libtool versioning
dnl ******************************
LIBEDATASERVER_CURRENT=22
LIBEDATASERVER_REVISION=0
LIBEDATASERVER_AGE=0

LIBEDATASERVERUI_CURRENT=1
LIBEDATASERVERUI_REVISION=0
LIBEDATASERVERUI_AGE=0

LIBECAL_CURRENT=19
LIBECAL_REVISION=0
LIBECAL_AGE=0

LIBEDATACAL_CURRENT=28
LIBEDATACAL_REVISION=0
LIBEDATACAL_AGE=0

LIBEDATABOOK_CURRENT=25
LIBEDATABOOK_REVISION=0
LIBEDATABOOK_AGE=0

LIBEBOOK_CURRENT=19
LIBEBOOK_REVISION=1
LIBEBOOK_AGE=3

LIBEBOOK_CONTACTS_CURRENT=2
LIBEBOOK_CONTACTS_REVISION=0
LIBEBOOK_CONTACTS_AGE=0

LIBCAMEL_CURRENT=59
LIBCAMEL_REVISION=0
LIBCAMEL_AGE=0

LIBEBACKEND_CURRENT=10
LIBEBACKEND_REVISION=0
LIBEBACKEND_AGE=0

AC_SUBST(EDS_MAJOR_VERSION)
AC_SUBST(EDS_MINOR_VERSION)
AC_SUBST(EDS_MICRO_VERSION)
AC_SUBST(LIBEDATASERVER_CURRENT)
AC_SUBST(LIBEDATASERVER_REVISION)
AC_SUBST(LIBEDATASERVER_AGE)
AC_SUBST(LIBEDATASERVERUI_CURRENT)
AC_SUBST(LIBEDATASERVERUI_REVISION)
AC_SUBST(LIBEDATASERVERUI_AGE)
AC_SUBST(LIBECAL_CURRENT)
AC_SUBST(LIBECAL_REVISION)
AC_SUBST(LIBECAL_AGE)
AC_SUBST(LIBEDATACAL_CURRENT)
AC_SUBST(LIBEDATACAL_REVISION)
AC_SUBST(LIBEDATACAL_AGE)
AC_SUBST(LIBEBOOK_CURRENT)
AC_SUBST(LIBEBOOK_REVISION)
AC_SUBST(LIBEBOOK_AGE)
AC_SUBST(LIBEBOOK_CONTACTS_CURRENT)
AC_SUBST(LIBEBOOK_CONTACTS_REVISION)
AC_SUBST(LIBEBOOK_CONTACTS_AGE)
AC_SUBST(LIBEDATABOOK_CURRENT)
AC_SUBST(LIBEDATABOOK_REVISION)
AC_SUBST(LIBEDATABOOK_AGE)
AC_SUBST(LIBCAMEL_CURRENT)
AC_SUBST(LIBCAMEL_REVISION)
AC_SUBST(LIBCAMEL_AGE)
AC_SUBST(LIBEBACKEND_CURRENT)
AC_SUBST(LIBEBACKEND_REVISION)
AC_SUBST(LIBEBACKEND_AGE)

dnl **************************************
dnl Put the ACLOCAL flags in the Makefile
dnl **************************************
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

dnl *********************************************
dnl Figure out early if we'll need a C++ compiler
dnl *********************************************

EVO_PHONENUMBER_ARGS

dnl ******************************
dnl Initialize maintainer mode
dnl ******************************
AM_MAINTAINER_MODE([enable])

if test "x$enable_maintainer_mode" = "xyes" ; then
	AC_DEFINE(ENABLE_MAINTAINER_MODE, 1, [Configured with enabled maintainer mode])
fi

dnl ******************************
dnl Compiler Warning Flags
dnl ******************************
proposed_warning_flags=
append_c_warning_flags=

if test "x$enable_maintainer_mode" = "xyes" ; then
	proposed_warning_flags="-Wall -Wextra -Wdeprecated-declarations"
else
	proposed_warning_flags="-Wno-deprecated-declarations"
fi

if test "$CC" = "clang" ; then
	append_c_warning_flags="-Wno-parentheses-equality -Wno-format-nonliteral"
fi

proposed_warning_flags="$proposed_warning_flags
		-Werror-implicit-function-declaration
		-Wformat -Wformat-security -Winit-self
		-Wmissing-declarations -Wmissing-include-dirs
		-Wmissing-noreturn -Wpointer-arith
		-Wredundant-decls -Wundef -Wwrite-strings"

proposed_c_warning_flags="$proposed_warning_flags
	-Wdeclaration-after-statement
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wnested-externs
	$append_c_warning_flags"

proposed_cxx_warning_flags="$proposed_warning_flags
	-Wabi -Wnoexcept"

AS_COMPILER_FLAGS(WARNING_FLAGS, [$proposed_c_warning_flags])
AC_SUBST(WARNING_FLAGS)

dnl Other useful compiler warnings for test builds only.
dnl These may produce warnings we have no control over,
dnl or false positives we don't always want to see.
dnl
dnl	-Wformat-nonliteral
dnl	-Wmissing-format-attribute
dnl	-Wshadow
dnl	-Wstrict-aliasing=2

AM_CFLAGS="$WARNING_FLAGS -fno-strict-aliasing"
AC_SUBST(AM_CFLAGS)

dnl C++ Compiler flags, needed for ICU C++ access and libphonenumber usage
AC_PROG_CXX

AC_LANG_PUSH([C++])
AS_COMPILER_FLAGS(CXX_WARNING_FLAGS, [$proposed_cxx_warning_flags])
AC_SUBST(CXX_WARNING_FLAGS)
AM_CXXFLAGS="$CXX_WARNING_FLAGS"
AC_SUBST(AM_CXXFLAGS)
AC_LANG_POP([C++])

dnl Permits linking of C++ based libraries using the C linker if needed.
AC_SUBST([predeps_CXX])
AC_SUBST([postdeps_CXX])

AM_CPPFLAGS="$WARNING_FLAGS -fno-strict-aliasing"

dnl Warn about API usage that violates our minimum requirements.
AM_CPPFLAGS="$AM_CPPFLAGS -DGLIB_VERSION_MAX_ALLOWED=glib_encoded_version"
AM_CPPFLAGS="$AM_CPPFLAGS -DGDK_VERSION_MAX_ALLOWED=gdk_encoded_version"
AM_CPPFLAGS="$AM_CPPFLAGS -DSOUP_VERSION_MAX_ALLOWED=soup_encoded_version"

dnl These will suppress warnings about newly-deprecated symbols.  Ideally
dnl these settings should match our minimum requirements and we will clean
dnl up any new deprecation warnings after bumping our minimum requirements.
dnl But if the warnings get to be overwhelming, use fixed versions instead.
AM_CPPFLAGS="$AM_CPPFLAGS -DGLIB_VERSION_MIN_REQUIRED=glib_encoded_version"
AM_CPPFLAGS="$AM_CPPFLAGS -DGDK_VERSION_MIN_REQUIRED=gdk_encoded_version"
AM_CPPFLAGS="$AM_CPPFLAGS -DSOUP_VERSION_MIN_REQUIRED=soup_encoded_version"

AC_SUBST(AM_CPPFLAGS)

AC_PROG_CC
AC_PROG_CPP
AC_C_INLINE
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PATH_PROG([PERL], [perl])
AC_SUBST(PERL)

dnl For gen-western-table.py
AM_PATH_PYTHON

dnl GCC 4.4 got more aggressive in its aliasing optimizations, changing
dnl behavior that -- according to the C99 standard -- is supposed to be
dnl undefined.  We may still have aliasing abuses lying around that rely
dnl on GCC's previous "undefined" behavior, so disable strict-aliasing
dnl optimization until we can find and fix all the abuses.
dnl (AC_PROG_CC must run first to set the GCC variable.)
dnl XXX This really belongs in AM_CFLAGS.
if test "x${GCC}" = "xyes"; then
	CFLAGS="$CFLAGS -fno-strict-aliasing"
fi

dnl ******************************
dnl I18N stuff
dnl ******************************
IT_PROG_INTLTOOL([0.35.5])

GETTEXT_PACKAGE=evolution-data-server-$BASE_VERSION
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

dnl ******************************
dnl Initialize libtool
dnl ******************************
LT_PREREQ(2.2)
LT_INIT(disable-static win32-dll)

PKG_PROG_PKG_CONFIG

dnl This relies on $SED, which is defined in ltmain.sh, which
dnl is invoked by LT_INIT (at least I think that's how it works).
GNOME_CODE_COVERAGE

dnl Add the option install unit tests
EDS_INSTALLED_TESTS

dnl ******************************
dnl Gtk Doc stuff
dnl ******************************
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

dnl The private D-Bus documentation is for developers only.
AC_ARG_WITH([private-docs],
	AS_HELP_STRING([--with-private-docs],
	[Build documentation for private libraries ]
	[(requires --enable-gtk-doc) [default=no]],),
	[with_private_docs="$withval"],[with_private_docs="no"])
AM_CONDITIONAL(WITH_PRIVATE_DOCS, [test x$with_private_docs = xyes])

dnl This must appear after AC_CANONICAL_HOST, which defines $host.
case "$host" in
*openbsd*|*freebsd*|*darwin*)
	dnl Do not set '-Wl,--no-undefined' on freebsd/openbsd/darwin
	;;
*)
	LDFLAGS="$LDFLAGS -Wl,--no-undefined"
	;;
esac

dnl ******************************
dnl Check for Win32
dnl ******************************
AC_MSG_CHECKING([for Win32])
case "$host" in
*-mingw*)
	os_win32='yes'
	NO_UNDEFINED='-no-undefined'
	SOCKET_LIBS='-lws2_32 -ldnsapi'
	DL_LIB=''
	LIBEXECDIR_IN_SERVER_FILE='../../../libexec'
	AC_CACHE_VAL(ac_cv_have_addrinfo, [ac_cv_have_addrinfo=yes])
	;;
*openbsd*|*freebsd*)
	os_win32='no'
	NO_UNDEFINED=''
	SOCKET_LIBS=''
	DL_LIB=''
	LIBEXECDIR_IN_SERVER_FILE="$libexecdir"
	;;
*)
	os_win32='no'
	NO_UNDEFINED=''
	SOCKET_LIBS=''
	DL_LIB='-ldl'
	LIBEXECDIR_IN_SERVER_FILE="$libexecdir"
	;;
esac
AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL(OS_WIN32, [test $os_win32 = yes])
AC_SUBST(NO_UNDEFINED)
AC_SUBST(SOCKET_LIBS)
AC_SUBST(LIBEXECDIR_IN_SERVER_FILE)

dnl **************************************
dnl Check for posix compatible sys/wait.h
dnl **************************************
AC_HEADER_SYS_WAIT

dnl ************************************
dnl Check for posix compatible alloca()
dnl ************************************
AC_FUNC_ALLOCA

dnl ******************************
dnl Checks for functions
dnl ******************************
AC_CHECK_FUNCS(fsync strptime strtok_r nl_langinfo)

dnl ***********************************
dnl Check for base dependencies early.
dnl ***********************************
PKG_CHECK_MODULES(GNOME_PLATFORM,
	[gio-2.0 >= glib_minimum_version
	gmodule-2.0 >= glib_minimum_version
	libxml-2.0 >= libxml_minimum_version
	libsoup-2.4 >= soup_minimum_version])

if test x$os_win32 = xno; then
	PKG_CHECK_MODULES(GIO_UNIX, [gio-unix-2.0])
else
	PKG_CHECK_MODULES(GIO_UNIX, [gio-windows-2.0])
fi

dnl ******************************
dnl Check for libphonenumber
dnl ******************************

EVO_PHONENUMBER_SUPPORT

dnl *******************
dnl Check for ICU
dnl *******************
dnl
dnl ICU recently started shipping pkg-config files but it's
dnl not present on many systems, if we don't find the pkg-config
dnl file then let's fallback on a manual check
enable_icu=no
PKG_CHECK_MODULES(ICU, icu-i18n, enable_icu=yes, [
        enable_icu=yes
	AC_CHECK_HEADERS(unicode/ucol.h,, enable_icu=no)
	AC_MSG_CHECKING([for libicui18n])
	LIBS_old=$LIBS
	LIBS="$LIBS -licui18n -licuuc -licudata"
	AC_TRY_LINK([#include <unicode/ucol.h>],
		[ucol_open ("", NULL);],
        	AC_MSG_RESULT(yes),
        	AC_MSG_RESULT(no); enable_icu=no)
	LIBS=$LIBS_old
	if [test x$enable_icu = xyes]; then
		ICU_CFLAGS=-D_REENTRANT
		ICU_LIBS="-licui18n -licuuc -licudata"
		AC_SUBST(ICU_CFLAGS)
		AC_SUBST(ICU_LIBS)
	else
		AC_MSG_ERROR([ICU libs not found, icu-i18n is required.])
	fi
])

dnl *************************
dnl Check for GTK+
dnl *************************
AC_ARG_ENABLE([gtk],
	[AS_HELP_STRING([--enable-gtk],
	[enable GTK+ (default=yes)])],
	[enable_gtk=$enableval], [enable_gtk=yes])
AC_MSG_CHECKING([if GTK+ support is enabled])
AC_MSG_RESULT([$enable_gtk])
if test "x$enable_gtk" = xyes; then
	PKG_CHECK_MODULES(
		[GTK], [gtk+-3.0 >= gdk_minimum_version],,
		[AC_MSG_ERROR([

	gtk+-3.0 not found (or version < gdk_minimum_version)

	If you want to disable GTK+ support,
	please append --disable-gtk to configure.

	])])

	PKG_CHECK_MODULES(
		[GCR], [gcr-3 >= gcr_minimum_version],,
		[AC_MSG_ERROR([

	gcr-3.0 not found (or version < gcr_minimum_version)

	If you want to disable gcr-3.0's GTK+ widgets,
	please append --disable-gtk to configure.

	])])

	AC_DEFINE(HAVE_GTK, 1, [Define to 1 if you have the gtk+-3.0 package.])
fi
AM_CONDITIONAL(HAVE_GTK, [test x$enable_gtk = xyes])

dnl **********************************
dnl Check for WebKitGTK+ and json-glib
dnl **********************************
AC_ARG_ENABLE([google-auth],
	[AS_HELP_STRING([--enable-google-auth],
	[enable Google authentication (default=yes)])],
	[enable_google_auth=$enableval], [enable_google_auth=yes])
AC_MSG_CHECKING([if Google authentication support is enabled])
AC_MSG_RESULT([$enable_google_auth])
if test "x$enable_google_auth" = "xyes"; then
	PKG_CHECK_MODULES(
		[GOOGLE_AUTH], [webkit2gtk-4.0 >= webkit2gtk_minimum_version
				json-glib-1.0 >= json_glib_minimum_version],,
		[AC_MSG_ERROR([

	Libraries to support Google authentication prompts not found,
	or versions not new enough.

	If you want to disable Google authentication support,
	please append --disable-google-auth to configure.

	])])

	AC_DEFINE(ENABLE_GOOGLE_AUTH, 1, [Define to 1 if Google autentication support is enabled.])
	AC_ARG_WITH(google-client-id, [AS_HELP_STRING([--with-google-client-id], [Google OAuth 2.0 client id])], [], [])
	if test "x$with_google_client_id" = "x"; then
		with_google_client_id=590402290962-2i0b7rqma8b9nmtfrcp7fa06g6cf7g74.apps.googleusercontent.com
	fi
	AC_ARG_WITH(google-client-secret, [AS_HELP_STRING([--with-google-client-secret], [Google OAuth 2.0 client secret])], [], [])
	if test "x$with_google_client_secret" = "x"; then
		with_google_client_secret=mtfUe5W8Aal9DcgVipOY1T9G
	fi
	AC_DEFINE_UNQUOTED(GOOGLE_CLIENT_ID, ["$with_google_client_id"], [Define Google OAuth 2.0 Client ID to use])
	AC_DEFINE_UNQUOTED(GOOGLE_CLIENT_SECRET, ["$with_google_client_secret"], [Define Google OAuth 2.0 Client Secret to use])
fi
AM_CONDITIONAL(ENABLE_GOOGLE_AUTH, [test x$enable_google_auth = xyes])

dnl ******************************************
dnl Check whether to build examples/demos
dnl ******************************************
AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples],
	[enable the building examples (default=yes)])],
	[enable_examples=$enableval], [enable_examples=yes])
AC_MSG_CHECKING([if examples should be built])
AC_MSG_RESULT([$enable_examples])
if test "x$enable_examples" = xyes; then
	PKG_CHECK_MODULES([EXAMPLES],
		[gtk+-3.0 >= 3.10 glib-2.0 >= 2.38],,
	[AC_MSG_ERROR([
	Some packages were not available to build the example program(s).

	If you want to disable examples, please
	append --disable-examples to configure.
	])])

	AC_DEFINE(BUILD_EXAMPLES, 1, [Define to 1 if the examples should be built.])
fi
AM_CONDITIONAL(BUILD_EXAMPLES, [test x$enable_examples = xyes])


dnl *******************************
dnl Check for GNOME Online Accounts
dnl *******************************
AC_ARG_ENABLE([goa],
	[AS_HELP_STRING([--enable-goa],
	[enable GNOME Online Accounts support (default=yes)])],
	[enable_goa=$enableval], [enable_goa=yes])
AC_MSG_CHECKING([if GNOME Online Accounts support is enabled])
AC_MSG_RESULT([$enable_goa])
if test "x$enable_goa" = xyes; then
	PKG_CHECK_MODULES(
		[GOA], [goa-1.0 >= goa_minimum_version],,
		[AC_MSG_ERROR([

	goa-1.0 not found (or version < goa_minimum_version)

	If you want to disable GNOME Online Accounts support,
	please append --disable-goa to configure.

	])])

	AC_DEFINE(HAVE_GOA, 1, [Define to 1 if you have the goa-1.0 package.])
fi
AM_CONDITIONAL(HAVE_GOA, [test x$enable_goa = xyes])

dnl ********************************
dnl Check for Ubuntu Online Accounts
dnl ********************************
AC_ARG_ENABLE([uoa],
	[AS_HELP_STRING([--enable-uoa],
	[enable Ubuntu Online Accounts support (default=yes)])],
	[enable_uoa=$enableval], [enable_uoa=yes])
AC_MSG_CHECKING([if Ubuntu Online Accounts support is enabled])
AC_MSG_RESULT([$enable_uoa])
if test "x$enable_uoa" = xyes; then
	PKG_CHECK_MODULES(
		[LIBACCOUNTS_GLIB],
		[libaccounts-glib >= libaccounts_glib_minimum_version],,
		[AC_MSG_ERROR([

	libaccounts-glib not found (or version < libaccounts_glib_minimum_version)

	If you want to disable Ubuntu Online Accounts support,
	please append --disable-uoa to configure.

	])])

	PKG_CHECK_MODULES(
		[LIBSIGNON_GLIB],
		[libsignon-glib >= libsignon_glib_minimum_version],,
		[AC_MSG_ERROR([

	libsignon-glib not found (or version < libsignon_glib_minimum_version)

	If you want to disable Ubuntu Online Accounts support,
	please append --disable-uoa to configure.

	])])

	PKG_CHECK_MODULES(
		[JSON_GLIB], [json-glib-1.0],,
		[AC_MSG_ERROR([

	json-glib-1.0 not found

	If you want to disable Ubuntu Online Accounts support,
	please append --disable-uoa to configure.

	])])

	PKG_CHECK_MODULES(
		[REST], [rest-0.7],,
		[AC_MSG_ERROR([

	librest-0.7 not found

	If you want to disable Ubuntu Online Accounts support,
	please append --disable-uoa to configure.

	])])

	AC_DEFINE(HAVE_UOA, 1, [Have libaccounts-glib])
fi
AM_CONDITIONAL(HAVE_UOA, [test x$enable_uoa = xyes])

dnl **********************************************
dnl Check if backend per process should be enabled
dnl **********************************************
AC_ARG_ENABLE([backend-per-process],
	[AS_HELP_STRING([--enable-backend-per-process],
	[enable backend per process support (default=yes)])],
	[enable_backend_per_process=$enableval], [enable_backend_per_process=yes])
AC_MSG_CHECKING([if backend per process is enabled])
AC_MSG_RESULT([$enable_backend_per_process])
if test "x$enable_backend_per_process" = xyes; then
	AC_DEFINE(ENABLE_BACKEND_PER_PROCESS, 1, [If backend-per-process is enabled])
fi
AM_CONDITIONAL(ENABLE_BACKEND_PER_PROCESS, [test x$enable_backend_per_process = xyes])

dnl ***********************************
dnl Check for GNOME Keyring.
dnl ***********************************
if test x$os_win32 = xno; then
	PKG_CHECK_MODULES(LIBSECRET,
		[libsecret-unstable >= libsecret_minimum_version])
fi
AC_SUBST(LIBSECRET_CFLAGS)
AC_SUBST(LIBSECRET_LIBS)

dnl **********************************************************
dnl gcr-base is needed for secure password exchange over D-Bus
dnl **********************************************************
PKG_CHECK_MODULES(GCR_BASE, [gcr-base-3 >= gcr_minimum_version])
AC_SUBST(GCR_BASE_CFLAGS)
AC_SUBST(GCR_BASE_LIBS)

LIBICAL_REQUIRED=libical_minimum_version
AC_SUBST(LIBICAL_REQUIRED)

dnl ******************************
dnl regex check
dnl ******************************
AC_CHECK_FUNCS(regexec,,[AC_CHECK_LIB(regex,regexec,
			[REGEX_LIBS=-lregex
			AC_DEFINE(HAVE_REGEXEC,1,[Define to 1 if you have the regexec function.])],
			[AC_MSG_ERROR([No regex library found])])])
AC_SUBST(REGEX_LIBS)

dnl ******************************
dnl libdb checking
dnl ******************************
AC_ARG_WITH([libdb],
	AS_HELP_STRING([--with-libdb=PREFIX],
	[Prefix where libdb is installed]),
	[with_libdb=$withval],[with_libdb=yes])

if test "x$with_libdb" != "xno" ; then
	if test "x$with_libdb" != "xyes" ; then
		DB_CFLAGS="$CFLAGS -I$with_libdb/include"
		DB_LIBS="$LIBS -L$with_libdb/lib -ldb"
	else
		if test -z "$DB_CFLAGS" -a -z "$DB_LIBS"; then
			DB_CFLAGS="$CFLAGS"
			DB_LIBS="$LIBS -ldb"
		fi
	fi

	AC_MSG_CHECKING([Berkeley DB])
	save_cflags=$CFLAGS; CFLAGS="$CFLAGS $DB_CFLAGS"
	save_libs=$LIBS; LIBS="$LIBS $DB_LIBS"
	AC_LINK_IFELSE([AC_LANG_PROGRAM(
		[[#include <db.h>]],
		[[db_create(NULL, NULL, 0)]])],
		[AC_MSG_RESULT([yes])],
		[AC_MSG_ERROR([Cannot find libdb])])
	CFLAGS=$save_cflags
	LIBS=$save_libs
	AC_SUBST(DB_CFLAGS)
	AC_SUBST(DB_LIBS)

	AC_DEFINE(HAVE_LIBDB, 1, [Have libdb])
fi
AM_CONDITIONAL(HAVE_LIBDB, [test x$with_libdb != xno])


dnl ******************************
dnl db_load checking
dnl ******************************
have_db_load=no
AC_PATH_PROG(DB_LOAD, [db_load])
if test -z "$DB_LOAD"; then
   AC_MSG_WARN([db_load not found, some unit tests will not be run.])
else
   have_db_load=yes
fi

AM_CONDITIONAL(HAVE_DB_LOAD, test x"$have_db_load" = "xyes")

dnl ******************************
dnl iconv checking
dnl ******************************
have_iconv="no"
save_LIBS="$LIBS"
LIBS="$LIBS -liconv"
AC_CACHE_CHECK([for iconv in -liconv], [ac_cv_libiconv],
	AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[
		#include <iconv.h>
		#include <stdlib.h>
	]],
	[[
		iconv_t cd;
		cd = iconv_open ("UTF-8", "ISO-8859-1");
	]]
	)],[ac_cv_libiconv=yes],[ac_cv_libiconv=no]))

if test "x$ac_cv_libiconv" = "xyes"; then
	ICONV_LIBS="-liconv"
	if test "x$os_win32" = "xyes"; then
		dnl Don't pointlessly auto-export the global symbols
		dnl from a potentially static libiconv.a
		ICONV_LIBS="$ICONV_LIBS -Wl,--exclude-libs=libiconv.a"
	fi
	have_iconv="yes"
else
	LIBS="$save_LIBS"
	AC_CHECK_FUNC(iconv, [have_iconv=yes], [have_iconv=no])
fi

if test "x$have_iconv" = "xyes"; then
	if test "x$ac_cv_libiconv" = "xno"; then
		AC_CHECK_FUNCS(gnu_get_libc_version)
	fi
AC_CACHE_CHECK([if iconv() handles UTF-8], [ac_cv_libiconv_utf8],
	AC_RUN_IFELSE([AC_LANG_SOURCE([[
		#include <iconv.h>
		#include <stdlib.h>
		#include <string.h>
		#ifdef HAVE_GNU_GET_LIBC_VERSION
		#include <gnu/libc-version.h>
		#endif
		int main() {
			char *jp = "\x1B\x24\x42\x46\x7C\x4B\x5C\x38\x6C";
			char *utf8 = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
			char *transbuf = malloc (10), *trans = transbuf;
			iconv_t cd;
			size_t jp_len = strlen (jp), utf8_len = 10;
			size_t utf8_real_len = strlen (utf8);

			#ifdef HAVE_GNU_GET_LIBC_VERSION
			/* glibc 2.1.2's iconv is broken in hard to test ways. */
			if (!strcmp (gnu_get_libc_version (), "2.1.2"))
				exit (1);
			#endif

			cd = iconv_open ("UTF-8", "ISO-2022-JP");
			if (cd == (iconv_t) -1)
				exit (1);
			if (iconv (cd, &jp, &jp_len, &trans, &utf8_len) == -1 || jp_len != 0)
				exit (1);
			if (memcmp (utf8, transbuf, utf8_real_len) != 0)
				exit (1);
			return (0);}
		]])
	],[ac_cv_libiconv_utf8=yes],[ac_cv_libiconv_utf8=no; have_iconv=no],[ac_cv_libiconv_utf8=hopefully]))
fi

if test "x$have_iconv" = "xno"; then
	AC_MSG_ERROR([You need to install a working iconv implementation, such as ftp://ftp.gnu.org/pub/gnu/libiconv])
fi
AC_SUBST(ICONV_LIBS)

CFLAGS="$CFLAGS -I$srcdir $GNOME_PLATFORM_CFLAGS"
LIBS="$LIBS $ICONV_LIBS $GNOME_PLATFORM_LIBS"

AC_MSG_CHECKING([preferred charset name formats for system iconv])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
	#define CONFIGURE_IN
	#include "iconv-detect.c"
	]])],
	[AC_MSG_RESULT([found])],
	[AC_MSG_RESULT([not found])
		AC_MSG_WARN([
		*** The iconv-detect program was unable to determine the
		*** preferred charset name formats recognized by your
		*** iconv library. It is suggested that you install a
		*** working iconv library such as the one found at
		*** ftp://ftp.gnu.org/pub/gnu/libiconv
	])],
	[if test "x$os_win32" = xyes; then
		AC_MSG_RESULT([using known win32 result])
		echo '/* This is an auto-generated header, DO NOT EDIT! */' > iconv-detect.h
		echo  >>iconv-detect.h
		echo '#define ICONV_ISO_D_FORMAT "iso-%d-%d"' >>iconv-detect.h
		echo '#define ICONV_ISO_S_FORMAT "iso-%d-%s"' >>iconv-detect.h
		echo '#define ICONV_10646 "UCS-4BE"' >>iconv-detect.h
	else
		AC_MSG_RESULT([unknown])
		AC_MSG_WARN([
		*** We can't determine the preferred charset name formats
		*** recognized by your iconv library. You are
		*** cross-compiling and supposed to know what you are doing.
		*** Please construct the iconv-detect.h file manually.
		])
	fi
])

CFLAGS="$save_CFLAGS"
LIBS="$save_LIBS"

AC_ARG_ENABLE([backtraces],
	[AS_HELP_STRING([--enable-backtraces],
	[enable backtraces for camel_pointer_tracker (default=no)])],
	[enable_backtraces=$enableval], [enable_backtraces=no])

if test "x$enable_backtraces" = xyes; then
	dnl ****************************
	dnl Check for backtrace_symbols function and dwfl from elfutils
	dnl ****************************
	AC_MSG_CHECKING([libc backtrace_symbols function])
	AC_LINK_IFELSE([AC_LANG_PROGRAM(
		[[#include <execinfo.h>]],
		[[{ void *bt[1]; backtrace_symbols (bt, backtrace(bt, 1)); }]])],
		[AC_DEFINE(HAVE_BACKTRACE_SYMBOLS, 1, [libc provides backtrace_symbols function]) ac_cv_have_bsf=yes],[ac_cv_have_bsf=no])
	AC_MSG_RESULT([$ac_cv_have_bsf])

	if test "x$ac_cv_have_bsf" = xyes; then
		LIBS="$LIBS -ldw"

		AC_MSG_CHECKING([elfutils/libdwfl])
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
			[[#include <elfutils/libdwfl.h>]],
			[[{
				Dwfl *dwfl;
				Dwfl_Module *module;
				Dwarf_Addr module_low_addr;
				Dwfl_Line *line;

				dwfl_standard_find_debuginfo;
				dwfl_linux_proc_find_elf;
				dwfl_begin (NULL);
				dwfl_linux_proc_report (NULL, 1);
				dwfl_report_end (NULL, NULL, NULL);
				dwfl_end (NULL);
				dwfl_module_addrname (NULL, NULL);
				dwfl_module_getsrc (NULL, NULL);
				dwfl_lineinfo (NULL, NULL, NULL, NULL, NULL, NULL);
				DWARF_CB_ABORT; DWARF_CB_OK;
				dwfl_getmodules (NULL, NULL, NULL, 0);
			}]])],
			[AC_DEFINE(HAVE_ELFUTILS_LIBDWFL, 1, [have elfutils/libdwfl.h functions]) ac_cv_have_elfdwfl=yes],[ac_cv_have_elfdwfl=no])
		AC_MSG_RESULT([$ac_cv_have_elfdwfl])

		LIBS="$save_LIBS"

		if test "x$ac_cv_have_elfdwfl" = xyes; then
			LIBDWFL_LIBS="-ldw"
			AC_SUBST(LIBDWFL_LIBS)
		fi
	fi
fi

dnl ******************************
dnl Check for nl_langinfo features
dnl ******************************
EVO_CHECK_LANGINFO([CODESET])
EVO_CHECK_LANGINFO([_NL_ADDRESS_COUNTRY_AB2])

dnl *******************************************************
dnl Check to see if strftime supports the use of %l and %k
dnl *******************************************************
AC_MSG_CHECKING([for %l and %k support in strftime])
AC_RUN_IFELSE([AC_LANG_SOURCE(
	[[	#include <stdlib.h>
		#include <string.h>
		#include <time.h>
		int main(int argc, char **argv)	{
		char buf[10];
		time_t rawtime;
		struct tm *timeinfo;

		time(&rawtime);
		timeinfo=localtime(&rawtime);
		buf[0] = '\0';
		strftime(buf, 10, "%lx%k", timeinfo);

		if (buf[0] == '\0' || buf[0] == 'x' || strstr(buf, "l") || strstr(buf, "k"))
			exit(1);
		else
			exit(0);
		return 0;
		}
	]]
	)],[AC_DEFINE(HAVE_LKSTRFTIME, 1, [strftime supports use of l and k]) ac_cv_lkstrftime=yes],[ac_cv_lkstrftime=no],[ac_cv_lkstrftime=no])
AC_MSG_RESULT([$ac_cv_lkstrftime])

dnl ********************************************************************************
dnl security extension support (SSL and S/MIME)
dnl
dnl The following voodoo does detection of mozilla libraries (nspr and nss)
dnl needed by Camel (SSL and S/MIME).
dnl
dnl The Evolution security extensions are only built if these libraries are found
dnl ********************************************************************************
msg_smime="no"

AC_ARG_ENABLE([smime],
	AS_HELP_STRING([--enable-smime],
	[Enable SMIME support through Mozilla nss @<:@default=yes@:>@ ]),
	[enable_smime="$enableval"],[enable_smime="yes"])

AC_ARG_WITH([nspr-includes],
	AS_HELP_STRING([--with-nspr-includes],
	[Prefix of Mozilla nspr4 includes.]),
	[with_nspr_includes="$withval"])

AC_ARG_WITH([nspr-libs],
	AS_HELP_STRING([--with-nspr-libs],
	[Prefix of Mozilla nspr4 libs.]),
	[with_nspr_libs="$withval"])

AC_ARG_WITH([nss-includes],
	AS_HELP_STRING([--with-nss-includes],
	[Prefix of Mozilla nss3 includes.]),
	[with_nss_includes="$withval"])

AC_ARG_WITH([nss-libs],
	AS_HELP_STRING([--with-nss-libs],
	[Prefix of Mozilla nss3 libs.]),
	[with_nss_libs="$withval"])

if test -n "${with_nspr_includes}" || test -n "${with_nspr_libs}" || test -n "${with_nss_includes}" || test -n "${with_nss_libs}"; then
	check_manually="yes"
else
	check_manually="no"
fi

dnl check if pkg-config files exist (which are only shipped by distributions, not upstream)
if test "x${check_manually}" = "xno"; then
	AC_MSG_CHECKING(Mozilla NSPR pkg-config module name)
	mozilla_nspr_pcs="nspr mozilla-nspr firefox-nspr xulrunner-nspr seamonkey-nspr"
	for pc in $mozilla_nspr_pcs; do
		if $PKG_CONFIG --exists $pc; then
			AC_MSG_RESULT($pc)
			mozilla_nspr=$pc
			break;
		fi
	done

	AC_MSG_CHECKING(Mozilla NSS pkg-config module name)
	mozilla_nss_pcs="nss mozilla-nss firefox-nss xulrunner-nss seamonkey-nss"
	for pc in $mozilla_nss_pcs; do
		if $PKG_CONFIG --exists $pc; then
			AC_MSG_RESULT($pc)
			mozilla_nss=$pc
			break;
		fi
	done

	if test -n "$mozilla_nspr" -a -n "$mozilla_nss"; then
		if test "x$enable_smime" = "xyes"; then
			AC_DEFINE(ENABLE_SMIME,1,[Define if SMIME should be enabled])
			msg_smime="yes"
		fi
		MANUAL_NSPR_CFLAGS=""
		MANUAL_NSPR_LIBS=""
		MANUAL_NSS_CFLAGS=""
		MANUAL_NSS_LIBS=""
	else
		check_manually="yes"
		mozilla_nspr=""
		mozilla_nss=""
	fi
fi

if test "x${check_manually}" = "xyes"; then
dnl ******************
dnl Check for NSPR 4
dnl ******************
	AC_MSG_CHECKING([for Mozilla nspr4 includes])
		CPPFLAGS_save="$CPPFLAGS"
		if test -n "$with_nspr_includes"; then
			CPPFLAGS="$CPPFLAGS -I$with_nspr_includes"
		fi

		AC_CHECK_HEADERS(nspr.h prio.h,
		[moz_nspr_includes="yes"],[moz_nspr_includes="no"])
		CPPFLAGS="$CPPFLAGS_save"

		if test "x${moz_nspr_includes}" = "xyes"; then
			MANUAL_NSPR_CFLAGS="-I$with_nspr_includes"
		else
			AC_MSG_FAILURE([NSPR headers not found. Use --with-nspr-includes to specify the include dir of NSPR.])
		fi
	AC_MSG_RESULT([$moz_nspr_includes])

	AC_MSG_CHECKING([for Mozilla nspr libraries])
		CFLAGS_save="$CFLAGS"
		LDFLAGS_save="$LDFLAGS"
		LIBS_save="$LIBS"
		nsprlibs="$DL_LIB -lplc4 -lplds4 -lnspr4"

		CFLAGS="$CFLAGS $MANUAL_NSPR_CFLAGS"
		LIBS="$nsprlibs"
		dnl Test to protect from giving libtool an -L , which results in failure (used below,too)
		if test -n "${with_nspr_libs}"; then
			LDFLAGS="$LDFLAGS -L$with_nspr_libs"
		else
			LDFLAGS="$LDFLAGS"
		fi

		AC_LINK_IFELSE([AC_LANG_CALL([],[PR_Init])],
		[moz_nspr_libs="yes"],[moz_nspr_libs="no"])

		CFLAGS="$CFLAGS_save"
		LDFLAGS="$LDFLAGS_save"
		LIBS="$LIBS_save"

		if test "x${moz_nspr_libs}" = "xyes"; then
			if test -n "${with_nspr_libs}"; then
				MANUAL_NSPR_LIBS="-L$with_nspr_libs $nsprlibs"
			else
				MANUAL_NSPR_LIBS="$nsprlibs"
			fi
		else
			AC_MSG_FAILURE([NSPR libs not found. Use --with-nspr-libs to specify the libdir of NSPR.])
		fi
	AC_MSG_RESULT([$moz_nspr_libs])

dnl *****************
dnl Check for NSS 3
dnl *****************
	AC_MSG_CHECKING([for Mozilla nss3 includes])
		CPPFLAGS_save="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS -I$with_nspr_includes -I$with_nss_includes"

		AC_CHECK_HEADERS(nss.h ssl.h smime.h,
		[moz_nss_includes="yes"],[moz_nss_includes="no"])
		CPPFLAGS="$CPPFLAGS_save"

		if test "x${moz_nss_includes}" = "xyes"; then
			MANUAL_NSS_CFLAGS="-I$with_nss_includes"
		else
			AC_MSG_FAILURE([NSS headers not found. Use --with-nss-includes to specify the include dir of NSS.])
		fi
	AC_MSG_RESULT($moz_nss_includes)

	AC_MSG_CHECKING([for Mozilla nss libraries])
		LDFLAGS_save="$LDFLAGS"
		LIBS_save="$LIBS"
		nsslibs="-lssl3 -lsmime3 -lnss3"

		LIBS="$nsslibs $nsprlibs"
		dnl Test to protect from giving libtool an -L , which results in failure (used below,too)
		if test -n "${with_nss_libs}"; then
			LDFLAGS="$LDFLAGS -L$with_nss_libs $MANUAL_NSPR_LIBS"
		else
			LDFLAGS="$LDFLAGS $LDFLAGS_NSPR $MANUAL_NSPR_LIBS"
		fi

		AC_LINK_IFELSE([AC_LANG_CALL([],[NSS_Init])],
		[moz_nss_libs="yes"],[moz_nss_libs="no"])
		LDFLAGS="$LDFLAGS_save"
		LIBS="$LIBS_save"

		if test "x${moz_nss_libs}" = "xyes"; then
			if test "x${enable_smime}" = "xyes"; then
				AC_DEFINE(ENABLE_SMIME,1,[Define if SMIME should be enabled])
				msg_smime="yes"
			fi
			if test -n "${with_nss_libs}"; then
				MANUAL_NSS_LIBS="-L$with_nss_libs $nsslibs"
			else
				MANUAL_NSS_LIBS="$nsslibs"
			fi
		else
			AC_MSG_FAILURE([NSS libs not found. Use --with-nss-libs to specify the libdir of NSS.])
		fi
	AC_MSG_RESULT([$moz_nss_libs])

	MANUAL_NSS_CFLAGS="$MANUAL_NSPR_CFLAGS $MANUAL_NSS_CFLAGS"
	MANUAL_NSS_LIBS="$MANUAL_NSPR_LIBS $MANUAL_NSS_LIBS"
fi

AM_CONDITIONAL(ENABLE_SMIME, test "x$msg_smime" != "xno")

dnl For pkgconfig files.
AC_SUBST(mozilla_nss)
AC_SUBST(mozilla_nspr)

AC_SUBST(MANUAL_NSPR_CFLAGS)
AC_SUBST(MANUAL_NSPR_LIBS)
AC_SUBST(MANUAL_NSS_CFLAGS)
AC_SUBST(MANUAL_NSS_LIBS)

dnl ******************************
dnl system mail stuff
dnl ******************************
AC_PATH_PROG(SENDMAIL, sendmail, [/usr/sbin/sendmail], [/usr/sbin:/usr/lib])
AC_DEFINE_UNQUOTED(SENDMAIL_PATH, "$SENDMAIL", [Path to a sendmail binary, or equivalent])

AC_MSG_CHECKING([system mail directory])
if test -d /var/mail -a '!' -h /var/mail ; then
	system_mail_dir="/var/mail"
else
	system_mail_dir="/var/spool/mail"
fi
AC_DEFINE_UNQUOTED(SYSTEM_MAIL_DIR, "$system_mail_dir", [Directory local mail is delivered to])

case `ls -ld $system_mail_dir 2>&1 | awk '{print $1;}'` in
	d??????rw?)
		CAMEL_LOCK_HELPER_USER=""
		CAMEL_LOCK_HELPER_GROUP=""
		system_mail_perm="world writable"
	;;
	d???rw????)
		CAMEL_LOCK_HELPER_USER=""
		CAMEL_LOCK_HELPER_GROUP=`ls -ld $system_mail_dir 2>&1 | awk '{print $4;}'`
		system_mail_perm="writable by group $CAMEL_LOCK_HELPER_GROUP"
	;;
	drw???????)
		CAMEL_LOCK_HELPER_USER=`ls -ld $system_mail_dir 2>&1 | awk '{print $3;}'`
		CAMEL_LOCK_HELPER_GROUP=""
		system_mail_perm="writable by user $CAMEL_LOCK_HELPER_USER"
	;;
	*)
		CAMEL_LOCK_HELPER_USER=""
		CAMEL_LOCK_HELPER_GROUP=""
		system_mail_perm="???"
	;;
esac

AC_MSG_RESULT([$system_mail_dir, $system_mail_perm])
AC_SUBST(CAMEL_LOCK_HELPER_USER)
AC_SUBST(CAMEL_LOCK_HELPER_GROUP)

dnl ******************************
dnl Timezone checks
dnl ******************************
AC_CACHE_CHECK(for tm_gmtoff in struct tm, [ac_cv_struct_tm_gmtoff],
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[	#include <time.h>	]],
	[[
		struct tm tm;
		tm.tm_gmtoff = 1;
	]]
	)],[ac_cv_struct_tm_gmtoff=yes],[ac_cv_struct_tm_gmtoff=no]))
if test "x$ac_cv_struct_tm_gmtoff" = "xyes"; then
	AC_DEFINE(HAVE_TM_GMTOFF, 1, [Define if struct tm has a tm_gmtoff member])
else
	AC_CACHE_CHECK(for timezone variable, [ac_cv_var_timezone],
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
		[[	#include <time.h> ]],
		[[	timezone = 1;	]]
		)],[ac_cv_var_timezone=yes],[ac_cv_var_timezone=no]))
	if test "x$ac_cv_var_timezone" = "xyes"; then
		AC_DEFINE(HAVE_TIMEZONE, 1, [Define if libc defines a timezone variable])
		AC_CACHE_CHECK(for altzone variable, [ac_cv_var_altzone],
			AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
			[[	#include <time.h>	]],
			[[	altzone = 1; ]]
			)],[ac_cv_var_altzone=yes],[ac_cv_var_altzone=no]))
		if test "x$ac_cv_var_altzone" = "xyes"; then
			AC_DEFINE(HAVE_ALTZONE, 1, [Define if libc defines an altzone variable])
		fi
	else
		AC_MSG_ERROR([unable to find a way to determine timezone])
	fi
fi

dnl ******************************
dnl ctime_r prototype
dnl ******************************
AC_CACHE_CHECK([if ctime_r wants three arguments], [ac_cv_ctime_r_three_args],
[
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[	#include <time.h> ]],
	[[	char *buf;
		time_t date;
		ctime_r (&date, buf, 100); ]]
	)],[ac_cv_ctime_r_three_args=yes],[ac_cv_ctime_r_three_args=no])
])

if test "x$ac_cv_ctime_r_three_args" = "xyes" ; then
	AC_DEFINE(CTIME_R_THREE_ARGS, 1, [Solaris-style ctime_r])
fi

dnl ******************************
dnl gethostbyname_r prototype
dnl ******************************
AC_CHECK_FUNCS(gethostbyname_r,[
AC_CACHE_CHECK([if gethostbyname_r wants five arguments], [ac_cv_gethostbyname_r_five_args],
[
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[
		#include "confdefs.h"
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>
		#include <netdb.h>
		#define BUFSIZE (sizeof(struct hostent)+10)
	]],
	[[
		struct hostent hent;
		char buffer[BUFSIZE];
		int bufsize=BUFSIZE;
		int h_errno;
		(void)gethostbyname_r ("www.ximian.com", &hent, buffer, bufsize, &h_errno);
	]]
	)],[ac_cv_gethostbyname_r_five_args=yes],[ac_cv_gethostbyname_r_five_args=no])
])])

if test "x$ac_cv_gethostbyname_r_five_args" = "xyes" ; then
	AC_DEFINE(GETHOSTBYNAME_R_FIVE_ARGS, 1, [Solaris-style gethostbyname_r])
fi

dnl ******************************
dnl gethostbyaddr_r prototype
dnl ******************************
AC_CHECK_FUNCS(gethostbyaddr_r,[
AC_CACHE_CHECK([if gethostbyaddr_r wants seven arguments], [ac_cv_gethostbyaddr_r_seven_args],
[
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[
		#include "confdefs.h"
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>
		#include <netdb.h>
		#define BUFSIZE (sizeof(struct hostent)+10)
	]],
	[[
		struct hostent hent;
		char buffer[BUFSIZE];
		int bufsize=BUFSIZE;
		int h_errno;
		(void)gethostbyaddr_r ("www.ximian.com", 14, AF_INET, &hent, buffer, bufsize, &h_errno); ]]
	)],[ac_cv_gethostbyaddr_r_seven_args=yes],[ac_cv_gethostbyaddr_r_seven_args=no])
])])

if test "x$ac_cv_gethostbyaddr_r_seven_args" = "xyes" ; then
	AC_DEFINE(GETHOSTBYADDR_R_SEVEN_ARGS, 1, [Solaris-style gethostbyaddr_r])
fi

dnl ******************************
dnl stat(v)fs location/type
dnl ******************************
AC_CHECK_HEADER([sys/statvfs.h],
	[AC_DEFINE([HAVE_SYS_STATVFS_H], 1, [Have <sys/statvfs.h>])],,
	[[	#if HAVE_SYS_STATVFS_H
		#include <sys/statvfs.h>
		#endif
	]])
AC_CHECK_FUNCS(statvfs)

AC_CHECK_HEADER([sys/param.h],
	[AC_DEFINE([HAVE_SYS_PARAM_H], 1, [Have <sys/param.h>])],,
	[[	#if HAVE_SYS_PARAM_H
		#include <sys/param.h>
		#endif
	]])
AC_CHECK_HEADER([sys/mount.h],
	[AC_DEFINE([HAVE_SYS_MOUNT_H], 1, [Have <sys/mount.h>])],,
	[[	#if HAVE_SYS_MOUNT_H
		#include <sys/mount.h>
		#endif
	]])
AC_CHECK_FUNCS(statfs)

dnl ******************************
dnl IPv6 support and getaddrinfo calls
dnl ******************************
AC_CACHE_CHECK([if system supports getaddrinfo and getnameinfo], [ac_cv_have_addrinfo],
[
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[	#include "confdefs.h"
		#include <sys/types.h>
		#include <sys/socket.h>
		#include <netinet/in.h>
		#include <netdb.h>
		#include <stddef.h>
	]],
	[[	#ifndef NI_MAXHOST
		#define NI_MAXHOST	1025
		#endif

		#ifndef NI_MAXSERV
		#define NI_MAXSERV	32
		#endif

		struct addrinfo hints, *res;
		struct sockaddr_in6 sin6;
		int af = AF_INET6;
		char host[NI_MAXHOST];
		char serv[NI_MAXSERV];

		getaddrinfo ("www.ximian.com", NULL, &hints, &res);
		freeaddrinfo (res);
		getnameinfo((struct sockaddr *)&sin6, sizeof(sin6), host, sizeof(host), serv, sizeof(serv), 0);	]]
	)],[ac_cv_have_addrinfo=yes],[ac_cv_have_addrinfo=no])
])

if test "x$ac_cv_have_addrinfo" = "xno" ; then
	AC_DEFINE(NEED_ADDRINFO,1,[Enable getaddrinfo emulation])
	if test "x$enable_ipv6" = "xyes" ; then
		AC_MSG_ERROR([system doesn't support necessary interfaces for ipv6 support])
	fi
	msg_ipv6="no"
else
	AC_ARG_ENABLE([ipv6],
	AS_HELP_STRING([--enable-ipv6=no/yes],
	[Enable support for resolving IPv6 addresses.]),
	[enable_ipv6=$enableval],[enable_ipv6=yes])
	if test "x$enable_ipv6" = "xyes"; then
		msg_ipv6="yes"
		AC_DEFINE(ENABLE_IPv6,1,[Enable IPv6 support])
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
		[[	#include "confdefs.h"
			#include <sys/types.h>
			#include <sys/socket.h>
			#include <netinet/in.h>
			#include <netdb.h>
		]],
		[[	struct addrinfo hints;
			hints.ai_flags = AI_ADDRCONFIG;
		]]
		)],[AC_DEFINE(HAVE_AI_ADDRCONFIG,1,[Define if the system defines the AI_ADDRCONFIG flag for getaddrinfo])],[])
	else
		msg_ipv6="no"
	fi
fi

AM_CONDITIONAL(ENABLE_IPv6, [test "x$enable_ipv6" = "xyes"])

AC_CHECK_HEADER([wspiapi.h],
	[AC_DEFINE([HAVE_WSPIAPI_H], 1, [Have <wspiapi.h>])],,)

dnl **********************************
dnl Weather calendar backend support
dnl **********************************
AC_MSG_CHECKING([if we should build the weather calendar backend])
AC_ARG_ENABLE([weather],
	[AS_HELP_STRING([--enable-weather],
	[Build the weather calendar backend (default=yes)])],
	[use_gweather=$enableval], [use_gweather=yes])

AC_MSG_RESULT([$use_gweather])
if test "x$use_gweather" = "xyes"; then
	PKG_CHECK_MODULES(
		[LIBGWEATHER], [gweather-3.0 >= gweather_minimum_version],[],
		[AC_MSG_ERROR([

	gweather-3.0 not found (or version < gweather_minimum_version)

	If you want to disable weather calendar support,
	please append --disable-weather to configure.

	])])
fi
AM_CONDITIONAL(ENABLE_WEATHER, [test $use_gweather = yes])

AC_DEFINE(HANDLE_LIBICAL_MEMORY, 1, [Define it once memory returned by libical is free'ed properly])

dnl ******************************
dnl File locking
dnl ******************************
AC_ARG_ENABLE([dot-locking],
	AS_HELP_STRING([--enable-dot-locking=yes/no],
	[Enable support for locking mail files with dot locking]),
	[enable_dot_locking=$enableval],[enable_dot_locking=yes])

if test "x$os_win32" != "xyes" -a "x$enable_dot_locking" = "xyes"; then
	AC_DEFINE(USE_DOT,1,[Define to use dot locking for mbox files])
	msg_dot="yes"
else
	msg_dot="no"
fi

AC_ARG_ENABLE([file-locking],
	AS_HELP_STRING([--enable-file-locking=fcntl/flock/no],
	[Enable support for locking mail files with file locking]),
	[],[enable_file_locking=fcntl])

if test "x$os_win32" != "xyes" -a "x$enable_file_locking" = "xfcntl"; then
	AC_DEFINE(USE_FCNTL,1,[Define to use fcntl locking for mbox files])
	msg_file="fcntl"
else
	if test "x$os_win32" != "xyes" -a "x$enable_file_locking" = "xflock"; then
		AC_DEFINE(USE_FLOCK,1,[Define to use flock locking for mbox files])
		msg_file="flock"
	else
		msg_file="no"
	fi
fi

dnl ******************************
dnl sendmail operation
dnl ******************************
AC_MSG_CHECKING([for SunOS broken spool format])
if test "x$host_os" = "xsunos" ; then
	with_broken_spool="yes"
fi

AC_ARG_WITH([broken-spool],
	AS_HELP_STRING([--with-broken-spool=yes/no],
	[Using SunOS/Solaris sendmail which has a broken spool format]),
	[with_broken_spool=$enableval],[with_broken_spool=${with_broken_spool:=no}])

if test "x$with_broken_spool" = "xyes"; then
	AC_DEFINE(HAVE_BROKEN_SPOOL,1,[Define if mail delivered to the system mail directory is in broken Content-Length format])
fi

AC_MSG_RESULT([$with_broken_spool])

dnl ***********
dnl Kerberos 5
dnl ***********
EVO_KRB5_SUPPORT(no)

dnl ******************************
dnl Purify support
dnl ******************************
EVO_PURIFY_SUPPORT

dnl ******************************
dnl LDAP support.
dnl ******************************
EVO_LDAP_CHECK(no)
case $with_openldap in
no)
	msg_ldap="no"
;;
*)
	case $with_static_ldap in
	yes)
		msg_ldap="$with_openldap (static)"
		;;
	*)
		msg_ldap="$with_openldap (dynamic)"
		;;
	esac
esac

dnl SunLDAP support, but if OpenLDAP support, skip check SunLDAP.
if test "x$with_openldap" = "xno" ; then
	EVO_SUNLDAP_CHECK(no)
	case $with_sunldap in
	no)
		msg_ldap="no"
		;;
	*)
		case $with_static_sunldap in
		yes)
			msg_ldap="$with_sunldap (static)"
			;;
		*)
			msg_ldap="$with_sunldap (dynamic)"
			;;
	esac
esac
else
	with_sunldap="no"
fi
AM_CONDITIONAL(SUNLDAP, [test "$with_sunldap" != no])

LDAP_CFLAGS="$LDAP_CFLAGS -DLDAP_DEPRECATED"

dnl ******************************
dnl Utility macro to set compiler flags for a specific lib.
dnl EVO_SET_COMPILE_FLAGS(VAR-PREFIX, DEPS, EXTRA-CFLAGS, EXTRA-LIBS)
dnl ******************************
AC_DEFUN([EVO_SET_COMPILE_FLAGS], [
	PKG_CHECK_MODULES([$1], [$2])
	$1_CFLAGS="[$]$1_CFLAGS $3 "
	$1_LIBS="[$]$1_LIBS $4 "
])

dnl ******************************
dnl sqlite3 flags
dnl ******************************
PKG_CHECK_MODULES(SQLITE3, [sqlite3 >= sqlite_minimum_version])

if test "x$enable_maintainer_mode" = "xyes" ; then
	EVO_SET_COMPILE_FLAGS(FACTORY_GTK, "gtk+-3.0")
	AC_SUBST(FACTORY_GTK_CFLAGS)
	AC_SUBST(FACTORY_GTK_LIBS)
fi

dnl ******************************
dnl libedataserver flags
dnl ******************************
E_DATA_SERVER_DEPS="gio-2.0 gmodule-2.0 libsecret-1 libxml-2.0 libsoup-2.4 $mozilla_nspr"

EVO_SET_COMPILE_FLAGS(E_DATA_SERVER, $E_DATA_SERVER_DEPS, $MANUAL_NSPR_CFLAGS, $MANUAL_NSPR_LIBS)
AC_SUBST(E_DATA_SERVER_CFLAGS)
AC_SUBST(E_DATA_SERVER_LIBS)

dnl ******************************
dnl libebackend flags
dnl ******************************
E_BACKEND_DEPS="gio-2.0 gmodule-2.0 libsecret-1 libsoup-2.4 libxml-2.0"

EVO_SET_COMPILE_FLAGS(E_BACKEND, $E_BACKEND_DEPS)
AC_SUBST(E_BACKEND_CFLAGS)
AC_SUBST(E_BACKEND_LIBS)

dnl ******************************
dnl evolution-addressbook flags
dnl ******************************
EVOLUTION_ADDRESSBOOK_DEPS="gio-2.0 libxml-2.0 libsoup-2.4 libsecret-1"

EVO_SET_COMPILE_FLAGS(EVOLUTION_ADDRESSBOOK, $EVOLUTION_ADDRESSBOOK_DEPS)
AC_SUBST(EVOLUTION_ADDRESSBOOK_CFLAGS)
AC_SUBST(EVOLUTION_ADDRESSBOOK_LIBS)

dnl ******************************
dnl evolution-calendar flags
dnl ******************************
EVOLUTION_CALENDAR_DEPS="gio-2.0 libical >= libical_minimum_version libsoup-2.4 libxml-2.0 libsecret-1"

if `$PKG_CONFIG --atleast-version=2.0 libical`; then
	AC_DEFINE(HAVE_LIBICAL_2_0, 1, [Define if compiled with libical 2.0])
fi

dnl *****
dnl libical.pc from libical-0.43 has a bug in it's CFlags.
dnl It wants apps to include <libical/ical*.h> but it's CFlags make it difficult
dnl to differentiate between <libical/ical.h> and <ical.h>
dnl We have fixed all our instances to use <libical/ical.h>. Until the .pc from
dnl libical is fixed, we have to work-around the buggy CFlags.
dnl *****
	LIBICAL_EXTRA_CFLAGS=" -I`$PKG_CONFIG --variable=includedir libical` "
	LIBICAL_EXTRA_LIBS=""
	AC_SUBST(LIBICAL_EXTRA_CFLAGS)
	AC_SUBST(LIBICAL_EXTRA_LIBS)

	EVO_SET_COMPILE_FLAGS(EVOLUTION_CALENDAR, $EVOLUTION_CALENDAR_DEPS, $LIBICAL_EXTRA_CFLAGS, $LIBICAL_EXTRA_LIBS)
	AC_SUBST(EVOLUTION_CALENDAR_CFLAGS)
	AC_SUBST(EVOLUTION_CALENDAR_LIBS)

dnl ****************************
dnl Check for ical_set_unknown_token_handling_setting function
dnl ****************************
AC_MSG_CHECKING([ical_set_unknown_token_handling_setting function])
save_cflags=$CFLAGS; CFLAGS=$EVOLUTION_CALENDAR_CFLAGS
save_libs=$LIBS; LIBS="$EVOLUTION_CALENDAR_LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[#include <libical/ical.h>]],
	[[ical_set_unknown_token_handling_setting (ICAL_DISCARD_TOKEN)]])],
	[AC_DEFINE(HAVE_ICAL_UNKNOWN_TOKEN_HANDLING, 1, [libical provides ical_set_unknown_token_handling_setting function]) ac_cv_have_iuth=yes],[ac_cv_have_iuth=no])
CFLAGS=$save_cflags
LIBS=$save_libs
AC_MSG_RESULT([$ac_cv_have_iuth])

dnl ****************************
dnl Check for icaltzutil_set_exact_vtimezones_support function
dnl ****************************
AC_MSG_CHECKING([icaltzutil_set_exact_vtimezones_support function])
save_cflags=$CFLAGS; CFLAGS="$EVOLUTION_CALENDAR_CFLAGS"
save_libs=$LIBS; LIBS="$EVOLUTION_CALENDAR_LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[#include <libical/ical.h>]],
	[[icaltzutil_set_exact_vtimezones_support (0)]])],
	[AC_DEFINE(HAVE_ICALTZUTIL_SET_EXACT_VTIMEZONES_SUPPORT, 1, [libical provides icaltzutil_set_exact_vtimezones_support function]) ac_cv_have_isevs=yes],[ac_cv_have_isevs=no])
CFLAGS=$save_cflags
LIBS=$save_libs
AC_MSG_RESULT([$ac_cv_have_isevs])

dnl ******************************
dnl Google flags
dnl ******************************
have_gdata_0_15_1=no
LIBGDATA_REQUIRED=libgdata_minimum_version
AC_SUBST(LIBGDATA_REQUIRED)
AC_ARG_ENABLE([google],
	[AS_HELP_STRING([--enable-google],
	[enable Google Contacts support (default=yes)])],
	[enable_google=$enableval], [enable_google=yes])
AC_MSG_CHECKING([if Google Contacts support is enabled])
AC_MSG_RESULT([$enable_google])
if test "x$enable_google" = xyes; then
	EVO_SET_COMPILE_FLAGS(GDATA, libgdata >= libgdata_minimum_version)
	AC_SUBST(GDATA_CFLAGS)
	AC_SUBST(GDATA_LIBS)

	AC_DEFINE(HAVE_GOOGLE, 1, [Define to 1 if you have the google-1.0 package.])

	if `$PKG_CONFIG --atleast-version=0.15.1 libgdata`; then
		have_gdata_0_15_1=yes
	fi

	AC_MSG_CHECKING([for libgdata GTasks pagination functions])
	save_cflags=$CFLAGS; CFLAGS="$GDATA_CFLAGS"
	save_libs=$LIBS; LIBS="$GDATA_LIBS"
	AC_LINK_IFELSE([AC_LANG_PROGRAM(
		[[#include <gdata/gdata.h>]],
		[[gdata_feed_get_next_page_token (NULL); gdata_query_next_page (NULL);]])],
		[AC_DEFINE(HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS, 1, [Define to 1 when the libgdata provides GTasks pagination functions]) ac_cv_have_gdgtpf=yes],[ac_cv_have_gdgtpf=no])
	CFLAGS=$save_cflags
	LIBS=$save_libs
	AC_MSG_RESULT([$ac_cv_have_gdgtpf])
fi
AM_CONDITIONAL(HAVE_GOOGLE, [test x$enable_google = xyes])
AM_CONDITIONAL(HAVE_GDATA_0_15_1, [test x$have_gdata_0_15_1 = xyes])

EVO_SET_COMPILE_FLAGS(SOUP, libsoup-2.4)
AC_SUBST(SOUP_CFLAGS)
AC_SUBST(SOUP_LIBS)

dnl ******************************
dnl Camel flags
dnl ******************************
SQLITE_REQUIRED=sqlite_minimum_version
AC_SUBST(SQLITE_REQUIRED)

zlib_found="false"
dnl deflateInit is a #define, use deflateEnd instead
AC_CHECK_LIB([z], [deflateEnd], [AC_CHECK_HEADER(zlib.h, [zlib_found=true])])

if test "x$zlib_found" = "xfalse"; then
	AC_MSG_ERROR([*** zlib is required])
fi

dnl ******************************
dnl Checks for large file support
dnl ******************************
AC_ARG_ENABLE([largefile],
	AS_HELP_STRING([--enable-largefile],
	[enable support for large files [[default=yes]]]),
	[],[enable_largefile="yes"])

if test "x$enable_largefile" != "xno"; then
	AC_SYS_LARGEFILE
	AC_CACHE_CHECK([for _LARGEFILE64_SOURCE value needed for large files], [ac_cv_largefile64_source],
	[AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[	#include <sys/types.h>
		#include <sys/stat.h>
		#include <fcntl.h>
	]],
	[[	int fd = open ("__o_largefile", O_CREAT | O_RDWR | O_LARGEFILE, 0644);	]]
	)],[ac_cv_largefile64_source="no"],
		[AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
		[[	#define _LARGEFILE64_SOURCE
			#include <sys/types.h>
			#include <sys/stat.h>
			#include <fcntl.h>
		]],
		[[ int fd = open ("__o_largefile", O_CREAT | O_RDWR | O_LARGEFILE, 0644); ]])],[ac_cv_largefile64_source="yes"],
		[ac_cv_largefile64_source="unknown"])])
	])

	enable_largefile="no"

	if test "x$ac_cv_largefile64_source" = "xyes"; then
		LARGEFILE_CFLAGS="-D_LARGEFILE64_SOURCE"
		enable_largefile="yes"
	elif test "x$ac_cv_largefile64_source" = "xunknown"; then
		AC_DEFINE(O_LARGEFILE, 0, [Define to 0 if your system does not have the O_LARGEFILE flag])
	fi

	if test -n "$ac_cv_sys_large_files" -a "x$ac_cv_sys_large_files" != "xno"; then
		LARGEFILE_CFLAGS="$LARGEFILE_CFLAGS -D_LARGE_FILES"
		enable_largefile="yes"
	fi

	if test "x$ac_cv_sys_file_offset_bits" != "xno"; then
		LARGEFILE_CFLAGS="$LARGEFILE_CFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
		enable_largefile="yes"
	fi
else
	AC_DEFINE(O_LARGEFILE, 0, [Define to 0 if your system does not have the O_LARGEFILE flag])
	LARGEFILE_CFLAGS=""
fi

AM_CONDITIONAL(ENABLE_LARGEFILE, [test "x$enable_largefile" = "xyes"])

EVO_SET_COMPILE_FLAGS(CAMEL, gio-2.0 gmodule-2.0 $mozilla_nss $mozilla_nspr sqlite3 >= sqlite_minimum_version, $KRB5_CFLAGS $MANUAL_NSS_CFLAGS $MANUAL_NSPR_CFLAGS $LARGEFILE_CFLAGS $ICU_CFLAGS, -lz $KRB5_LIBS $MANUAL_NSS_LIBS $MANUAL_NSPR_LIBS $ICU_LIBS)
AC_SUBST(CAMEL_CFLAGS)
AC_SUBST(CAMEL_LIBS)

dnl ******************************
dnl Special directories
dnl ******************************

dnl If you add something here, consider whether or not you also
dnl need to add it to one or more .pc.in files (for Connector,
dnl etc)

privdatadir='${datadir}'/evolution-data-server
AC_SUBST(privdatadir)

privincludedir='${includedir}'/evolution-data-server
AC_SUBST(privincludedir)

privlibdir='${libdir}'/evolution-data-server
AC_SUBST(privlibdir)

privlibexecdir='${libexecdir}'/evolution-data-server
AC_SUBST(privlibexecdir)

if test "$os_win32" = yes; then
	dnl On Win32 there is no "rpath" mechanism. We install the private
	dnl shared libraries in $libdir, meaning the DLLs will actually be in
	dnl $bindir. This means just having $bindir in PATH will be enough.
	privsolibdir=$libdir
else
	privsolibdir=$privlibdir
fi
AC_SUBST(privsolibdir)

imagesdir='${datadir}'/pixmaps/evolution-data-server
AC_SUBST(imagesdir)

moduledir='${privlibdir}'/registry-modules
AC_SUBST(moduledir)

credentialmoduledir='${privlibdir}'/credential-modules
AC_SUBST(credentialmoduledir)

ebook_backenddir='${privlibdir}'/addressbook-backends
AC_SUBST(ebook_backenddir)

ecal_backenddir='${privlibdir}'/calendar-backends
AC_SUBST(ecal_backenddir)

ro_sourcesdir='${privdatadir}'/ro-sources
AC_SUBST(ro_sourcesdir)

rw_sourcesdir='${privdatadir}'/rw-sources
AC_SUBST(rw_sourcesdir)

if test "x$use_gweather" = "xyes"; then
	weatherdatadir="$privdatadir/weather"
	AC_SUBST(weatherdatadir)
fi

dnl separate camel from e-d-s? or should it be under the same spot? same for now.
camel_providerdir='${privlibdir}'/camel-providers
AC_SUBST(camel_providerdir)

dnl *******************
dnl D-BUS service stuff
dnl *******************
servicelibexecdir=${libexecdir}
if test x$os_win32 = xyes -a "x$WIN32_SERVICELIBEXECDIR" != "x" ; then
	dnl
	dnl D-Bus requires Windows full path, not Unix-like, like the one provided by MSYS
	dnl
	AC_SUBST(WIN32_SERVICELIBEXECDIR)
	servicelibexecdir="$WIN32_SERVICELIBEXECDIR"
fi
AC_SUBST(servicelibexecdir)
m4_pattern_allow([AM_V_GEN])
EVO_SUBST_SERVICE_RULE='%.service: %.service.in Makefile ; $(AM_V_GEN) sed -e "s|\@libexecdir\@|$(servicelibexecdir)|" -e s"|\@ADDRESS_BOOK_DBUS_SERVICE_NAME\@|$(ADDRESS_BOOK_DBUS_SERVICE_NAME)|" -e "s|\@CALENDAR_DBUS_SERVICE_NAME\@|$(CALENDAR_DBUS_SERVICE_NAME)|" -e "s|\@SOURCES_DBUS_SERVICE_NAME\@|$(SOURCES_DBUS_SERVICE_NAME)|" -e "s|\@USER_PROMPTER_DBUS_SERVICE_NAME\@|$(USER_PROMPTER_DBUS_SERVICE_NAME)|" $< > $@'
AC_SUBST(EVO_SUBST_SERVICE_RULE)

AC_ARG_WITH([dbus-service-dir],
	AS_HELP_STRING([--with-dbus-service-dir=PATH],
	[Set directory for D-Bus service files]
	[[default=${datadir}/dbus-1/services]]),
	[dbus_servicedir="$withval"],
	[dbus_servicedir='${datadir}/dbus-1/services'])
AC_SUBST([dbus_servicedir])

# It is correct for this to be in ${prefix}/lib, even on systems where that
# does not match ${libdir}. This is what systemd uses on such platforms.
AC_ARG_WITH([systemduserunitdir],
	AS_HELP_STRING([--with-systemduserunitdir=PATH],
	[Set directory for systemd user units, or 'no' to disable]
	[[default=${prefix}/lib/systemd/user]]),
	[systemd_userdir="$withval"],
	[systemd_userdir='${prefix}/lib/systemd/user'])
AC_SUBST([systemd_userdir])
AM_CONDITIONAL([WITH_SYSTEMD_USER_UNITS], [test "x$systemd_userdir" != "xno"])

dnl ******************************
dnl GLib stuff
dnl ******************************
GLIB_GSETTINGS
AM_PATH_GLIB_2_0

dnl ******************************
dnl gperf stuff
dnl ******************************
AC_PATH_PROG(GPERF, gperf, [no])
if test "x$GPERF" = "xno"; then
  AC_MSG_ERROR([You need gperf to build evolution-data-server])
fi

GOBJECT_INTROSPECTION_CHECK([0.9.12])

dnl ******************************
dnl Vala binding
dnl ******************************
AC_ARG_ENABLE(vala-bindings,
        AC_HELP_STRING([--enable-vala-bindings],
        [build the EXPERIMENTAL Vala bindings]),
        enable_vala_bindings=$enableval,
        enable_vala_bindings=no)

if test x$enable_vala_bindings = xyes; then
  if test "x$INTROSPECTION_SCANNER" = x; then
    AC_MSG_ERROR([GObject-Introspection must be enabled for Vala bindings])
  fi

  AM_PROG_VALAC([0.22.0])
  have_vala=yes

  AC_PATH_PROG(VALAC, valac, valac)
  AC_SUBST(VALAC)
  AC_SUBST(VALAFLAGS)

  AC_PATH_PROG([VAPIGEN], [vapigen], [false])

  if test "x$VAPIGEN" = "xfalse"; then
    AC_MSG_ERROR([vapigen not found. Was vala compiled with --enable-vapigen?])
  fi

  AC_SUBST(VAPIGEN)
fi

AM_CONDITIONAL([HAVE_VALA], [test "x$have_vala" = "xyes"])
AC_SUBST(HAVE_VALA)

dnl ******************************
dnl Makefiles
dnl ******************************
export privlibdir
export privincludedir
export privdatadir

AC_CONFIG_FILES([
Makefile
evolution-data-server.pc
addressbook/Makefile
addressbook/libebook/Makefile
addressbook/libebook/libebook.pc
addressbook/libebook-contacts/Makefile
addressbook/libebook-contacts/libebook-contacts.pc
addressbook/libedata-book/Makefile
addressbook/libedata-book/libedata-book.pc
addressbook/libegdbus/Makefile
addressbook/backends/Makefile
addressbook/backends/file/Makefile
addressbook/backends/ldap/Makefile
addressbook/backends/google/Makefile
addressbook/backends/google/tests/Makefile
addressbook/backends/webdav/Makefile
art/Makefile
calendar/Makefile
calendar/libecal/Makefile
calendar/libecal/libecal.pc
calendar/libedata-cal/Makefile
calendar/libedata-cal/libedata-cal.pc
calendar/libegdbus/Makefile
calendar/backends/Makefile
calendar/backends/caldav/Makefile
calendar/backends/file/Makefile
calendar/backends/gtasks/Makefile
calendar/backends/http/Makefile
calendar/backends/contacts/Makefile
calendar/backends/weather/Makefile
camel/Makefile
camel/providers/Makefile
camel/providers/imapx/Makefile
camel/providers/local/Makefile
camel/providers/nntp/Makefile
camel/providers/pop3/Makefile
camel/providers/sendmail/Makefile
camel/providers/smtp/Makefile
camel/tests/Makefile
camel/tests/folder/Makefile
camel/tests/lib/Makefile
camel/tests/message/Makefile
camel/tests/mime-filter/Makefile
camel/tests/misc/Makefile
camel/tests/smime/Makefile
camel/camel.pc
data/Makefile
libebackend/Makefile
libebackend/libebackend.pc
libedataserver/Makefile
libedataserver/eds-version.h
libedataserver/libedataserver.pc
libedataserverui/Makefile
libedataserverui/libedataserverui.pc
modules/Makefile
modules/cache-reaper/Makefile
modules/gnome-online-accounts/Makefile
modules/google-backend/Makefile
modules/outlook-backend/Makefile
modules/owncloud-backend/Makefile
modules/secret-monitor/Makefile
modules/ubuntu-online-accounts/Makefile
modules/ubuntu-online-accounts/calendar.service-type.in
modules/ubuntu-online-accounts/contacts.service-type.in
modules/ubuntu-online-accounts/evolution-data-server.application.in
modules/ubuntu-online-accounts/evolution-data-server-uoa.desktop.in
modules/ubuntu-online-accounts/google-calendar.service.in
modules/ubuntu-online-accounts/google-contacts.service.in
modules/ubuntu-online-accounts/google-gmail.service.in
modules/ubuntu-online-accounts/mail.service-type.in
modules/ubuntu-online-accounts/windows-live-mail.service.in
modules/ubuntu-online-accounts/yahoo-calendar.service.in
modules/ubuntu-online-accounts/yahoo-mail.service.in
modules/trust-prompt/Makefile
modules/yahoo-backend/Makefile
private/Makefile
services/Makefile
services/evolution-addressbook-factory/Makefile
services/evolution-calendar-factory/Makefile
services/evolution-source-registry/Makefile
services/evolution-user-prompter/Makefile
tools/Makefile
tools/addressbook-export/Makefile
tools/addressbook-export/csv2vcard
tests/Makefile
tests/libedata-book/Makefile
tests/libebook/Makefile
tests/libebook-contacts/Makefile
tests/libebook/client/Makefile
tests/libebook/vcard/Makefile
tests/book-migration/Makefile
tests/book-migration/db/Makefile
tests/libecal/Makefile
tests/libecal/client/Makefile
tests/libedata-cal/Makefile
tests/libedataserver/Makefile
tests/test-server-utils/Makefile
tests/test-server-utils/services/Makefile
tests/test-server-utils/services/org.gnome.evolution.dataserver.AddressBook.service
tests/test-server-utils/services/org.gnome.evolution.dataserver.Calendar.service
tests/test-server-utils/services/org.gnome.evolution.dataserver.Sources.service
examples/Makefile
examples/cursor/Makefile
docs/Makefile
docs/reference/Makefile
docs/reference/camel/Makefile
docs/reference/private/Makefile
docs/reference/eds/Makefile
docs/reference/eds/version.xml
po/Makefile.in
vala/Makefile
])
AC_OUTPUT

echo "
	evolution-data-server has been configured as follows:
	Weather calendar:	$use_gweather
	Mail Directory:		$system_mail_dir, $system_mail_perm
	LDAP support:		$msg_ldap
	Kerberos 5:		$msg_krb5
	SMIME support:		$msg_smime
	IPv6 support:		$msg_ipv6
	Phone number support:	$msg_phonenumber
	Dot Locking:		$msg_dot
	File Locking:		$msg_file
	Large files:		$enable_largefile
	Gtk Doc:		$enable_gtk_doc
	Introspection:		$enable_introspection
	Vala bindings:		$enable_vala_bindings
	GNOME Online Accounts	$enable_goa
	Ubuntu Online Accounts	$enable_uoa
	Google Authentication	$enable_google_auth
	Google Contacts		$enable_google
	Google Tasks		$have_gdata_0_15_1
	GTK+:  			$enable_gtk
	Examples:  		$enable_examples
	Code coverage (gcov):	$enable_code_coverage
	Installed tests:        $enable_installed_tests
	Backend per process	$enable_backend_per_process
"

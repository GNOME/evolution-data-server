dnl Evolution-Data-Server version
m4_define([eds_major_version], [2])
m4_define([eds_minor_version], [31])
m4_define([eds_micro_version], [1])

m4_define([eds_version],
	[eds_major_version.eds_minor_version.eds_micro_version])

dnl Base Version: This is for API/version tracking for things like
dnl Bonobo server files.  This should always be the major/minor of
dnl the stable version or stable version to be.  Note, this is set
dnl the way it is so that GETTEXT_PACKAGE will be parsed correctly.
BASE_VERSION=3.0
m4_define([base_version], [3.0])

dnl This number is meaningless, but we're now stuck with it in our
dnl library names for backward compatibility.
m4_define([api_version], [1.2])

dnl Autoconf / Automake Initialization
AC_PREREQ(2.58)
AC_INIT([evolution-data-server],[eds_version],[http://bugzilla.gnome.org/enter_bug.cgi?product=Evolution-Data-Server])
AM_INIT_AUTOMAKE([gnu 1.9 tar-ustar -Wall -Werror foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR(README)
AC_CONFIG_HEADERS(config.h)

dnl This is for the autoconf tests only - it set's the language we use
AC_LANG(C)

dnl Automake 1.11 - Silent Build Rules
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Required Package Versions
m4_define([glib_minimum_version], [2.16.1])
m4_define([gtk_minimum_version], [2.18.0])
m4_define([gconf_minimum_version], [2.0.0])		dnl XXX Just a Guess
m4_define([libxml_minimum_version], [2.0.0])		dnl XXX Just a Guess
m4_define([libsoup_minimum_version], [2.3.0])
m4_define([gnome_keyring_minimum_version], [2.20.1])
m4_define([sqlite_minimum_version], [3.5])
m4_define([gweather_minimum_version], [2.25.4])
m4_define([libical_minimum_version], [0.43])
m4_define([dbus_glib_minimum_version], [0.6])

AC_SUBST([BASE_VERSION],[base_version])
AC_SUBST([API_VERSION],[api_version])
AC_DEFINE_UNQUOTED(BASE_VERSION, ["$BASE_VERSION"], [Base version (Major.Minor)])
AC_DEFINE_UNQUOTED(API_VERSION, ["$API_VERSION"], [API version (Major.Minor)])

EDS_MAJOR_VERSION=eds_major_version
EDS_MINOR_VERSION=eds_minor_version
EDS_MICRO_VERSION=eds_micro_version

dnl ******************************
dnl Libtool versioning
dnl ******************************
LIBEDATASERVER_CURRENT=12
LIBEDATASERVER_REVISION=0
LIBEDATASERVER_AGE=0

LIBEDATASERVERUI_CURRENT=10
LIBEDATASERVERUI_REVISION=0
LIBEDATASERVERUI_AGE=0

LIBECAL_CURRENT=9
LIBECAL_REVISION=2
LIBECAL_AGE=2

LIBEDATACAL_CURRENT=7
LIBEDATACAL_REVISION=0
LIBEDATACAL_AGE=0

LIBEDATABOOK_CURRENT=6
LIBEDATABOOK_REVISION=1
LIBEDATABOOK_AGE=4

LIBEBOOK_CURRENT=12
LIBEBOOK_REVISION=1
LIBEBOOK_AGE=3

LIBEGROUPWISE_CURRENT=13
LIBEGROUPWISE_REVISION=1
LIBEGROUPWISE_AGE=0

LIBCAMEL_CURRENT=15
LIBCAMEL_REVISION=0
LIBCAMEL_AGE=0

LIBGDATA_CURRENT=1
LIBGDATA_REVISION=0
LIBGDATA_AGE=0

LIBGDATA_GOOGLE_CURRENT=1
LIBGDATA_GOOGLE_REVISION=0
LIBGDATA_GOOGLE_AGE=0

LIBEBACKEND_CURRENT=0
LIBEBACKEND_REVISION=1
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
AC_SUBST(LIBEDATABOOK_CURRENT)
AC_SUBST(LIBEDATABOOK_REVISION)
AC_SUBST(LIBEDATABOOK_AGE)
AC_SUBST(LIBEGROUPWISE_CURRENT)
AC_SUBST(LIBEGROUPWISE_REVISION)
AC_SUBST(LIBEGROUPWISE_AGE)
AC_SUBST(LIBCAMEL_CURRENT)
AC_SUBST(LIBCAMEL_REVISION)
AC_SUBST(LIBCAMEL_AGE)
AC_SUBST(LIBGDATA_CURRENT)
AC_SUBST(LIBGDATA_REVISION)
AC_SUBST(LIBGDATA_AGE)
AC_SUBST(LIBGDATA_GOOGLE_CURRENT)
AC_SUBST(LIBGDATA_GOOGLE_REVISION)
AC_SUBST(LIBGDATA_GOOGLE_AGE)
AC_SUBST(LIBEBACKEND_CURRENT)
AC_SUBST(LIBEBACKEND_REVISION)
AC_SUBST(LIBEBACKEND_AGE)

dnl **************************************
dnl Put the ACLOCAL flags in the Makefile
dnl **************************************
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

dnl ******************************
dnl Compiler Warning Flags
dnl ******************************
AS_COMPILER_FLAGS(WARNING_FLAGS,
	"-DG_DISABLE_DEPRECATED
	-DPANGO_DISABLE_DEPRECATED
	-DGDK_DISABLE_DEPRECATED
	-DGDK_PIXBUF_DISABLE_DEPRECATED
	-DGTK_DISABLE_DEPRECATED
	-DG_DISABLE_SINGLE_INCLUDES
	-DGTK_DISABLE_SINGLE_INCLUDES
	-Wall -Wextra
	-Wno-missing-field-initializers
	-Wno-sign-compare
	-Wno-unused-parameter
	-Wdeclaration-after-statement
	-Werror-implicit-function-declaration
	-Wformat-security -Winit-self
	-Wmissing-declarations -Wmissing-include-dirs
	-Wmissing-noreturn -Wnested-externs -Wpointer-arith
	-Wredundant-decls -Wundef -Wwrite-strings")
AC_SUBST(WARNING_FLAGS)

dnl Other useful compiler warnings for test builds only.
dnl These may produce warnings we have no control over,
dnl or false positives we don't always want to see.
dnl
dnl	-Wformat-nonliteral
dnl	-Wmissing-format-attribute
dnl	-Wshadow
dnl	-Wstrict-aliasing=2

dnl GCC 4.4 got more aggressive in its aliasing optimizations, changing
dnl behavior that -- according to the C99 standard -- is supposed to be
dnl undefined.  We may still have aliasing abuses lying around that rely
dnl on GCC's previous "undefined" behavior, so disable strict-aliasing
dnl optimization until we can find and fix all the abuses.
dnl XXX This really belongs in AM_CFLAGS.
AM_CPPFLAGS="$WARNING_FLAGS -fno-strict-aliasing"
AC_SUBST(AM_CPPFLAGS)

LDFLAGS="$LDFLAGS -Wl,--no-undefined"

dnl ******************************
dnl Initialize maintainer mode
dnl ******************************
AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_CPP
AC_C_INLINE
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_LEX
AC_PROG_YACC
case $YACC in
*yacc*)
	AC_MSG_ERROR([You need bison to build evolution-data-server])
	;;
esac

dnl ******************************
dnl Test whether jw is installed
dnl ******************************
AC_PATH_PROG(JW,jw,no)
if test "x$JW" = "xno"; then
	HAVE_JW="no"
else
	HAVE_JW="yes"
fi
AM_CONDITIONAL(HAVE_JW, test "x$HAVE_JW" = "xyes")
AC_SUBST(HAVE_JW)

dnl ******************************
dnl I18N stuff
dnl ******************************
IT_PROG_INTLTOOL([0.35.5])

AM_GLIB_GNU_GETTEXT

GETTEXT_PACKAGE=evolution-data-server-$BASE_VERSION
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Package name for gettext])

localedir='$(prefix)/$(DATADIRNAME)/locale'
AC_SUBST(localedir)

dnl ******************************
dnl Initialize libtool
dnl ******************************
LT_PREREQ(2.2)
LT_INIT(disable-static win32-dll)

PKG_PROG_PKG_CONFIG

dnl ******************************
dnl Gtk Doc stuff
dnl ******************************
GTK_DOC_CHECK(1.9)

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
	AC_DEFINE(_WIN32_WINNT, 0x501, [To get getaddrinfo etc declarations])
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
	[glib-2.0 >= glib_minimum_version
	gtk+-2.0 >= gtk_minimum_version
	gconf-2.0 >= gconf_minimum_version
	libxml-2.0 >= libxml_minimum_version
	libsoup-2.4 >= libsoup_minimum_version])

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
	[libdb_prefix="$withval"], [libdb_prefix='${prefix}'])

DB_CFLAGS="-I$libdb_prefix/include"
DB_LIBS="-L$libdb_prefix/lib -ldb"

AC_MSG_CHECKING([Berkeley DB])
save_cflags=$CFLAGS; CFLAGS=$DB_CFLAGS
save_libs=$LIBS; LIBS="$DB_LIBS"
AC_LINK_IFELSE([AC_LANG_PROGRAM(
	[[#include <db.h>]],
	[[db_create(NULL, NULL, 0)]])],
	[AC_MSG_RESULT([yes])],
	[AC_MSG_ERROR([Cannot find libdb])])
CFLAGS=$save_cflags
LIBS=$save_libs
AC_SUBST(DB_CFLAGS)
AC_SUBST(DB_LIBS)

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

CFLAGS="$CFLAGS -I$srcdir"

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

dnl **********************************
dnl Check for nl_langinfo and CODESET
dnl **********************************
AC_MSG_CHECKING([for nl_langinfo (CODESET)])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[[#include <langinfo.h>]],
	[[char *codeset = nl_langinfo (CODESET);]]
	)],[ac_cv_langinfo_codeset=yes],[ac_cv_langinfo_codeset=no])
if test "x$ac_cv_langinfo_codeset" = "xyes"; then
	AC_DEFINE(HAVE_CODESET, 1, [Have nl_langinfo (CODESET)])
fi
AC_MSG_RESULT([$ac_cv_langinfo_codeset])

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
msg_ssl="no"
msg_smime="no"

AC_ARG_ENABLE([ssl],
	AS_HELP_STRING([--enable-ssl],
	[Enable SSL support through Mozilla nss @<:@default=yes@:>@]),
	[enable_ssl="$enableval"],[enable_ssl="yes"])

AC_ARG_ENABLE([smime],
	AS_HELP_STRING([--enable-smime],
	[Enable SMIME support through Mozilla nss @<:@default=yes,requires --enable-ssl@:>@ ]),
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

if test "x${enable_ssl}" = "xyes"; then
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
			msg_ssl="yes"
			if test "x$enable_smime" = "xyes"; then
				AC_DEFINE(ENABLE_SMIME,1,[Define if SMIME should be enabled])
				msg_smime="yes"
			fi
			AC_DEFINE(HAVE_NSS,1,[Define if you have NSS])
			AC_DEFINE(HAVE_SSL,1,[Define if you have a supported SSL library])
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
			CPPFLAGS="$CPPFLAGS -I$with_nspr_includes"

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
			nsprlibs="$DL_LIB -lplc4 -lplds4 -lnspr4 $PTHREAD_LIB"

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
				AC_DEFINE(HAVE_NSS)
				AC_DEFINE(HAVE_SSL)
				msg_ssl="yes"
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

dnl ******************************
dnl Calendar support
dnl ******************************
AC_MSG_CHECKING([if we should build the calendar])
AC_ARG_ENABLE([calendar],
	AS_HELP_STRING([--enable-calendar=yes/no],
	[Enable the calendar components (default yes)]),
	[enable_calendar=$enableval], [enable_calendar=yes])

if test "x${enable_calendar}" != "xno"; then
	AC_DEFINE(ENABLE_CALENDAR, 1, [Define if Calendar should be built])
fi
AM_CONDITIONAL(ENABLE_CALENDAR, [test x$enable_calendar = xyes])
AC_MSG_RESULT([$enable_calendar])
msg_calendar=$enable_calendar

dnl **********************************
dnl Weather calendar backend support
dnl **********************************
AC_MSG_CHECKING([if we should build the weather calendar backend])
AC_ARG_WITH([weather],
	[AS_HELP_STRING([--with-weather],
	[Build the weather calendar backend (default=yes)])],
	[use_gweather=$withval], [use_gweather=yes])
if test "$enable_calendar" = "no"; then
	use_gweather="no (calendar support is disabled)"
fi

AC_MSG_RESULT([$use_gweather])
if test "x$use_gweather" = "xyes"; then
	PKG_CHECK_MODULES([LIBGWEATHER], [gweather >= gweather_minimum_version],[],
	[AC_MSG_ERROR([The weather calendar backend requires GWeather >= gweather_minimum_version. Alternatively, you may specify --without-weather as a configure option to avoid building the backend.])])
fi
AM_CONDITIONAL(ENABLE_WEATHER, [test $use_gweather = yes])

dnl ******************************
dnl NNTP support.
dnl ******************************
AC_ARG_ENABLE([nntp],
	AS_HELP_STRING([--enable-nntp],
	[Build Usenet news (NNTP) backend]),
	[enable_nntp=$enableval],[enable_nntp=yes])
if test "x$enable_nntp" = "xyes"; then
	AC_DEFINE(ENABLE_NNTP,1,[Build NNTP backend])
	msg_nntp="yes"
else
	msg_nntp="no"
fi
AM_CONDITIONAL(ENABLE_NNTP, [test x$enable_nntp = xyes])

AC_DEFINE(HANDLE_LIBICAL_MEMORY, 1, [Define it once memory returned by libical is free'ed properly])

dnl ******************************
dnl Hula support
dnl ******************************
AC_ARG_ENABLE([hula],
	AS_HELP_STRING([--enable-hula=no/yes],
	[Enable support for the now-defunct Hula project]),
	[],[enable_hula=no])

if test "x$enable_hula" = "xyes"; then
	AC_DEFINE(ENABLE_HULA,1,[Build Hula backend])
	msg_hula="yes"
else
	msg_hula="no"
fi
AM_CONDITIONAL(ENABLE_HULA, [test x$enable_hula = xyes])

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
EVO_KRB5_SUPPORT

dnl ******************************
dnl Purify support
dnl ******************************
EVO_PURIFY_SUPPORT

dnl ******************************
dnl LDAP support.
dnl ******************************
if test "x$os_win32" != "xyes"; then
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
else
	dnl Win32 LDAP support
	LDAP_CFLAGS="-DLDAP_DEPRECATED"
	LDAP_LIBS="-lwldap32"
	AC_SUBST(LDAP_CFLAGS)
	AC_SUBST(LDAP_LIBS)
	AC_DEFINE(HAVE_LDAP,1,[Define if you have LDAP support])
	AM_CONDITIONAL(ENABLE_LDAP, true)
	AM_CONDITIONAL(SUNLDAP, false)
	msg_ldap="yes"
fi

DBUS_GLIB_REQUIRED=dbus_glib_minimum_version
AC_SUBST(DBUS_GLIB_REQUIRED)

dnl ******************************
dnl GObject marshalling
dnl ******************************
AM_PATH_GLIB_2_0

dnl We use AC_SUBST_FILE because AC_SUBST won't deal with newlines
EVO_MARSHAL_RULE=$srcdir/marshal.mk
AC_SUBST_FILE(EVO_MARSHAL_RULE)

dnl ******************************
dnl Posix thread support
dnl ******************************

dnl GLIB_CONFIG=${GLIB_CONFIG-glib-config}
dnl GNOME_PTHREAD_CHECK

dnl if test "x$PTHREAD_LIB" = "x" ; then
dnl 	AC_MSG_ERROR([POSIX threads are currently required for Evolution])
dnl fi

dnl Notice that this is a hack, and we wont be able to use this forever, but
dnl at least for some time

EVO_PTHREAD_CHECK

THREADS_LIBS="$PTHREAD_LIB"
THREADS_CFLAGS="$PTHREAD_CFLAGS"

AC_SUBST(THREADS_LIBS)
AC_SUBST(THREADS_CFLAGS)
AC_DEFINE(ENABLE_THREADS,1,[Required])

dnl ******************************
dnl DBus stuff
dnl ******************************
PKG_CHECK_MODULES(DBUS_GLIB, dbus-glib-1 >= dbus_glib_minimum_version)
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

AC_PATH_PROG(DBUS_BINDING_TOOL, dbus-binding-tool, [no])
if test "x$DBUS_BINDING_TOOL" = "xno"; then
  AC_MSG_ERROR([You need dbus-binding-tools to build evolution-data-server])
fi

dnl ***********************
dnl Gnome keyring support
dnl ***********************
AC_ARG_ENABLE([gnome-keyring],
	AS_HELP_STRING([--enable-gnome-keyring],
	[Attempt to use Gnome Keyring for storing passwords @<:@default=yes@:>@]),
	[enable_gnome_keyring="$enableval"],[enable_gnome_keyring="yes"])

if test "x${enable_gnome_keyring}" = "xyes"; then
	PKG_CHECK_MODULES(GNOME_KEYRING, gnome-keyring-1 >= gnome_keyring_minimum_version, have_gnome_keyring="yes", have_gnome_keyring="no");
	if test "x${have_gnome_keyring}" = "xyes"; then
		AC_DEFINE(WITH_GNOME_KEYRING, 1, [Gnome Keyring available])
		GNOME_KEYRING_REQUIREMENT="gnome-keyring-1"
	else
		AC_MSG_ERROR([gnome-keyring >= gnome_keyring_minimum_version is required for gnome keyring support. Use --disable-gnome-keyring if you want to disable gnome-keyring support.])
	fi
fi

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
dnl libedataserver flags
dnl ******************************
E_DATA_SERVER_DEPS="libxml-2.0 gthread-2.0 libsoup-2.4 gconf-2.0 $mozilla_nspr"

EVO_SET_COMPILE_FLAGS(E_DATA_SERVER, $E_DATA_SERVER_DEPS, $THREADS_CFLAGS $MANUAL_NSPR_CFLAGS, $THREADS_LIBS $MANUAL_NSPR_LIBS)
AC_SUBST(E_DATA_SERVER_CFLAGS)
AC_SUBST(E_DATA_SERVER_LIBS)

dnl ******************************
dnl libedataserverui
dnl ******************************
E_DATA_SERVER_UI_DEPS="gtk+-2.0 libxml-2.0 gobject-2.0 gthread-2.0 gconf-2.0 $GNOME_KEYRING_REQUIREMENT"

EVO_SET_COMPILE_FLAGS(E_DATA_SERVER_UI, $E_DATA_SERVER_UI_DEPS, $THREADS_CFLAGS, $THREADS_LIBS)
AC_SUBST(E_DATA_SERVER_UI_CFLAGS)
AC_SUBST(E_DATA_SERVER_UI_LIBS)

E_BACKEND_DEPS="gobject-2.0 libxml-2.0 gmodule-2.0 gconf-2.0"

dnl ******************************
dnl libebackend flags
dnl ******************************
EVO_SET_COMPILE_FLAGS(E_BACKEND, $E_BACKEND_DEPS)
AC_SUBST(E_BACKEND_CFLAGS)
AC_SUBST(E_BACKEND_LIBS)

dnl ******************************
dnl evolution-addressbook flags
dnl ******************************
EVOLUTION_ADDRESSBOOK_DEPS="libxml-2.0 dbus-glib-1 gobject-2.0 gthread-2.0 gconf-2.0"

EVO_SET_COMPILE_FLAGS(EVOLUTION_ADDRESSBOOK, $EVOLUTION_ADDRESSBOOK_DEPS)
AC_SUBST(EVOLUTION_ADDRESSBOOK_CFLAGS)
AC_SUBST(EVOLUTION_ADDRESSBOOK_LIBS)

dnl ******************************
dnl evolution-calendar flags
dnl ******************************
if test "x${enable_calendar}" = "xyes"; then
	EVOLUTION_CALENDAR_DEPS="libical >= libical_minimum_version libxml-2.0 dbus-glib-1 gio-2.0 gobject-2.0 gthread-2.0 gconf-2.0"

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
fi

dnl ******************************
dnl Google flags
dnl ******************************
GDATA_DEPS="libsoup-2.4 libxml-2.0"
EVO_SET_COMPILE_FLAGS(GDATA, $GDATA_DEPS)
AC_SUBST(GDATA_CFLAGS)
AC_SUBST(GDATA_LIBS)

dnl ******************************
dnl Groupwise flags
dnl ******************************
LIBSOUP_REQUIRED=libsoup_minimum_version
AC_SUBST(LIBSOUP_REQUIRED)

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
	[enable support for large files [[default=no]]]),
	[],[enable_largefile="no"])

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

EVO_SET_COMPILE_FLAGS(CAMEL, $mozilla_nss gio-2.0 sqlite3 >= sqlite_minimum_version gthread-2.0, $THREADS_CFLAGS $KRB5_CFLAGS $MANUAL_NSS_CFLAGS $LARGEFILE_CFLAGS, -lz $THREADS_LIBS $KRB5_LIBS $MANUAL_NSS_LIBS)
AC_SUBST(CAMEL_CFLAGS)
AC_SUBST(CAMEL_LIBS)
AC_SUBST(CAMEL_GROUPWISE_CFLAGS)
AC_SUBST(CAMEL_GROUPWISE_LIBS)

dnl ******************************
dnl Special directories
dnl ******************************

dnl If you add something here, consider whether or not you also
dnl need to add it to one or more .pc.in files (for Connector,
dnl etc)

privdatadir='${datadir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privdatadir)

privincludedir='${includedir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privincludedir)

privlibdir='${libdir}'/evolution-data-server-$BASE_VERSION
AC_SUBST(privlibdir)

imagesdir='${datadir}'/pixmaps/evolution-data-server
AC_SUBST(imagesdir)

extensiondir='${libdir}'/evolution-data-server-$API_VERSION/extensions
AC_SUBST(extensiondir)

uidir="$privdatadir/ui"
AC_SUBST(uidir)

if test "x$use_gweather" = "xyes"; then
	weatherdatadir="$privdatadir/weather"
	AC_SUBST(weatherdatadir)
fi

dnl separate camel from e-d-s? or should it be under the same spot? same for now.
camel_providerdir='${libdir}'/evolution-data-server-$API_VERSION/camel-providers
AC_SUBST(camel_providerdir)

dnl ******************************
dnl IDL/Component Versioning
dnl ******************************
INTERFACE_VERSION="$BASE_VERSION"
AC_SUBST(INTERFACE_VERSION)
AC_DEFINE_UNQUOTED(INTERFACE_VERSION, "$INTERFACE_VERSION", [IDL interface version (Major.Minor)])

EVO_SUBST_SERVER_RULE='%.server.in: %.server.in.in ; sed -e "s|\@BINDIR\@|$(bindir)|" -e "s|\@LIBEXECDIR_IN_SERVER_FILE\@|$(LIBEXECDIR_IN_SERVER_FILE)|" -e "s|\@COMPONENTDIR\@|$(componentdir)|" -e "s|\@IMPORTERSDIR\@|$(importersdir)|" -e "s|\@EXEEXT\@|$(EXEEXT)|" -e "s|\@VERSION\@|$(BASE_VERSION)|" -e "s|\@API_VERSION\@|$(API_VERSION)|" -e "s|\@INTERFACE_VERSION\@|$(INTERFACE_VERSION)|" $< > $@'
EVO_NAME_SERVER_RULE='%_$(API_VERSION).server: %.server ; mv $< $@'
AC_SUBST(EVO_SUBST_SERVER_RULE)
AC_SUBST(EVO_NAME_SERVER_RULE)

dnl *******************
dnl D-BUS service stuff
dnl *******************
EVO_SUBST_SERVICE_RULE='%.service: %.service.in Makefile ; sed -e "s|\@BINDIR\@|$(bindir)|" -e "s|\@LIBEXECDIR\@|$(libexecdir)|" -e "s|\@COMPONENTDIR\@|$(componentdir)|" -e "s|\@IMPORTERSDIR\@|$(importersdir)|" -e "s|\@VERSION\@|$(BASE_VERSION)|" -e "s|\@INTERFACE_VERSION\@|$(INTERFACE_VERSION)|" $< > $@'
AC_SUBST(EVO_SUBST_SERVICE_RULE)

dnl ******************************
dnl GConf stuff
dnl ******************************
AC_PATH_PROG(GCONFTOOL, gconftool-2, [no])
AM_GCONF_SOURCE_2

dnl ******************************
dnl gperf stuff
dnl ******************************
AC_PATH_PROG(GPERF, gperf, [no])
if test "x$GPERF" = "xno"; then
  AC_MSG_ERROR([You need gperf to build evolution-data-server])
fi

dnl ******************************
dnl Makefiles
dnl ******************************
export privlibdir
export privincludedir
export privdatadir

AC_CONFIG_FILES([
Makefile
evolution-data-server-zip
evolution-data-server.pc
addressbook/Makefile
addressbook/libebook/Makefile
addressbook/libebook/libebook.pc
addressbook/libedata-book/Makefile
addressbook/libedata-book/libedata-book.pc
addressbook/backends/Makefile
addressbook/backends/file/Makefile
addressbook/backends/vcf/Makefile
addressbook/backends/ldap/Makefile
addressbook/backends/google/Makefile
addressbook/backends/groupwise/Makefile
addressbook/backends/webdav/Makefile
addressbook/tests/Makefile
addressbook/tests/ebook/Makefile
addressbook/tests/vcard/Makefile
art/Makefile
calendar/Makefile
calendar/libecal/Makefile
calendar/libecal/libecal.pc
calendar/libedata-cal/Makefile
calendar/libedata-cal/libedata-cal.pc
calendar/backends/Makefile
calendar/backends/caldav/Makefile
calendar/backends/file/Makefile
calendar/backends/groupwise/Makefile
calendar/backends/http/Makefile
calendar/backends/contacts/Makefile
calendar/backends/weather/Makefile
calendar/backends/google/Makefile
calendar/tests/Makefile
calendar/tests/ecal/Makefile
camel/Makefile
camel/providers/Makefile
camel/providers/groupwise/Makefile
camel/providers/hula/Makefile
camel/providers/imap/Makefile
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
camel/tests/stream/Makefile
camel/camel.pc
camel/camel-provider.pc
libebackend/Makefile
libebackend/libebackend.pc
libedataserver/Makefile
libedataserver/eds-version.h
libedataserver/libedataserver.pc
libedataserverui/Makefile
libedataserverui/libedataserverui.pc
servers/Makefile
servers/groupwise/Makefile
servers/groupwise/libegroupwise.pc
servers/google/Makefile
servers/google/libgdata/Makefile
servers/google/libgdata/libgdata.pc
servers/google/libgdata-google/Makefile
servers/google/libgdata-google/libgdata-google.pc
docs/Makefile
docs/reference/Makefile
docs/reference/addressbook/Makefile
docs/reference/addressbook/libebook/Makefile
docs/reference/addressbook/libedata-book/Makefile
docs/reference/calendar/Makefile
docs/reference/calendar/libecal/Makefile
docs/reference/calendar/libedata-cal/Makefile
docs/reference/camel/Makefile
docs/reference/libedataserver/Makefile
docs/reference/libedataserverui/Makefile
docs/reference/libebackend/Makefile
po/Makefile.in
])
AC_OUTPUT

echo "
	evolution-data-server has been configured as follows:
	Calendar:		$msg_calendar
	Weather calendar:	$use_gweather
	Mail Directory:		$system_mail_dir, $system_mail_perm
	LDAP support:		$msg_ldap
	NNTP support:		$msg_nntp
	Kerberos 5:		$msg_krb5
	Gnome Keyring:		$enable_gnome_keyring
	SSL support:		$msg_ssl
	SMIME support:		$msg_smime
	IPv6 support:		$msg_ipv6
	Dot Locking:		$msg_dot
	File Locking:		$msg_file
	Large files:		$enable_largefile
	Gtk Doc:		$enable_gtk_doc
"
if test "x$msg_hula" = "xyes"; then
	echo "Hula support:	$msg_hula"
fi

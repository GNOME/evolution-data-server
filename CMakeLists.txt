# Evolution-Data-Server build script

cmake_minimum_required(VERSION 3.1)
cmake_policy(VERSION 3.1)

project(evolution-data-server
	VERSION 3.38.0
	LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(PROJECT_BUGREPORT "https://gitlab.gnome.org/GNOME/evolution-data-server/issues/")

# Base Version: This is for API/version tracking for things like
# D-Bus server files. This should always be the major/minor of
# the stable version or stable version to be.
set(BASE_VERSION 3.38)

# This number is meaningless, but we're now stuck with it in our
# library names for backward compatibility.
set(API_VERSION 1.2)

# API version of the calendar part
set(CAL_API_VERSION 2.0)

# Required for FindIntltool module
set(GETTEXT_PACKAGE ${PROJECT_NAME})
set(GETTEXT_PO_DIR ${CMAKE_SOURCE_DIR}/po)

# Required for 'disttest' and 'distcheck' of DistTarget module
set(PROJECT_DISTCONFIGURE_PARAMS
	-DENABLE_GOA=ON
	-DENABLE_EXAMPLES=ON
	-DENABLE_INTROSPECTION=ON
	-DENABLE_VALA_BINDINGS=ON
	-DENABLE_INSTALLED_TESTS=ON
	-DENABLE_GTK_DOC=ON
	-DWITH_PRIVATE_DOCS=ON
)

# ******************************
# D-Bus versioning
# ******************************
# Actual name can be modified with DBUS_SERVICES_PREFIX option
set(ADDRESS_BOOK_DBUS_SERVICE_NAME	"org.gnome.evolution.dataserver.AddressBook10")
set(CALENDAR_DBUS_SERVICE_NAME		"org.gnome.evolution.dataserver.Calendar8")
set(SOURCES_DBUS_SERVICE_NAME		"org.gnome.evolution.dataserver.Sources5")
set(USER_PROMPTER_DBUS_SERVICE_NAME	"org.gnome.evolution.dataserver.UserPrompter0")

# ******************************
# Library versioning
# ******************************
set(LIBCAMEL_CURRENT 62)
set(LIBCAMEL_REVISION 0)
set(LIBCAMEL_AGE 0)

set(LIBEBACKEND_CURRENT 10)
set(LIBEBACKEND_REVISION 0)
set(LIBEBACKEND_AGE 0)

set(LIBEDATASERVER_CURRENT 25)
set(LIBEDATASERVER_REVISION 0)
set(LIBEDATASERVER_AGE 0)

set(LIBEDATASERVERUI_CURRENT 2)
set(LIBEDATASERVERUI_REVISION 0)
set(LIBEDATASERVERUI_AGE 0)

set(LIBEBOOK_CURRENT 20)
set(LIBEBOOK_REVISION 1)
set(LIBEBOOK_AGE 3)

set(LIBEBOOK_CONTACTS_CURRENT 3)
set(LIBEBOOK_CONTACTS_REVISION 0)
set(LIBEBOOK_CONTACTS_AGE 0)

set(LIBEDATABOOK_CURRENT 26)
set(LIBEDATABOOK_REVISION 0)
set(LIBEDATABOOK_AGE 0)

set(LIBECAL_CURRENT 1)
set(LIBECAL_REVISION 0)
set(LIBECAL_AGE 0)

set(LIBEDATACAL_CURRENT 1)
set(LIBEDATACAL_REVISION 0)
set(LIBEDATACAL_AGE 0)

# Keep these two definitions in agreement.
set(glib_minimum_version 2.46)
set(glib_encoded_version GLIB_VERSION_2_46)

# Keep these two definitions in agreement.
set(gdk_minimum_version 3.10)
set(gdk_encoded_version GDK_VERSION_3_10)

# Keep these two definitions in agreement.
set(soup_minimum_version 2.58)
set(soup_encoded_version SOUP_VERSION_2_58)

# Warn about API usage that violates our minimum requirements.
add_definitions(-DGLIB_VERSION_MAX_ALLOWED=${glib_encoded_version})
add_definitions(-DGDK_VERSION_MAX_ALLOWED=${gdk_encoded_version})
add_definitions(-DSOUP_VERSION_MAX_ALLOWED=${soup_encoded_version})

# These will suppress warnings about newly-deprecated symbols. Ideally
# these settings should match our minimum requirements and we will clean
# up any new deprecation warnings after bumping our minimum requirements.
# But if the warnings get to be overwhelming, use fixed versions instead.
add_definitions(-DGLIB_VERSION_MIN_REQUIRED=${glib_encoded_version})
add_definitions(-DGDK_VERSION_MIN_REQUIRED=${gdk_encoded_version})
add_definitions(-DSOUP_VERSION_MIN_REQUIRED=${soup_encoded_version})

set(gcr_minimum_version 3.4)
set(libgdata_minimum_version 0.15.1)
set(libical_glib_minimum_version 3.0.7)
set(libsecret_minimum_version 0.5)
set(libxml_minimum_version 2.0.0)
set(sqlite_minimum_version 3.7.17)

# Optional Packages
set(goa_minimum_version 3.8)
set(gweather_minimum_version 3.10)
set(libaccounts_glib_minimum_version 1.4)
set(libsignon_glib_minimum_version 1.8)
set(json_glib_minimum_version 1.0.4)
set(webkit2gtk_minimum_version 2.28.0)
set(libcanberra_gtk_minimum_version 0.25)

# Load modules from the source tree
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Packagers might want to need different settings for the RPATH related things
# From experience, especially CMAKE_BUILD_WITH_INSTALL_RPATH might need to be
# switched to ON, if CMake fails to set the right values during make install
set(CMAKE_SKIP_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_SKIP_BUILD_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF CACHE BOOL INTERNAL)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON CACHE BOOL INTERNAL)
# CMAKE_INSTALL_RPATH is set below

# CMake modules
include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckLibraryExists)

# Project custom modules
include(PrintableOptions)

add_printable_variable(LIB_SUFFIX "Library directory suffix, usually defined to '64' for x86_64 systems" "")
add_printable_variable_bare(CMAKE_INSTALL_PREFIX)
add_printable_variable_path(BIN_INSTALL_DIR "Install directory for binary files, defaults to CMAKE_INSTALL_PREFIX/bin" "")
add_printable_variable_path(INCLUDE_INSTALL_DIR "Install directory for header files, defaults to CMAKE_INSTALL_PREFIX/include" "")
add_printable_variable_path(LIB_INSTALL_DIR "Install directory for library files, defaults to CMAKE_INSTALL_PREFIX/lib{LIB_SUFFIX}" "")
add_printable_variable_path(LIBEXEC_INSTALL_DIR "Install directory for library executable files, defaults to CMAKE_INSTALL_PREFIX/libexec" "")
add_printable_variable_path(SHARE_INSTALL_PREFIX "Install directory for shared files, defaults to CMAKE_INSTALL_PREFIX/share" "")
add_printable_variable_path(LOCALE_INSTALL_DIR "Install directory for locale files, defaults to SHARE_INSTALL_PREFIX/locale" "")
add_printable_variable_path(SYSCONF_INSTALL_DIR "Install directory for system configuration files, defaults to CMAKE_INSTALL_PREFIX/etc" "")

macro(ensure_default_value _var _defvalue)
	if(${_var} STREQUAL "")
		set(${_var} ${_defvalue})
	endif(${_var} STREQUAL "")
endmacro(ensure_default_value)

ensure_default_value(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin")
ensure_default_value(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/include")
ensure_default_value(LIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}")
ensure_default_value(LIBEXEC_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/libexec")
ensure_default_value(SHARE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/share")
ensure_default_value(LOCALE_INSTALL_DIR "${SHARE_INSTALL_PREFIX}/locale")
ensure_default_value(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc")

add_printable_variable(DBUS_SERVICES_PREFIX "Prefix for D-Bus services, usually left empty, without trailing dot" "")

if(NOT ("${DBUS_SERVICES_PREFIX}" STREQUAL ""))
	set(ADDRESS_BOOK_DBUS_SERVICE_NAME	"${DBUS_SERVICES_PREFIX}.${ADDRESS_BOOK_DBUS_SERVICE_NAME}")
	set(CALENDAR_DBUS_SERVICE_NAME		"${DBUS_SERVICES_PREFIX}.${CALENDAR_DBUS_SERVICE_NAME}")
	set(SOURCES_DBUS_SERVICE_NAME		"${DBUS_SERVICES_PREFIX}.${SOURCES_DBUS_SERVICE_NAME}")
	set(USER_PROMPTER_DBUS_SERVICE_NAME	"${DBUS_SERVICES_PREFIX}.${USER_PROMPTER_DBUS_SERVICE_NAME}")
else(NOT ("${DBUS_SERVICES_PREFIX}" STREQUAL ""))
	unset(DBUS_SERVICES_PREFIX)
endif(NOT ("${DBUS_SERVICES_PREFIX}" STREQUAL ""))

# ******************************
# Special directories
# ******************************

# If you add something here, consider whether or not you also
# need to add it to one or more .pc.in files (for Connector, etc)

set(privdatadir "${SHARE_INSTALL_PREFIX}/${PROJECT_NAME}")
set(privincludedir "${INCLUDE_INSTALL_DIR}/${PROJECT_NAME}")
set(privlibdir "${LIB_INSTALL_DIR}/${PROJECT_NAME}")
set(privlibexecdir "${LIBEXEC_INSTALL_DIR}/${PROJECT_NAME}")

SET(CMAKE_INSTALL_RPATH "${privlibdir}")

if(WIN32)
	# On Win32 there is no "rpath" mechanism. We install the private
	# shared libraries in $libdir, meaning the DLLs will actually be in
	# $bindir. This means just having $bindir in PATH will be enough.
	set(privsolibdir "${LIB_INSTALL_DIR}")
else(WIN32)
	set(privsolibdir "${privlibdir}")
endif(WIN32)

set(imagesdir "${SHARE_INSTALL_PREFIX}/pixmaps/${PROJECT_NAME}")
set(moduledir "${privlibdir}/registry-modules")
set(credentialmoduledir "${privlibdir}/credential-modules")
set(uimoduledir "${privlibdir}/ui-modules")
set(ebook_backenddir "${privlibdir}/addressbook-backends")
set(ecal_backenddir "${privlibdir}/calendar-backends")
set(ro_sourcesdir "${privdatadir}/ro-sources")
set(rw_sourcesdir "${privdatadir}/rw-sources")
set(camel_providerdir "${privlibdir}/camel-providers")

# *******************
# D-BUS service stuff
# *******************
set(servicelibexecdir "${LIBEXEC_INSTALL_DIR}")
if(WIN32 AND (NOT ("$ENV{WIN32_SERVICELIBEXECDIR}" STREQUAL "")))
	#
	# D-Bus requires Windows full path, not Unix-like, like the one provided by MSYS
	#
	AC_SUBST(WIN32_SERVICELIBEXECDIR)
	set(servicelibexecdir "$ENV{WIN32_SERVICELIBEXECDIR}")
endif(WIN32 AND (NOT ("$ENV{WIN32_SERVICELIBEXECDIR}" STREQUAL "")))

add_printable_variable_path(WITH_DBUS_SERVICE_DIR "Set directory for D-Bus service files" "${SHARE_INSTALL_PREFIX}/dbus-1/services")

# It is correct for this to be in ${CMAKE_INSTALL_PREFIX}/lib, even on systems where that
# does not match ${LIB_INSTALL_DIR}. This is what systemd uses on such platforms.

add_printable_variable_path(WITH_SYSTEMDUSERUNITDIR "Set directory for systemd user units, or 'no' to disable" "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")

string(LENGTH "${CMAKE_BINARY_DIR}" bindirlen)
string(LENGTH "${WITH_SYSTEMDUSERUNITDIR}" maxlen)
if(maxlen LESS bindirlen)
	set(substr "***")
else(maxlen LESS bindirlen)
	string(SUBSTRING "${WITH_SYSTEMDUSERUNITDIR}" 0 ${bindirlen} substr)
endif(maxlen LESS bindirlen)
string(TOUPPER "${WITH_SYSTEMDUSERUNITDIR}" optupper)

if(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))
	set(WITH_SYSTEMD_USER_UNITS ON)
	set(WITH_SYSTEMDUSERUNITDIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
elseif(NOT WITH_SYSTEMDUSERUNITDIR)
	set(WITH_SYSTEMD_USER_UNITS OFF)
else()
	set(WITH_SYSTEMD_USER_UNITS ON)
endif()

unset(bindirlen)
unset(maxlen)
unset(substr)
unset(optupper)

# ******************************
# Dependencies
# ******************************

include(CodeCoverageGCOV)
include(CheckTarget)
include(DistTargets)
include(GLibTools)
include(GObjectIntrospection)
include(GtkDoc)
include(InstalledTests)
include(PkgConfigEx)
include(SetupBuildFlags)
include(UninstallTarget)

include(FindIntltool)
include(FindKRB5)
include(FindLDAP)
include(FindPhonenumber)
include(FindSMIME)

add_printable_option(ENABLE_MAINTAINER_MODE "Enable maintainer mode" OFF)
add_printable_option(WITH_PRIVATE_DOCS "Build documentation for private libraries (requires -DENABLE-GTK-DOC=ON" OFF)

if(ENABLE_MAINTAINER_MODE)
	set(BUILD_TESTING ON)
endif(ENABLE_MAINTAINER_MODE)

# Setup compiler/linker flags
setup_build_flags(${ENABLE_MAINTAINER_MODE})

CHECK_INCLUDE_FILE(com_err.h HAVE_COM_ERR_H)
CHECK_INCLUDE_FILE(et/com_err.h HAVE_ET_COM_ERR_H)
CHECK_INCLUDE_FILE(sys/wait.h HAVE_SYS_WAIT_H)
CHECK_INCLUDE_FILE(wspiapi.h HAVE_WSPIAPI_H)
CHECK_INCLUDE_FILE(zlib.h HAVE_ZLIB_H)
CHECK_FUNCTION_EXISTS(fsync HAVE_FSYNC)
CHECK_FUNCTION_EXISTS(strptime HAVE_STRPTIME)
CHECK_FUNCTION_EXISTS(nl_langinfo HAVE_NL_LANGINFO)

# ******************************
# required executables
# ******************************

find_program(GPERF gperf)
if(NOT GPERF)
	message(FATAL_ERROR "You need gperf to build ${PROJECT_NAME}")
endif(NOT GPERF)

# ******************************
# db_load checking, it's optional
# ******************************

find_program(DB_LOAD NAMES db_load db4_load db_load-4 db5_load db_load-5)
if(DB_LOAD)
	set(HAVE_DB_LOAD ON)
else(DB_LOAD)
	set(HAVE_DB_LOAD OFF)
	message(WARNING "db_load not found, some unit tests will not be run")
endif(DB_LOAD)

# ******************************
# deflateInit is a #define, use deflateEnd instead
# ******************************

CHECK_LIBRARY_EXISTS(z deflateEnd "" deflateEnd_exists)

if(NOT HAVE_ZLIB_H)
	message(FATAL_ERROR "zlib.h not found")
endif(NOT HAVE_ZLIB_H)
if(NOT deflateEnd_exists)
	message(FATAL_ERROR "zlib doesn't provide deflateEnd")
endif(NOT deflateEnd_exists)

pkg_check_modules(GNOME_PLATFORM REQUIRED
	gio-2.0>=${glib_minimum_version}
	gmodule-2.0>=${glib_minimum_version}
	libxml-2.0>=${libxml_minimum_version}
	libsoup-2.4>=${soup_minimum_version}
)

if(WIN32)
	pkg_check_modules(GIO_UNIX gio-windows-2.0)
else(WIN32)
	pkg_check_modules(GIO_UNIX gio-unix-2.0)
endif(WIN32)

# *******************
# Check for ICU
# *******************
#
# ICU started shipping pkg-config files but it's not present
# on many systems, if we don't find the pkg-config
# file then let's fallback on a manual check
pkg_check_modules(ICU icu-i18n)

if(NOT ICU_FOUND)
	CHECK_INCLUDE_FILE(unicode/ucol.h HAVE_UNICODE_UCOL_H)
	if(NOT HAVE_UNICODE_UCOL_H)
		message(FATAL_ERROR "ICU unicode/ucol.h not found; icu-i18n is required")
	endif(HAVE_UNICODE_UCOL_H)

	set(CMAKE_REQUIRED_LIBRARIES "-licui18n -licuuc -licudata")
	CHECK_C_SOURCE_COMPILES("#include <unicode/ucol.h>
				int main(void) { ucol_open (\"\", NULL); return 0; }" HAVE_UCOL_OPEN)
	unset(CMAKE_REQUIRED_LIBRARIES)

	if(HAVE_UCOL_OPEN)
		set(ICU_CFLAGS -D_REENTRANT)
		set(ICU_LIBS "-licui18n -licuuc -licudata")
	else(HAVE_UCOL_OPEN)
		message(FATAL_ERROR "Failed to find icu-i18n, install its development files or build them first")
	endif(HAVE_UCOL_OPEN)
endif(NOT ICU_FOUND)

# *************************
# Check for GTK+
# *************************

add_printable_option(ENABLE_GTK "Enable gtk+ support" ON)

if(ENABLE_GTK)
	pkg_check_modules_for_option(ENABLE_GTK "GTK+ support" GTK gtk+-3.0>=${gdk_minimum_version})
	pkg_check_modules_for_option(ENABLE_GTK "GTK+ support" GCR gcr-3>=${gcr_minimum_version})

	set(HAVE_GTK 1)
endif(ENABLE_GTK)

# **************************************************************
# Check for WebKitGTK+ and json-glib for OAuth2 authentications
# **************************************************************

add_printable_option(ENABLE_OAUTH2 "Enable built-in OAuth2 authentications" ON)

if(ENABLE_OAUTH2)
	pkg_check_modules_for_option(ENABLE_OAUTH2 "OAuth2 authentication support" OAUTH2_JSON_GLIB
		json-glib-1.0>=${json_glib_minimum_version}
	)

	pkg_check_modules_for_option(ENABLE_OAUTH2 "OAuth2 authentication support" OAUTH2_WEBKIT2GTK
		webkit2gtk-4.0>=${webkit2gtk_minimum_version}
	)

	add_printable_variable(WITH_GOOGLE_CLIENT_ID "Google OAuth 2.0 client id" "")
	add_printable_variable(WITH_GOOGLE_CLIENT_SECRET "Google OAuth 2.0 client secret" "")

	if(WITH_GOOGLE_CLIENT_ID STREQUAL "")
		set(WITH_GOOGLE_CLIENT_ID "590402290962-2i0b7rqma8b9nmtfrcp7fa06g6cf7g74.apps.googleusercontent.com")
	endif(WITH_GOOGLE_CLIENT_ID STREQUAL "")

	if(WITH_GOOGLE_CLIENT_SECRET STREQUAL "")
		set(WITH_GOOGLE_CLIENT_SECRET "mtfUe5W8Aal9DcgVipOY1T9G")
	endif(WITH_GOOGLE_CLIENT_SECRET STREQUAL "")

	add_printable_variable(WITH_OUTLOOK_CLIENT_ID "Outlook.com OAuth 2.0 client id" "")
	add_printable_variable(WITH_OUTLOOK_CLIENT_SECRET "Outlook.com OAuth 2.0 client secret" "")

	if(WITH_OUTLOOK_CLIENT_ID STREQUAL "")
		set(WITH_OUTLOOK_CLIENT_ID "cc6e0693-0e26-4220-8322-9d363e308fc6")
	endif(WITH_OUTLOOK_CLIENT_ID STREQUAL "")

	# Outlook.com requires client secret only for Web applications
	#if(WITH_OUTLOOK_CLIENT_SECRET STREQUAL "")
	#	set(WITH_OUTLOOK_CLIENT_SECRET "")
	#endif(WITH_OUTLOOK_CLIENT_SECRET STREQUAL "")
endif(ENABLE_OAUTH2)

# ******************************************
# Check whether to build examples/demos
# ******************************************

add_printable_option(ENABLE_EXAMPLES "Enable the build of examples" ON)

if(ENABLE_EXAMPLES)
	pkg_check_modules_for_option(ENABLE_EXAMPLES "build the example program(s)" EXAMPLES
		gtk+-3.0>=3.10
		glib-2.0>=2.38
	)

	set(BUILD_EXAMPLES 1)
endif(ENABLE_EXAMPLES)

# *******************************
# Check for GNOME Online Accounts
# *******************************

add_printable_option(ENABLE_GOA "Enable GNOME Online Accounts support" ON)

if(ENABLE_GOA)
	pkg_check_modules_for_option(ENABLE_GOA "GNOME Online Accounts support" GOA goa-1.0>=${goa_minimum_version})

	set(HAVE_GOA 1)
endif(ENABLE_GOA)

# **********************************************
# Check if backend per process should be enabled
# **********************************************

add_printable_option(ENABLE_BACKEND_PER_PROCESS "Enable backend per process support" OFF)

# ***********************************
# Check for libsecret and gcr-base
# ***********************************

if(NOT WIN32)
	pkg_check_modules(LIBSECRET REQUIRED libsecret-unstable>=${libsecret_minimum_version})
	pkg_check_modules(GCR_BASE REQUIRED gcr-base-3>=${gcr_minimum_version})
endif(NOT WIN32)

# ******************************
# libdb checking
# ******************************
add_printable_variable(WITH_LIBDB "Prefix where libdb is installed" ON)
add_printable_variable(WITH_LIBDB_CFLAGS "Arguments required to compile with libdb" "")
add_printable_variable(WITH_LIBDB_LIBS "Arguments required to link with libdb" "")

if(WITH_LIBDB STREQUAL "")
	set(WITH_LIBDB ON)
endif(WITH_LIBDB STREQUAL "")

if(WITH_LIBDB)
	if(NOT (WITH_LIBDB OR ("${WITH_LIBDB}" STREQUAL "YES")))
		set(LIBDB_CFLAGS "-I${WITH_LIBDB}/include")
		set(LIBDB_LIBS "-L${WITH_LIBDB}/lib -ldb")
	else(NOT (WITH_LIBDB OR ("${WITH_LIBDB}" STREQUAL "YES")))
		if(("${WITH_LIBDB_CFLAGS}" STREQUAL "") AND ("${WITH_LIBDB_LIBS}" STREQUAL ""))
			set(LIBDB_CFLAGS "")
			set(LIBDB_LIBS "-ldb")
		else(("${WITH_LIBDB_CFLAGS}" STREQUAL "") AND ("${WITH_LIBDB_LIBS}" STREQUAL ""))
			set(LIBDB_CFLAGS ${WITH_LIBDB_CFLAGS})
			set(LIBDB_LIBS ${WITH_LIBDB_LIBS})
		endif(("${WITH_LIBDB_CFLAGS}" STREQUAL "") AND ("${WITH_LIBDB_LIBS}" STREQUAL ""))
	endif(NOT (WITH_LIBDB OR ("${WITH_LIBDB}" STREQUAL "YES")))

	set(CMAKE_REQUIRED_FLAGS ${LIBDB_CFLAGS})
	set(CMAKE_REQUIRED_LIBRARIES ${LIBDB_LIBS})
	CHECK_C_SOURCE_COMPILES("#include <db.h>
				int main(void) { db_create(NULL, NULL, 0); return 0; }" HAVE_LIBDB)
	unset(CMAKE_REQUIRED_FLAGS)
	unset(CMAKE_REQUIRED_LIBRARIES)

	if(NOT HAVE_LIBDB)
		message(FATAL_ERROR "libdb not found. Use -DWITH_LIBDB=PATH to specify the library prefix, or use -DWITH_LIBDB_CFLAGS=-I/path/to/db/include and -DWITH_LIBDB_LIBS=/path/to/db/lib to specify arguments for compiling and linking. If you want to disable libdb, please use -DWITH_LIBDB=OFF")
	endif(NOT HAVE_LIBDB)
endif(WITH_LIBDB)

# ******************************
# iconv checking
# ******************************

set(CMAKE_REQUIRED_LIBRARIES "-liconv")
CHECK_C_SOURCE_COMPILES("#include <iconv.h>
			#include <stdlib.h>
			int main(void) { iconv_t cd; cd = iconv_open (\"UTF-8\", \"ISO-8859-1\"); return 0; }" HAVE_LIBICONV)
unset(CMAKE_REQUIRED_LIBRARIES)

if(HAVE_LIBICONV)
	set(ICONV_LIBS "-liconv")
	set(HAVE_ICONV ON)
else(HAVE_LIBICONV)
	set(ICONV_LIBS "")
	CHECK_FUNCTION_EXISTS(iconv HAVE_ICONV)
endif(HAVE_LIBICONV)

if(NOT HAVE_ICONV)
	message(FATAL_ERROR "You need to install a working iconv implementation, such as ftp://ftp.gnu.org/pub/gnu/libiconv")
endif(NOT HAVE_ICONV)

set(CMAKE_REQUIRED_LIBRARIES ${ICONV_LIBS})
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/" _binary_dir_with_separator)
CHECK_C_SOURCE_RUNS("#define ICONV_DETECT_BUILD_DIR \"${_binary_dir_with_separator}\"
		     #include \"${CMAKE_SOURCE_DIR}/iconv-detect.c\"" _correct_iconv)
unset(_binary_dir_with_separator)
unset(CMAKE_REQUIRED_LIBRARIES)

if(NOT _correct_iconv)
	message(FATAL_ERROR "You need to install a working iconv implementation, such as ftp://ftp.gnu.org/pub/gnu/libiconv")
endif(NOT _correct_iconv)

# ******************************
# Backtraces for debugging
# ******************************

add_printable_option(ENABLE_BACKTRACES "Enable backtraces for camel_pointer_tracker" OFF)

if(ENABLE_BACKTRACES)
	CHECK_C_SOURCE_COMPILES("#include <execinfo.h>
				int main(void) { void *bt[1]; backtrace_symbols (bt, backtrace (bt, 1)); return 0; }" HAVE_BACKTRACE_SYMBOLS)

	if(HAVE_BACKTRACE_SYMBOLS)
		set(CMAKE_REQUIRED_LIBRARIES -ldw)
		CHECK_C_SOURCE_COMPILES("#include <elfutils/libdwfl.h>
					int main(void) {
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
						return 0; }" _have_elfdwlf)
		unset(CMAKE_REQUIRED_LIBRARIES)

		if(_have_elfdwlf)
			set(HAVE_ELFUTILS_LIBDWFL ON)
			set(LIBDWFL_LIBS "-ldw")
		endif(_have_elfdwlf)
	endif(HAVE_BACKTRACE_SYMBOLS)
endif(ENABLE_BACKTRACES)

# ******************************
# Check for nl_langinfo features
# ******************************

CHECK_C_SOURCE_COMPILES("#include <langinfo.h>
			int main(void) { char *detail = nl_langinfo (CODESET); return 0; }" HAVE_CODESET)

CHECK_C_SOURCE_COMPILES("#include <langinfo.h>
			int main(void) { char *detail = nl_langinfo (_NL_ADDRESS_COUNTRY_AB2); return 0; }" HAVE__NL_ADDRESS_COUNTRY_AB2)

# *******************************************************
# Check to see if strftime supports the use of %l and %k
# *******************************************************

CHECK_C_SOURCE_RUNS("
	#include <stdlib.h>
	#include <string.h>
	#include <time.h>
	int main(int argc, char **argv)	{
	char buf[10];
	time_t rawtime;
	struct tm *timeinfo;

	time(&rawtime);
	timeinfo=localtime(&rawtime);
	buf[0] = 0;
	strftime(buf, 10, \"%lx%k\", timeinfo);

	if (!buf[0] || buf[0] == 'x' || strstr(buf, \"l\") || strstr(buf, \"k\"))
		return 1;
	return 0;
	}" HAVE_LKSTRFTIME)

# ******************************
# system mail stuff
# ******************************

if("$ENV{SENDMAIL_PATH}" STREQUAL "")
	find_program(SENDMAIL_PATH sendmail /usr/sbin /usr/lib)
	if(NOT SENDMAIL_PATH)
		set(SENDMAIL_PATH "/usr/sbin/sendmail")
		message(WARNING "sendmail not found, defaulting to ${SENDMAIL_PATH}")
	endif(NOT SENDMAIL_PATH)
else("$ENV{SENDMAIL_PATH}" STREQUAL "")
	set(SENDMAIL_PATH $ENV{SENDMAIL_PATH})
endif("$ENV{SENDMAIL_PATH}" STREQUAL "")

# ******************************
# Timezone checks
# ******************************

CHECK_C_SOURCE_COMPILES("#include <time.h>
			int main(void) { struct tm tm; tm.tm_gmtoff = 1; return 0; }" HAVE_TM_GMTOFF)

CHECK_C_SOURCE_COMPILES("#include <time.h>
			int main(void) { timezone = 1; return 0; }" HAVE_TIMEZONE)

CHECK_C_SOURCE_COMPILES("#include <time.h>
			int main(void) { altzone = 1; return 0; }" HAVE_ALTZONE)

if((NOT HAVE_TM_GMTOFF) AND (NOT HAVE_TIMEZONE))
	message(FATAL_ERROR "Unable to find a way to determine timezone")
endif((NOT HAVE_TM_GMTOFF) AND (NOT HAVE_TIMEZONE))

# ******************************
# gethostbyaddr_r prototype
# ******************************

CHECK_C_SOURCE_COMPILES("#include <sys/types.h>
			#include <sys/socket.h>
			#include <netinet/in.h>
			#include <netdb.h>
			#define BUFSIZE (sizeof(struct hostent)+10)

			int main(void) {
				struct hostent hent;
				char buffer[BUFSIZE];
				int bufsize=BUFSIZE;
				int h_errno;
				(void)gethostbyaddr_r (\"www.ximian.com\", 14, AF_INET, &hent, buffer, bufsize, &h_errno);
				return 0;
			}" GETHOSTBYADDR_R_SEVEN_ARGS)

# ******************************
# gethostbyname_r prototype
# ******************************

CHECK_C_SOURCE_COMPILES("#include <sys/types.h>
			#include <sys/socket.h>
			#include <netinet/in.h>
			#include <netdb.h>
			#define BUFSIZE (sizeof(struct hostent)+10)

			int main(void) {
				struct hostent hent;
				char buffer[BUFSIZE];
				int bufsize=BUFSIZE;
				int h_errno;
				(void)gethostbyname_r (\"www.ximian.com\", &hent, buffer, bufsize, &h_errno);
				return 0;
			}" GETHOSTBYNAME_R_FIVE_ARGS)

# ******************************
# IPv6 support and getaddrinfo calls
# ******************************

add_printable_option(ENABLE_IPV6 "Enable IPv6 support" ON)

CHECK_C_SOURCE_COMPILES("#include <sys/types.h>
			#include <sys/socket.h>
			#include <netinet/in.h>
			#include <netdb.h>
			#include <stddef.h>

			int main(void) {
				#ifndef NI_MAXHOST
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

				getaddrinfo (\"www.ximian.com\", NULL, &hints, &res);
				freeaddrinfo (res);
				getnameinfo((struct sockaddr *)&sin6, sizeof(sin6), host, sizeof(host), serv, sizeof(serv), 0);
				return 0;
			}" have_addrinfo)

if(NOT have_addrinfo)
	set(NEED_ADDRINFO ON)
	if(ENABLE_IPV6)
		message(FATAL_ERROR "System doesn't support necessary interfaces for IPv6 support. Use -DENABLE_IPV6=OFF to disable IPv6 support.")
	endif(ENABLE_IPV6)
else(NOT have_addrinfo)
	if(ENABLE_IPV6)
		CHECK_C_SOURCE_COMPILES("#include <sys/types.h>
					#include <sys/socket.h>
					#include <netinet/in.h>
					#include <netdb.h>

					int main(void) {
						struct addrinfo hints;
						hints.ai_flags = AI_ADDRCONFIG;
						return 0;
					}" HAVE_AI_ADDRCONFIG)
	endif(ENABLE_IPV6)
endif(NOT have_addrinfo)

# **********************************
# Weather calendar backend support
# **********************************

add_printable_option(ENABLE_WEATHER "Build the weather calendar backend" ON)

if(ENABLE_WEATHER)
	pkg_check_modules_for_option(ENABLE_WEATHER "weather calendar backend" LIBGWEATHER gweather-3.0>=${gweather_minimum_version})

	set(CMAKE_REQUIRED_INCLUDES ${LIBGWEATHER_INCLUDE_DIRS})
	set(CMAKE_REQUIRED_LIBRARIES ${LIBGWEATHER_LDFLAGS})
	CHECK_C_SOURCE_COMPILES("#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
				#include <libgweather/gweather.h>
				#undef GWEATHER_I_KNOW_THIS_IS_UNSTABLE

				int main(void) {
					gweather_info_new (NULL);
					return 0;
				}" HAVE_ONE_ARG_GWEATHER_INFO_NEW)
	unset(CMAKE_REQUIRED_INCLUDES)
	unset(CMAKE_REQUIRED_LIBRARIES)
endif(ENABLE_WEATHER)

# ************************************************
# evolution-alarm-notify : Canberra-GTK for Sound
# ************************************************

add_printable_option(ENABLE_CANBERRA "Enable Canberra-GTK for sound in evolution-alarm-notify" ON)

if(ENABLE_CANBERRA)
	pkg_check_modules_for_option(ENABLE_CANBERRA "Canberra-GTK for sound in evolution-alarm-notify" CANBERRA libcanberra-gtk3>=${libcanberra_gtk_minimum_version})
	set(HAVE_CANBERRA ON)
endif(ENABLE_CANBERRA)

# ******************************
# File locking
# ******************************

add_printable_option(ENABLE_DOT_LOCKING "Enable support for locking mail files with dot locking" ON)

if(WIN32 AND ENABLE_DOT_LOCKING)
	message(WARNING "Auto-disabling dot locking for mail files for this platform")
	set(ENABLE_DOT_LOCKING OFF)
	set(USE_DOT_LOCKING OFF)
else(WIN32 AND ENABLE_DOT_LOCKING)
	set(USE_DOT_LOCKING ${ENABLE_DOT_LOCKING})
endif(WIN32 AND ENABLE_DOT_LOCKING)

add_printable_variable(ENABLE_FILE_LOCKING "Enable support for locking mail files with file locking" "fcntl")
set(enable_file_locking_values fcntl flock no)
set_property(CACHE ENABLE_FILE_LOCKING PROPERTY STRINGS ${enable_file_locking_values})

if(NOT WIN32)
	if (${ENABLE_FILE_LOCKING} STREQUAL "fcntl")
		set(USE_FCNTL_LOCKING ON)
	elseif(${ENABLE_FILE_LOCKING} STREQUAL "flock")
		set(USE_FLOCK_LOCKING ON)
	endif()
endif(NOT WIN32)

# ******************************
# sendmail operation
# ******************************

set(defval OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
	set(defval ON)
endif(CMAKE_SYSTEM_NAME STREQUAL "SunOS")

add_printable_option(ENABLE_BROKEN_SPOOL "Use SunOS/Solaris sendmail which has a broken spool format" ${defval})

unset(defval)

# ******************************
# sqlite3 flags
# ******************************
pkg_check_modules(SQLITE3 REQUIRED sqlite3>=${sqlite_minimum_version})

# ******************************
# Google flags
# ******************************

add_printable_option(ENABLE_GOOGLE "Enable Google support" ON)
if(ENABLE_GOOGLE)
	pkg_check_modules_for_option(ENABLE_GOOGLE "Google" LIBGDATA libgdata>=${libgdata_minimum_version})
	set(HAVE_LIBGDATA ON)

	set(CMAKE_REQUIRED_DEFINITIONS ${LIBGDATA_CFLAGS_OTHER})
	set(CMAKE_REQUIRED_INCLUDES ${LIBGDATA_INCLUDE_DIRS})
	set(CMAKE_REQUIRED_LIBRARIES ${LIBGDATA_LDFLAGS})

	CHECK_C_SOURCE_COMPILES("#include <gdata/gdata.h>
				int main(void) {
					gdata_feed_get_next_page_token (NULL);
					gdata_query_next_page (NULL);
					return 0;
				}" HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS)

	unset(CMAKE_REQUIRED_DEFINITIONS)
	unset(CMAKE_REQUIRED_INCLUDES)
	unset(CMAKE_REQUIRED_LIBRARIES)
endif(ENABLE_GOOGLE)

# ******************************
# Checks for large file support
# ******************************

add_printable_option(ENABLE_LARGEFILE "Enable support for large files" ON)

if(ENABLE_LARGEFILE)
	CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)

	if(HAVE_UNISTD_H)
		set(CMAKE_REQUIRED_FLAGS "-DHAVE_UNISTD_H")
	endif(HAVE_UNISTD_H)

	CHECK_C_SOURCE_COMPILES("#ifdef HAVE_UNISTD_H
			#include <unistd.h>
			#endif
			int main(void) {
				off64_t x1;
				return 0;
			}" have_off64_t_bare)
	if(NOT have_off64_t_bare)
		CHECK_C_SOURCE_COMPILES("#define _LARGEFILE64_SOURCE
				#ifdef HAVE_UNISTD_H
				#include <unistd.h>
				#endif
				int main(void) {
					off64_t x1;
					return 0;
				}" have_off64_t_with_largefile64_source)
	endif(NOT have_off64_t_bare)

	CHECK_C_SOURCE_COMPILES("#include <sys/types.h>
			#include <sys/stat.h>
			#include <fcntl.h>
			int main(void) {
				int fd = open (\"__o_largefile\", O_CREAT | O_RDWR | O_LARGEFILE, 0644);
				return 0;
			}" have_o_largefile_bare)
	if(NOT have_o_largefile_bare)
		CHECK_C_SOURCE_COMPILES("#define _LARGEFILE64_SOURCE
				#include <sys/types.h>
				#include <sys/stat.h>
				#include <fcntl.h>
				int main(void) {
					int fd = open (\"__o_largefile\", O_CREAT | O_RDWR | O_LARGEFILE, 0644);
					return 0;
				}" have_o_largefile_with_largefile64_source)
	endif(NOT have_o_largefile_bare)

	set(HAVE_O_LARGEFILE ON)
	if(have_o_largefile_with_largefile64_source)
		add_definitions(-D_LARGEFILE64_SOURCE=1)
	elseif(NOT have_o_largefile_bare)
		set(HAVE_O_LARGEFILE OFF)
	endif()
	unset(CMAKE_REQUIRED_FLAGS)
else(ENABLE_LARGEFILE)
	set(HAVE_O_LARGEFILE OFF)
endif(ENABLE_LARGEFILE)

# ******************************
# subparts flags
# ******************************

pkg_check_modules(SOUP REQUIRED libsoup-2.4)
pkg_check_modules(DATA_SERVER REQUIRED gio-2.0 gmodule-2.0 libsecret-1 libxml-2.0 libsoup-2.4 ${mozilla_nspr} ${mozilla_nss})
set(E_DATA_SERVER_INCLUDES ${E_DATA_SERVER_INCLUDES} ${MANUAL_NSPR_INCLUDES} ${MANUAL_NSS_INCLUDES})
set(E_DATA_SERVER_LDFLAGS ${E_DATA_SERVER_LDFLAGS} ${MANUAL_NSPR_LIBS} ${MANUAL_NSS_LIBS})

pkg_check_modules(BACKEND REQUIRED gio-2.0 gmodule-2.0 libsecret-1 libsoup-2.4 libxml-2.0)
pkg_check_modules(ADDRESSBOOK REQUIRED gio-2.0 libxml-2.0 libsoup-2.4 libsecret-1)
pkg_check_modules(CALENDAR REQUIRED gio-2.0 libical-glib>=${libical_glib_minimum_version} libsoup-2.4 libxml-2.0 libsecret-1)
set(ADDRESSBOOK_CFLAGS ${ADDRESSBOOK_CFLAGS} ${PHONENUMBER_DEFINITIONS})
set(ADDRESSBOOK_INCLUDE_DIRS ${ADDRESSBOOK_INCLUDE_DIRS} ${PHONENUMBER_INCLUDE_DIRS})
set(ADDRESSBOOK_LDFLAGS ${ADDRESSBOOK_LDFLAGS} ${PHONENUMBER_LDFLAGS})
set(CALENDAR_CFLAGS ${CALENDAR_CFLAGS} -DLIBICAL_GLIB_UNSTABLE_API=1)

pkg_check_modules(CAMEL REQUIRED gio-2.0 gmodule-2.0 ${mozilla_nss} ${mozilla_nspr} sqlite3>=${sqlite_minimum_version})
set(CAMEL_CFLAGS ${CAMEL_CFLAGS} ${KRB5_CFLAGS} ${MANUAL_NSS_CFLAGS} ${MANUAL_NSPR_CFLAGS} ${ICU_CFLAGS})
set(CAMEL_LDFLAGS ${CAMEL_LDFLAGS} -lz ${KRB5_LDFLAGS} ${MANUAL_NSS_LIBS} ${MANUAL_NSPR_LIBS} ${ICU_LDFLAGS})

if(NOT (MANUAL_NSPR_INCLUDES STREQUAL ""))
	list(APPEND CAMEL_INCLUDE_DIRS ${MANUAL_NSPR_INCLUDES})
endif(NOT (MANUAL_NSPR_INCLUDES STREQUAL ""))

if(NOT (MANUAL_NSS_INCLUDES STREQUAL ""))
	list(APPEND CAMEL_INCLUDE_DIRS ${MANUAL_NSS_INCLUDES})
endif(NOT (MANUAL_NSS_INCLUDES STREQUAL ""))

# ******************************
# Vala binding
# ******************************

add_printable_option(ENABLE_VALA_BINDINGS "Build the experimental Vala bindings" OFF)

if(ENABLE_VALA_BINDINGS)
	if(NOT ENABLE_INTROSPECTION)
		message(FATAL_ERROR "Vala bindings require introspection generation to be enabled, use -DENABLE_INTROSPECTION=ON to enable it, or disable Vala bindings with -DENABLE_VALA_BINDINGS=OFF")
	endif(NOT ENABLE_INTROSPECTION)

	find_program(VALAC valac)
	if(NOT VALAC)
		message(FATAL_ERROR "The valac not found. Install it or disable Vala bindings with -DENABLE_VALA_BINDINGS=OFF")
	endif(NOT VALAC)

	find_program(VAPIGEN vapigen)
	if(NOT VAPIGEN)
		message(FATAL_ERROR "The vapigen not found. Install it or disable Vala bindings with -DENABLE_VALA_BINDINGS=OFF")
	endif(NOT VAPIGEN)

endif(ENABLE_VALA_BINDINGS)

# Generate the ${PROJECT_NAME}-config.h file
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}-config.h)

print_build_options()

add_pkgconfig_file(evolution-data-server.pc.in evolution-data-server-${API_VERSION}.pc)

add_subdirectory(data)
add_subdirectory(po)
add_subdirectory(src)
add_subdirectory(tests)

# Add it as the last, because it looks for targets defined above
if(ENABLE_GTK_DOC)
	add_subdirectory(docs)
endif(ENABLE_GTK_DOC)

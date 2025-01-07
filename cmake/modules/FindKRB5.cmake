# FindKRB5.cmake
#
# Searches for KRB5 library
#
# The output is:
#    HAVE_KRB5 - set to ON, if Kerberos 5 support is enabled and libraries found
#    HAVE_MIT_KRB5 - set to ON, when found MIT implementation
#    HAVE_HEIMDAL_KRB5 - set to ON, when found Heimdal implementation
#    KRB5_CFLAGS - CFLAGS to use with target_compile_options() and similar commands
#    KRB5_LDFLAGS - LDFLAGS to use with target_link_libraries() and similar commands

include(CheckCSourceCompiles)
include(PkgConfigEx)
include(PrintableOptions)

add_printable_variable_path(WITH_KRB5 "Location of Kerberos 5 install dir, defaults to ON to search for it" "ON")
add_printable_variable_path(WITH_KRB5_INCLUDES "Location of Kerberos 5 headers" "")
add_printable_variable_path(WITH_KRB5_LIBS "Location of Kerberos 5 libraries" "")

if(NOT WITH_KRB5)
	return()
endif(NOT WITH_KRB5)

pkg_check_modules(KRB5 krb5 krb5-gssapi)

if(KRB5_FOUND)
	pkg_check_variable(KRB5_VENDOR krb5 vendor)

	if(KRB5_VENDOR STREQUAL "MIT")
		message(STATUS "Using MIT Kerberos 5 (found by pkg-config)")
		set(WITH_KRB5 ON)
		set(HAVE_KRB5 ON)
		set(HAVE_MIT_KRB5 ON)
		return()
	endif(KRB5_VENDOR STREQUAL "MIT")

	if(KRB5_VENDOR STREQUAL "Heimdal")
		message(STATUS "Using Heimdal KRB5 implementation (found by pkg-config)")
		set(WITH_KRB5 ON)
		set(HAVE_KRB5 ON)
		set(HAVE_HEIMDAL_KRB5 ON)
		return()
	endif(KRB5_VENDOR STREQUAL "Heimdal")

	message(STATUS "Found Kerberos 5 with pkg-config, but unknown vendor '${KRB5_VENDOR}', continue with autodetection")
endif()

string(LENGTH "${CMAKE_BINARY_DIR}" bindirlen)
string(LENGTH "${WITH_KRB5}" maxlen)
if(maxlen LESS bindirlen)
	set(substr "***")
else(maxlen LESS bindirlen)
	string(SUBSTRING "${WITH_KRB5}" 0 ${bindirlen} substr)
endif(maxlen LESS bindirlen)
string(TOUPPER "${WITH_KRB5}" optupper)

if(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))
	set(WITH_KRB5 "/usr")
endif(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))

unset(bindirlen)
unset(maxlen)
unset(substr)
unset(optupper)

set(mit_includes "${WITH_KRB5}/include")
set(mit_libs "-lkrb5 -lk5crypto -lcom_err -lgssapi_krb5")
set(heimdal_includes "${WITH_KRB5}/include/heimdal")
set(heimdal_libs "-lkrb5 -lcrypto -lasn1 -lcom_err -lroken -lgssapi")
set(sun_includes "${WITH_KRB5}/include/kerberosv5")
set(sun_libs "-lkrb5 -lgss")

set(krb_libs "${WITH_KRB5}/lib${LIB_SUFFIX}")

if(NOT (WITH_KRB5_INCLUDES STREQUAL ""))
	set(mit_includes "${WITH_KRB5_INCLUDES}")
	set(heimdal_includes "${WITH_KRB5_INCLUDES}")
	set(sun_includes "${WITH_KRB5_INCLUDES}")
endif(NOT (WITH_KRB5_INCLUDES STREQUAL ""))

if(NOT (WITH_KRB5_LIBS STREQUAL ""))
	set(krb_libs "${WITH_KRB5_LIBS}")
endif(NOT (WITH_KRB5_LIBS STREQUAL ""))

set(CMAKE_REQUIRED_INCLUDES "-I${mit_includes}")
set(CMAKE_REQUIRED_LIBRARIES "-L${krb_libs} ${mit_libs}")
CHECK_C_SOURCE_COMPILES("#include <krb5/krb5.h>
			int main(void) { krb5_init_context (NULL); return 0; }" HAVE_KRB5)

if(HAVE_KRB5)
	set(HAVE_MIT_KRB5 ON)
	message(STATUS "Found MIT Kerberos 5")
else(HAVE_KRB5)
	unset(HAVE_KRB5 CACHE)
	set(CMAKE_REQUIRED_INCLUDES "-I${heimdal_includes}")
	set(CMAKE_REQUIRED_LIBRARIES "-L${krb_libs} ${heimdal_libs}")
	CHECK_C_SOURCE_COMPILES("#include <krb5.h>
				int main(void) { krb5_init_context (NULL); return 0; }" HAVE_KRB5)

	if(HAVE_KRB5)
		set(HAVE_HEIMDAL_KRB5 ON)
		message(STATUS "Found Heimdal Kerberos 5")
	endif(HAVE_KRB5)
endif(HAVE_KRB5)

if(NOT HAVE_KRB5)
	unset(HAVE_KRB5 CACHE)
	set(CMAKE_REQUIRED_INCLUDES "-I${sun_includes}")
	set(CMAKE_REQUIRED_LIBRARIES "-L${krb_libs} ${sun_libs}")
	CHECK_C_SOURCE_COMPILES("#include <krb5/krb5.h>
				int main(void) { krb5_init_context (NULL); return 0; }" HAVE_KRB5)
	if(HAVE_KRB5)
		set(HAVE_SUN_KRB5 ON)
		message(STATUS "Found Sun Kerberos 5")
	endif(HAVE_KRB5)
endif(NOT HAVE_KRB5)

if(HAVE_KRB5)
	set(KRB5_CFLAGS ${CMAKE_REQUIRED_INCLUDES})
	set(KRB5_LDFLAGS ${CMAKE_REQUIRED_LIBRARIES})
else(HAVE_KRB5)
	message(FATAL_ERROR "Failed to find Kerberos 5 libraries. Use -DWITH_KRB5=OFF to disable Kerberos 5 support")
endif(HAVE_KRB5)

unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_INCLUDES)

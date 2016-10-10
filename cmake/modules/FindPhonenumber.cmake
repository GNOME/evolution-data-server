# FindPhonenumber.cmake
#
# Searches for Google's libphonenumber library
#
# Defines -DWITH_PHONENUMBER=PATH variable, which defaults to OFF.
# The output is:
#    ENABLE_PHONENUMBER - ON, when the libphonenumber is used
#    PHONENUMBER_RAW_INPUT_NEEDED - Whether Parse() or ParseAndKeepRawInput() must be used to get the country-code source
#    PHONENUMBER_CXXFLAGS - CXXFLAGS to use with target_compile_options() and similar commands
#    PHONENUMBER_LDFLAGS - LDFLAGS to use with target_link_libraries() and similar commands

include(PrintableOptions)
include(CheckCXXSourceCompiles)

add_printable_variable_path(WITH_PHONENUMBER "Path prefix where the libphonenumber is installed" OFF)

if(NOT WITH_PHONENUMBER)
	return()
endif(NOT WITH_PHONENUMBER)

set(PHONENUMBER_CXXFLAGS -DI18N_PHONENUMBERS_USE_BOOST)
set(PHONENUMBER_LDFLAGS -lphonenumber)

string(LENGTH "${CMAKE_BINARY_DIR}" bindirlen)
string(SUBSTRING "${WITH_PHONENUMBER}" 0 ${bindirlen} substr)
string(TOUPPER "${WITH_PHONENUMBER}" optupper)

if(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))
	set(WITH_PHONENUMBER "ON")
else(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))
	set(PHONENUMBER_CXXFLAGS "-I${WITH_PHONENUMBER}/include ${PHONENUMBER_CXXFLAGS}")
	set(PHONENUMBER_LDFLAGS "-L${WITH_PHONENUMBER}/lib${LIB_SUFFIX} ${PHONENUMBER_LDFLAGS}")
endif(("${optupper}" STREQUAL "ON") OR ("${substr}" STREQUAL "${CMAKE_BINARY_DIR}"))

unset(bindirlen)
unset(substr)
unset(optupper)

set(CMAKE_REQUIRED_FLAGS "${PHONENUMBER_CXXFLAGS}")

foreach(lib boost_thread-mt boost_thread)
	set(CMAKE_REQUIRED_LIBRARIES "${PHONENUMBER_LDFLAGS} -l${lib}")
	CHECK_CXX_SOURCE_COMPILES("#include <phonenumbers/phonenumberutil.h>

				int main(void) {
					i18n::phonenumbers::PhoneNumberUtil::GetInstance();
					return 0;
				}" phone_number_with_${lib})
	if(phone_number_with_${lib})
		set(ENABLE_PHONENUMBER ON)
		set(PHONENUMBER_LDFLAGS "${CMAKE_REQUIRED_LIBRARIES}")
		break()
	endif(phone_number_with_${lib})
endforeach(lib)

if(NOT ENABLE_PHONENUMBER)
	message(FATAL_ERROR "libphonenumber cannot be used. Use -DWITH_PHONENUMBER=PATH to specify the library prefix, or -DWITH_PHONENUMBER=OFF to disable it.")
endif(NOT ENABLE_PHONENUMBER)

CHECK_CXX_SOURCE_COMPILES("#include <phonenumbers/phonenumberutil.h>

			int main(void) {
				namespace pn = i18n::phonenumbers;

				pn::PhoneNumber n;

				if (pn::PhoneNumberUtil::GetInstance ()->
					Parse(\"049(800)46663\", \"DE\", &n) == pn::PhoneNumberUtil::NO_PARSING_ERROR
					&& n.has_country_code_source ()
					&& n.country_code_source () == 49)
					return EXIT_SUCCESS;

				return EXIT_FAILURE;
			}" PHONENUMBER_RAW_INPUT_NEEDED)

unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LIBRARIES)

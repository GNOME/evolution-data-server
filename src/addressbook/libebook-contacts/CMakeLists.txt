glib_mkenums(e-book-contacts-enumtypes e-book-contacts-types.h E_BOOK_CONTACTS_ENUMTYPES_H)

add_pkgconfig_file(libebook-contacts.pc.in libebook-contacts-${API_VERSION}.pc)

add_executable(gen-western-table
	gen-western-table.c
)

target_compile_definitions(gen-western-table PRIVATE
	-DG_LOG_DOMAIN=\"gen-western-table\"
)

target_compile_options(gen-western-table PUBLIC
	${GNOME_PLATFORM_CFLAGS}
)

target_include_directories(gen-western-table PUBLIC
	${CMAKE_BINARY_DIR}
	${GNOME_PLATFORM_INCLUDE_DIRS}
)

target_link_libraries(gen-western-table
	${GNOME_PLATFORM_LDFLAGS}
)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/e-name-western-tables.h
	COMMAND ${CMAKE_CURRENT_BINARY_DIR}/gen-western-table "${CMAKE_CURRENT_SOURCE_DIR}/e-name-western-tables.h.in" >${CMAKE_CURRENT_BINARY_DIR}/e-name-western-tables.h
	DEPENDS gen-western-table e-name-western-tables.h.in
)

set(DEPENDENCIES
	camel
	edataserver
)

set(SOURCES
	e-address-western.c
	e-book-contacts-types.c
	e-book-query.c
	e-contact.c
	e-name-western.c
	e-phone-number.c
	e-phone-number-private.h
	e-source-backend-summary-setup.c
	e-vcard.c
	${CMAKE_CURRENT_BINARY_DIR}/e-book-contacts-enumtypes.c
	${CMAKE_CURRENT_BINARY_DIR}/e-name-western-tables.h
)

set(HEADERS
	libebook-contacts.h
	e-address-western.h
	e-book-contacts-types.h
	e-book-query.h
	e-contact.h
	e-name-western.h
	e-phone-number.h
	e-source-backend-summary-setup.h
	e-vcard.h
	${CMAKE_CURRENT_BINARY_DIR}/e-book-contacts-enumtypes.h
)

if(ENABLE_PHONENUMBER)
	# We put the C++ code into a separate static library, so that we can use
	# the C linker for libebook-contacts. This avoids that libebook-contacts
	# depends on the C++ Standard Library, even if phone number support is
	# disabled.

	add_library(ephonenumber-private STATIC
		e-phone-number-private.cpp
	)

	target_compile_definitions(ephonenumber-private PRIVATE
		-DG_LOG_DOMAIN=\"e-phone-number\"
		-DLIBEBOOK_CONTACTS_COMPILATION
		${PHONENUMBER_DEFINITIONS}
	)

	target_compile_options(ephonenumber-private PUBLIC
		${ADDRESSBOOK_CFLAGS}
	)

	target_include_directories(ephonenumber-private PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_CURRENT_BINARY_DIR}
		${ADDRESSBOOK_INCLUDE_DIRS}
		${PHONENUMBER_INCLUDE_DIRS}
	)

	target_link_libraries(ephonenumber-private
		${ADDRESSBOOK_LDFLAGS}
		${PHONENUMBER_LDFLAGS}
	)

	list(APPEND DEPENDENCIES
		ephonenumber-private
	)
endif(ENABLE_PHONENUMBER)

add_library(ebook-contacts SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(ebook-contacts
	${DEPENDENCIES}
)

set_target_properties(ebook-contacts PROPERTIES
	VERSION "${LIBEBOOK_CONTACTS_CURRENT}.${LIBEBOOK_CONTACTS_REVISION}.${LIBEBOOK_CONTACTS_AGE}"
	SOVERSION ${LIBEBOOK_CONTACTS_CURRENT}
	OUTPUT_NAME ebook-contacts-${API_VERSION}
)

target_compile_definitions(ebook-contacts PRIVATE
	-DG_LOG_DOMAIN=\"libebook-contacts\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DLIBEBOOK_CONTACTS_COMPILATION
)

target_compile_options(ebook-contacts PUBLIC
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(ebook-contacts PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/addressbook/libegdbus
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook/libegdbus
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(ebook-contacts
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
)

install(TARGETS ebook-contacts
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libebook-contacts
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 GLib-2.0 Gio-2.0 libxml2-2.0)
set(gir_cflags
	-DLIBEBOOK_CONTACTS_COMPILATION
	-I${CMAKE_BINARY_DIR}/src/addressbook
	-I${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
	-I${CMAKE_SOURCE_DIR}/src/addressbook
	-I${CMAKE_SOURCE_DIR}/src/addressbook/libebook-contacts
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)
set(gir_libs
	ebook-contacts
	edataserver
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
)

gir_add_introspection_simple(
	EBookContacts
	libebook-contacts
	${API_VERSION}
	"libebook-contacts/libebook-contacts.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

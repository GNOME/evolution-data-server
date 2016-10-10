glib_mkenums(e-book-enumtypes e-book-types.h E_BOOK_ENUMTYPES_H)

add_pkgconfig_file(libebook.pc.in libebook-${API_VERSION}.pc)

set(DEPENDENCIES
	camel
	ebackend
	ebook-contacts
	edbus-private
	edataserver
	edata-book
	egdbus-book
)

set(SOURCES
	e-book.c
	e-book-client.c
	e-book-client-cursor.c
	e-book-client-view.c
	e-book-view-private.h
	e-book-view.c
	e-destination.c
	e-error.h
	${CMAKE_CURRENT_BINARY_DIR}/e-book-enumtypes.c
)

set(HEADERS
	libebook.h
	e-book.h
	e-book-client.h
	e-book-client-cursor.h
	e-book-client-view.h
	e-book-view.h
	e-book-types.h
	e-destination.h
	${CMAKE_CURRENT_BINARY_DIR}/e-book-enumtypes.h
)

add_library(ebook SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(ebook
	${DEPENDENCIES}
)

set_target_properties(ebook PROPERTIES
	VERSION "${LIBEBOOK_CURRENT}.${LIBEBOOK_REVISION}.${LIBEBOOK_AGE}"
	SOVERSION ${LIBEBOOK_CURRENT}
	OUTPUT_NAME ebook-${API_VERSION}
)

target_compile_definitions(ebook PRIVATE
	-DG_LOG_DOMAIN=\"libebook\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DLIBEBOOK_COMPILATION
)

target_compile_options(ebook PUBLIC
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(ebook PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/addressbook
	${CMAKE_BINARY_DIR}/addressbook/libegdbus
	${CMAKE_BINARY_DIR}/private
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/private
	${CMAKE_SOURCE_DIR}/addressbook
	${CMAKE_SOURCE_DIR}/addressbook/libegdbus
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(ebook
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
)

install(TARGETS ebook
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libebook
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 GLib-2.0 Gio-2.0 libxml2-2.0)
set(gir_cflags
	${CAMEL_CFLAGS} ${DATA_SERVER_CFLAGS}
	-DLIBEBOOK_COMPILATION
	-I${CMAKE_BINARY_DIR}/addressbook
	-I${CMAKE_BINARY_DIR}/addressbook/libebook
	-I${CMAKE_SOURCE_DIR}/addressbook
	-I${CMAKE_SOURCE_DIR}/addressbook/libebook
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/private
	${CMAKE_BINARY_DIR}/libedataserver
	${CMAKE_BINARY_DIR}/addressbook/libebook-contacts
	${CMAKE_BINARY_DIR}/addressbook/libedata-book
)
set(gir_libs
	ebook
	ebook-contacts
	edata-book
	edataserver
)
set(gir_deps
	${CMAKE_BINARY_DIR}/libedataserver/EDataServer-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/addressbook/libebook-contacts/EBookContacts-${API_VERSION}.gir
)

gir_add_introspection_simple(
	EBook
	libebook
	${API_VERSION}
	"libebook/libebook.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

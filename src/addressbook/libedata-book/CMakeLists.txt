add_pkgconfig_file(libedata-book.pc.in libedata-book-${API_VERSION}.pc)

set(DEPENDENCIES
	camel
	ebackend
	ebook-contacts
	edbus-private
	edataserver
)

set(SOURCES
	e-book-backend.c
	e-book-backend-cache.c
	e-book-backend-factory.c
	e-book-backend-sexp.c
	e-book-backend-sqlitedb.c
	e-book-backend-summary.c
	e-book-backend-sync.c
	e-book-cache.c
	e-book-meta-backend.c
	e-book-sqlite.c
	e-data-book.c
	e-data-book-cursor.c
	e-data-book-cursor-cache.c
	e-data-book-cursor-sqlite.c
	e-data-book-direct.c
	e-data-book-factory.c
	e-data-book-view.c
	e-subprocess-book-factory.c
	e-system-locale-watcher.c
	ximian-vcard.h
)

set(HEADERS
	libedata-book.h
	e-book-backend.h
	e-book-backend-cache.h
	e-book-backend-factory.h
	e-book-backend-sexp.h
	e-book-backend-sqlitedb.h
	e-book-backend-summary.h
	e-book-backend-sync.h
	e-book-cache.h
	e-book-meta-backend.h
	e-book-sqlite.h
	e-data-book-factory.h
	e-data-book-view.h
	e-data-book.h
	e-data-book-cursor.h
	e-data-book-cursor-cache.h
	e-data-book-cursor-sqlite.h
	e-data-book-direct.h
	e-subprocess-book-factory.h
	e-system-locale-watcher.h
)

if(WITH_LIBDB)
	list(APPEND SOURCES
		e-book-backend-db-cache.c
	)

	list(APPEND HEADERS
		e-book-backend-db-cache.h
	)
endif(WITH_LIBDB)

add_library(edata-book SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(edata-book
	${DEPENDENCIES}
)

set_target_properties(edata-book PROPERTIES
	VERSION "${LIBEDATABOOK_CURRENT}.${LIBEDATABOOK_REVISION}.${LIBEDATABOOK_AGE}"
	SOVERSION ${LIBEDATABOOK_CURRENT}
	OUTPUT_NAME edata-book-${API_VERSION}
)

target_compile_definitions(edata-book PRIVATE
	-DG_LOG_DOMAIN=\"libedata-book\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DSUBPROCESS_BOOK_BACKEND_PATH=\"${LIBEXEC_INSTALL_DIR}/evolution-addressbook-factory-subprocess\"
	-DLIBEDATA_BOOK_COMPILATION
)

target_compile_options(edata-book PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBDB_CFLAGS}
	${SQLITE3_CFLAGS}
)

target_include_directories(edata-book PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBDB_INCLUDE_DIRS}
	${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(edata-book
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LIBDB_LIBS}
	${SQLITE3_LDFLAGS}
)

install(TARGETS edata-book
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedata-book
)

add_executable(e-book-backend-sqlitedb-test EXCLUDE_FROM_ALL e-book-backend-sqlitedb-test.c)

target_compile_definitions(e-book-backend-sqlitedb-test PRIVATE
	-DG_LOG_DOMAIN=\"libedata-book\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
	-DSUBPROCESS_BOOK_BACKEND_PATH=\"${LIBEXEC_INSTALL_DIR}/evolution-addressbook-factory-subprocess\"
	-DLIBEDATA_BOOK_COMPILATION
)

target_compile_options(e-book-backend-sqlitedb-test PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBDB_CFLAGS}
	${SQLITE3_CFLAGS}
)

target_include_directories(e-book-backend-sqlitedb-test PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBDB_INCLUDE_DIRS}
	${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(e-book-backend-sqlitedb-test
	edata-book
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LIBDB_LIBS}
	${SQLITE3_LDFLAGS}
)

set(DEPENDENCIES
	ebackend
	edataserver
	edata-book
	edbus-private
)

add_executable(evolution-addressbook-factory-subprocess
	evolution-addressbook-factory-subprocess.c)

target_compile_definitions(evolution-addressbook-factory-subprocess PRIVATE
	-DG_LOG_DOMAIN=\"evolution-addressbook-factory-subprocess\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-addressbook-factory-subprocess PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-addressbook-factory-subprocess PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-addressbook-factory-subprocess
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-addressbook-factory-subprocess
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 GLib-2.0 Gio-2.0 libxml2-2.0)
set(gir_cflags
	-DLIBEDATA_BOOK_COMPILATION
	-DEDS_DISABLE_DEPRECATED=1
	-I${CMAKE_BINARY_DIR}/src/addressbook
	-I${CMAKE_BINARY_DIR}/src/addressbook/libedata-book
	-I${CMAKE_SOURCE_DIR}/src/addressbook
	-I${CMAKE_SOURCE_DIR}/src/addressbook/libedata-book
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libebackend
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
	${CMAKE_BINARY_DIR}/src/addressbook/libedata-book
	${PHONENUMBER_LIB_DIRS}
)
set(gir_libs
	camel
	ebook-contacts
	edata-book
	edataserver
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libebackend/EBackend-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts/EBookContacts-${API_VERSION}.gir
)

gir_add_introspection_simple(
	EDataBook
	libedata-book
	${API_VERSION}
	"libedata-book/libedata-book.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

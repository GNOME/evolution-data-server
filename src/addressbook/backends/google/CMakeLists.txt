set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

add_library(ebookbackendgoogle MODULE
	e-book-backend-google-factory.c
	e-book-backend-google.c
	e-book-backend-google.h
	e-book-google-utils.c
	e-book-google-utils.h
	e-gdata-oauth2-authorizer.c
	e-gdata-oauth2-authorizer.h
)

add_dependencies(ebookbackendgoogle
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendgoogle PRIVATE
	-DG_LOG_DOMAIN=\"e-book-backend-google\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
)

target_compile_options(ebookbackendgoogle PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBGDATA_CFLAGS}
)

target_include_directories(ebookbackendgoogle PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBGDATA_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendgoogle
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LIBGDATA_LDFLAGS}
)

install(TARGETS ebookbackendgoogle
	DESTINATION ${ebook_backenddir}
)

# Private utility library.
# This is split out to allow it to be unit tested.

set(SOURCES
	e-book-google-utils.c
	e-book-google-utils.h
)

add_library(ebook-google-utils STATIC
	${SOURCES}
)

add_dependencies(ebook-google-utils
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

target_compile_definitions(ebook-google-utils PRIVATE
	-DG_LOG_DOMAIN=\"e-book-google-utils\"
)

target_compile_options(ebook-google-utils PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBGDATA_CFLAGS}
)

target_include_directories(ebook-google-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBGDATA_INCLUDE_DIRS}
)

target_link_libraries(ebook-google-utils
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
	${ADDRESSBOOK_LDFLAGS}
	${LIBGDATA_LDFLAGS}
)

add_subdirectory(tests)

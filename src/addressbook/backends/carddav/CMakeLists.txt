set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

add_library(ebookbackendcarddav MODULE
	e-book-backend-carddav.c
	e-book-backend-carddav.h
	e-book-backend-carddav-factory.c
)

add_dependencies(ebookbackendcarddav
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendcarddav PRIVATE
	-DG_LOG_DOMAIN=\"e-book-backend-carddav\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
)

target_compile_options(ebookbackendcarddav PUBLIC
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(ebookbackendcarddav PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendcarddav
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
)

install(TARGETS ebookbackendcarddav
	DESTINATION ${ebook_backenddir}
)

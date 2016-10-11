set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

add_library(ebookbackendwebdav MODULE
	e-book-backend-webdav.c
	e-book-backend-webdav.h
	e-book-backend-webdav-factory.c
)

add_dependencies(ebookbackendwebdav
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendwebdav PRIVATE
	-DG_LOG_DOMAIN=\"e-book-backend-webdav\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
)

target_compile_options(ebookbackendwebdav PUBLIC
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(ebookbackendwebdav PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendwebdav
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
)

install(TARGETS ebookbackendwebdav
	DESTINATION ${ebook_backenddir}
)

add_executable(dump-vcard
	dump-vcard.c
)

set(DEPENDENCIES
	ebook
	edataserver
)

add_dependencies(dump-vcard
	${DEPENDENCIES}
)

target_compile_definitions(dump-vcard PRIVATE
	-DG_LOG_DOMAIN=\"dump-vcard\"
)

target_compile_options(dump-vcard PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(dump-vcard PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/addressbook
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(dump-vcard
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${ADDRESSBOOK_LDFLAGS}
)

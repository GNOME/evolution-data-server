set(DEPENDENCIES
	ebook-google-utils
)

add_executable(ebookbackendgoogle-phonenumber
	phone-numbers.c
)

add_dependencies(ebookbackendgoogle-phonenumber
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendgoogle-phonenumber PRIVATE
	-DG_LOG_DOMAIN=\"ebookbackendgoogle-phonenumber\"
)

target_compile_options(ebookbackendgoogle-phonenumber PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBGDATA_CFLAGS}
)

target_include_directories(ebookbackendgoogle-phonenumber PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/addressbook/backends/google
	${CMAKE_CURRENT_SOURCE_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBGDATA_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendgoogle-phonenumber
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LIBGDATA_LDFLAGS}
)

add_check_test(ebookbackendgoogle-phonenumber)

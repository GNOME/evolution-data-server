set(extra_deps
	ebook
	ebook-contacts
)

set(extra_defines
	-DSRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
	-DINSTALLED_TEST_DIR=\"${INSTALLED_TESTS_EXEC_DIR}\"
)

set(extra_cflags
	${ADDRESSBOOK_CFLAGS}
	${GIO_UNIX_CFLAGS}
)

set(extra_incdirs
	${ADDRESSBOOK_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
)

set(extra_ldflags
	${ADDRESSBOOK_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
)

set(SOURCES
	ebook-test-utils.c
	ebook-test-utils.h
)

add_library(ebook-test-utils STATIC
	${SOURCES}
)

add_dependencies(ebook-test-utils
	edataserver
	${extra_deps}
)

target_compile_definitions(ebook-test-utils PRIVATE
	-DG_LOG_DOMAIN=\"ebook-test-utils\"
	${extra_defines}
)

target_compile_options(ebook-test-utils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(ebook-test-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(ebook-test-utils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

set(extra_deps
	ebook
	ebook-contacts
	ebook-test-utils
)

set(extra_defines)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-ebook-add-contact
	test-ebook-get-contact
	test-ebook-commit-contact
	test-ebook-remove-contact
	test-ebook-get-required-fields
	test-ebook-get-static-capabilities
	test-ebook-get-supported-fields
	test-ebook-get-supported-auth-methods
	test-ebook-remove-contact-by-id
	test-ebook-remove-contacts
	test-ebook-get-book-view
)

foreach(_test ${TESTS})
	set(SOURCES ${_test}.c)

	add_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
		"session-exclusive"
		"TEST_INSTALLED_SERVICES=1"
	)
endforeach(_test)

if(ENABLE_INSTALLED_TESTS)
	file(GLOB VCARDS ${CMAKE_CURRENT_SOURCE_DIR}/data/vcards/*.vcf)

	install(FILES ${VCARDS}
		DESTINATION ${INSTALLED_TESTS_EXEC_DIR}/vcards
	)
endif(ENABLE_INSTALLED_TESTS)

add_subdirectory(client)
add_subdirectory(vcard)

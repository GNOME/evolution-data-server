set(extra_deps
	ebook
	ebook-contacts
	edbus-private
	etestserverutils
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
	client-test-utils.c
	client-test-utils.h
)

add_library(client-test-utils STATIC
	${SOURCES}
)

add_dependencies(client-test-utils
	edataserver
	${extra_deps}
)

target_compile_definitions(client-test-utils PRIVATE
	-DG_LOG_DOMAIN=\"client-test-utils\"
	${extra_defines}
)

target_compile_options(client-test-utils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(client-test-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/tests/test-server-utils
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_SOURCE_DIR}/tests/test-server-utils
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(client-test-utils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

set(extra_deps
	client-test-utils
	ebook
	ebook-contacts
	edbus-private
	etestserverutils
)

set(extra_defines)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-book-client-refresh
	test-book-client-add-contact
	test-book-client-get-contact
	test-book-client-get-contact-uids
	test-book-client-modify-contact
	test-book-client-remove-contact
	test-book-client-remove-contact-by-uid
	test-book-client-remove-contacts
	test-book-client-add-and-get-sync
	test-book-client-add-and-get-async
	test-book-client-self
	test-book-client-preserve-uid
	test-book-client-photo-is-uri
	test-book-client-e164-param
        test-book-client-custom-summary
	test-book-client-get-revision
	test-book-client-write-write
	test-book-client-get-view
	test-book-client-uid-only-view
	test-book-client-revision-view
	test-book-client-view-operations
	test-book-client-suppress-notifications
	test-book-client-cursor-create
)

# Skip these from 'make check' and installed tests, they take too long or require special setup
set(TESTS_SKIP
	test-book-client-cursor-operations
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

foreach(_test ${TESTS_SKIP})
	set(SOURCES ${_test}.c)

	build_only_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
	)
endforeach(_test)

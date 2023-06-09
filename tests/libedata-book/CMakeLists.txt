set(extra_deps
	camel
	ebook
	ebook-contacts
	edata-book
	edbus-private
	etestserverutils
)

set(extra_defines
	-DINSTALLED_TEST_DIR=\"${INSTALLED_TESTS_EXEC_DIR}\"
)

set(extra_cflags
	${ADDRESSBOOK_CFLAGS}
	${CAMEL_CFLAGS}
	${GIO_UNIX_CFLAGS}
)

set(extra_incdirs
	${ADDRESSBOOK_INCLUDE_DIRS}
	${CAMEL_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
)

set(extra_ldflags
	${ADDRESSBOOK_LDFLAGS}
	${CAMEL_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
)

set(SOURCES
	data-test-utils.c
	data-test-utils.h
	test-book-cache-utils.c
	test-book-cache-utils.h
)

add_library(data-book-test-utils STATIC
	${SOURCES}
)

add_dependencies(data-book-test-utils
	edataserver
	${extra_deps}
)

target_compile_definitions(data-book-test-utils PRIVATE
	-DG_LOG_DOMAIN=\"data-book-test-utils\"
	${extra_defines}
)

target_compile_options(data-book-test-utils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(data-book-test-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(data-book-test-utils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

set(extra_deps
	ebook
	ebook-contacts
	data-book-test-utils
)

set(extra_defines)

# Should be kept ordered approximately from least to most difficult/complex
#
# Note that these tests must be run in order:
#   test-sqlite-cursor-posix-initial,
#   test-sqlite-cursor-en-US-migrated,
#   test-sqlite-cursor-posix-migrated
#
# This is because each migrated test changes the
# locale and reloads the same addressbook of the previous test.
set(TESTS
	test-book-cache-get-contact
	test-book-cache-create-cursor
	test-book-cache-cursor-move-by-posix
	test-book-cache-cursor-move-by-en-US
	test-book-cache-cursor-move-by-fr-CA
	test-book-cache-cursor-move-by-de-DE
	test-book-cache-cursor-set-target
	test-book-cache-cursor-calculate
	test-book-cache-cursor-set-sexp
	test-book-cache-cursor-change-locale
	test-book-cache-offline
	test-book-cache-query
	test-book-meta-backend
	test-sqlite-get-contact
	test-sqlite-create-cursor
	test-sqlite-cursor-move-by-posix
	test-sqlite-cursor-move-by-en-US
	test-sqlite-cursor-move-by-fr-CA
	test-sqlite-cursor-move-by-de-DE
	test-sqlite-cursor-set-target
	test-sqlite-cursor-calculate
	test-sqlite-cursor-set-sexp
	test-sqlite-cursor-change-locale
	test-sqlite-query
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
		--data-dir "${CMAKE_CURRENT_SOURCE_DIR}/../libebook/data/vcards"
	)
endforeach(_test)

if(ENABLE_INSTALLED_TESTS)
	file(GLOB VCARDS ${CMAKE_SOURCE_DIR}/tests/libebook/data/vcards/*.vcf)

	install(FILES ${VCARDS}
		DESTINATION ${INSTALLED_TESTS_EXEC_DIR}/vcards
	)
endif(ENABLE_INSTALLED_TESTS)

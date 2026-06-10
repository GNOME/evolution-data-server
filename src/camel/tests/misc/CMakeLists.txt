set(TESTS
	test-camel-misc-references
	test-camel-misc-param-list
	test-camel-misc-hostname
	test-camel-misc-utf7
	test-camel-misc-search-split
	test-camel-misc-rfc2047
)

# enable for `make check`/`make test` and install these tests
set(TESTS_CHECK
	test-camel-misc-text-index
	test-camel-misc-db
	test-camel-misc-folder-thread
	test-camel-misc-store-search
	test-camel-misc-vee-folder
)

set(TESTS_SKIP
	test-camel-misc-url
	test-camel-misc-url-scan
)

add_camel_tests(TESTS ON)
add_camel_tests(TESTS_CHECK ON)
add_camel_tests(TESTS_SKIP OFF)

foreach(_test ${TESTS_CHECK})
	add_check_test(${_test})
	set_tests_properties(${_test} PROPERTIES ENVIRONMENT "GSETTINGS_SCHEMA_DIR=${CMAKE_BINARY_DIR}/data")
	install_test_if_enabled(${_test} "session-exclusive" "TEST_INSTALLED_SERVICES=1")
endforeach(_test)

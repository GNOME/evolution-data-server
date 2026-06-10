set(TESTS_CHECK
	test-camel-folder-body-search
)

set(TESTS_SKIP
	test-camel-folder-store
	test-camel-folder-ops
	test-camel-folder-search
	test-camel-folder-store-imap
	test-camel-folder-store-nntp
	test-camel-folder-ops-imap
	test-camel-folder-ops-nntp
	test-camel-folder-threaded
	test-camel-folder-index
	test-camel-folder-thread-ops
	test-camel-folder-thread-sexp
)

add_camel_tests(TESTS_CHECK ON)
add_camel_tests(TESTS_SKIP OFF)

foreach(_test ${TESTS_CHECK})
	add_check_test(${_test})
	set_tests_properties(${_test} PROPERTIES ENVIRONMENT "GSETTINGS_SCHEMA_DIR=${CMAKE_BINARY_DIR}/data")
	install_test_if_enabled(${_test} "session-exclusive" "")
endforeach(_test)

set(TESTS
	test1
	test2
	test3
	utf7
	split
	rfc2047
)

# enable for `make check`/`make test` and install these tests
set(TESTS_CHECK
	test-camel-db
	test-folder-thread
	test-store-search
	test-vee-folder
)

set(TESTS_SKIP
	url
	url-scan
)

add_camel_tests(misc TESTS ON)
add_camel_tests(misc TESTS_CHECK ON)
add_camel_tests(misc TESTS_SKIP OFF)

foreach(_test ${TESTS_CHECK})
	add_check_test(cameltest-misc-${_test})
	install_test_if_enabled(cameltest-misc-${_test} "session-exclusive" "TEST_INSTALLED_SERVICES=1")
endforeach(_test)

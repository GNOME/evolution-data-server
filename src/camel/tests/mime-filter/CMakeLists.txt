set(TESTS
	test1
	test-crlf
	test-tohtml
)

set(TESTS_SKIP
	test-charset
)

add_camel_tests(mime-filter TESTS ON)
add_camel_tests(mime-filter TESTS_SKIP OFF)

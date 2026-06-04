set(TESTS
	test-camel-mime-filter-canon
	test-camel-mime-filter-crlf
	test-camel-mime-filter-tohtml
)

set(TESTS_SKIP
	test-camel-mime-filter-charset
)

add_camel_tests(TESTS ON)
add_camel_tests(TESTS_SKIP OFF)

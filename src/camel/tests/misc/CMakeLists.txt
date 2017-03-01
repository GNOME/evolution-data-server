set(TESTS
	test1
	test2
	utf7
	split
	rfc2047
)

set(TESTS_SKIP
	url
	url-scan
)

add_camel_tests(misc TESTS ON)
add_camel_tests(misc TESTS_SKIP OFF)

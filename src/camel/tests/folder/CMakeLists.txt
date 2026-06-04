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

add_camel_tests(TESTS_SKIP OFF)

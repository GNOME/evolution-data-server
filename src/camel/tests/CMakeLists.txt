macro(add_camel_test_one _name _src_file _in_check)
	# Not using EXCLUDE_FROM_ALL here, to have these built always
	add_executable(${_name} ${_src_file})
	add_dependencies(${_name} camel)

	target_compile_definitions(${_name} PRIVATE
		-DG_LOG_DOMAIN=\"${_name}\"
	)

	target_compile_options(${_name} PUBLIC
		${CAMEL_CFLAGS}
	)

	target_include_directories(${_name} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_BINARY_DIR}/src/camel
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/src/camel
		${CMAKE_SOURCE_DIR}/src/camel/tests/lib
		${CAMEL_INCLUDE_DIRS}
	)

	target_link_libraries(${_name}
		camel
		cameltest
		cameltest-provider
		${CAMEL_LDFLAGS}
	)

	if(${_in_check})
		add_check_test(${_name} --data-dir "${CMAKE_CURRENT_SOURCE_DIR}/data")
		set_tests_properties(${_name} PROPERTIES ENVIRONMENT "GSETTINGS_SCHEMA_DIR=${CMAKE_BINARY_DIR}/data")
		install_test_if_enabled(${_name} "session-exclusive" "TEST_INSTALLED_SERVICES=1")
	endif(${_in_check})
endmacro(add_camel_test_one)

macro(add_camel_tests _tests _in_check)
	foreach(test IN LISTS ${_tests})
		if(NOT "${test}" STREQUAL "")
			add_camel_test_one(${test} ${test}.c ${_in_check})
		endif(NOT "${test}" STREQUAL "")
	endforeach(test)
endmacro(add_camel_tests)

add_subdirectory(lib)

# Tests that are run via `make check` / `ctest`
set(TESTS
	test-camel-references
	test-camel-param-list
	test-camel-hostname
	test-camel-utf7
	test-camel-search-split
	test-camel-rfc2047
	test-camel-mime-filter-canon
	test-camel-mime-filter-crlf
	test-camel-mime-filter-tohtml
	test-camel-text-index
	test-camel-db
	test-camel-folder-thread
	test-camel-store-search
	test-camel-vee-folder
	test-camel-folder-body-search
	test-camel-maildir-flags
	test-camel-provider-imapx
)

# Tests that are built but not run automatically
set(TESTS_SKIP
	test-camel-url
	test-camel-url-scan
	test-camel-mime-filter-charset
	test-camel-message-stream
	test-camel-message-address
	test-camel-message-parser
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
	test-camel-smime-pgp
	test-camel-smime-pgp-mime
	test-camel-smime-pkcs7
)

add_camel_tests(TESTS ON)
add_camel_tests(TESTS_SKIP OFF)

macro(add_camel_test_one _part _name _src_file)
	set(_test_ident cameltest-${_part}-${_name})

	add_executable(${_test_ident} EXCLUDE_FROM_ALL ${_src_file})

	target_compile_definitions(${_test_ident} PRIVATE
		-DG_LOG_DOMAIN=\"${_test_ident}\"
		-DSOURCEDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
		-DTEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/camel/tests/data\"
	)

	target_compile_options(${_test_ident} PUBLIC
		${CAMEL_CFLAGS}
	)

	target_include_directories(${_test_ident} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/camel
		${CMAKE_SOURCE_DIR}
		${CMAKE_SOURCE_DIR}/camel
		${CMAKE_SOURCE_DIR}/camel/tests/lib
		${CAMEL_INCLUDE_DIRS}
	)

	target_link_libraries(${_test_ident}
		camel
		cameltest
		cameltest-provider
		${CAMEL_LDFLAGS}
	)

	set_target_properties(${_test_ident} PROPERTIES
		OUTPUT_NAME ${_name}}
	)

	add_check_test(${_test_ident})
endmacro(add_camel_test_one)

macro(add_camel_tests _part _tests)
	foreach(test IN LISTS ${_tests})
		if(NOT "${test}" STREQUAL "")
			add_camel_test_one(${_part} ${test} ${test}.c)
		endif(NOT "${test}" STREQUAL "")
	endforeach(test)
endmacro(add_camel_tests)

add_subdirectory(lib)
add_subdirectory(message)
add_subdirectory(folder)
add_subdirectory(smime)
add_subdirectory(misc)
add_subdirectory(mime-filter)

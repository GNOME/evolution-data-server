macro(add_camel_test_one _part _name _src_file _in_check)
	set(_test_ident cameltest-${_part}-${_name})

	# Not using EXCLUDE_FROM_ALL here, to have these built always
	add_executable(${_test_ident} ${_src_file})

	target_compile_definitions(${_test_ident} PRIVATE
		-DG_LOG_DOMAIN=\"${_test_ident}\"
		-DSOURCEDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
		-DTEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/src/camel/tests/data\"
	)

	target_compile_options(${_test_ident} PUBLIC
		${CAMEL_CFLAGS}
	)

	target_include_directories(${_test_ident} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_BINARY_DIR}/src/camel
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/src/camel
		${CMAKE_SOURCE_DIR}/src/camel/tests/lib
		${CAMEL_INCLUDE_DIRS}
	)

	target_link_libraries(${_test_ident}
		camel
		cameltest
		cameltest-provider
		${CAMEL_LDFLAGS}
	)

	set_target_properties(${_test_ident} PROPERTIES
		OUTPUT_NAME ${_name}
	)

	if(_in_check)
		add_check_test(${_test_ident})
	endif(_in_check)
endmacro(add_camel_test_one)

macro(add_camel_tests _part _tests _in_check)
	foreach(test IN LISTS ${_tests})
		if(NOT "${test}" STREQUAL "")
			add_camel_test_one(${_part} ${test} ${test}.c ${_in_check})
		endif(NOT "${test}" STREQUAL "")
	endforeach(test)
endmacro(add_camel_tests)

add_subdirectory(lib)
add_subdirectory(message)
add_subdirectory(folder)
add_subdirectory(smime)
add_subdirectory(misc)
add_subdirectory(mime-filter)

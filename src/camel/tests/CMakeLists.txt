macro(add_camel_test_one _name _src_file _in_check)
	# Not using EXCLUDE_FROM_ALL here, to have these built always
	add_executable(${_name} ${_src_file})
	add_dependencies(${_name} camel)

	target_compile_definitions(${_name} PRIVATE
		-DG_LOG_DOMAIN=\"${_name}\"
		-DSOURCEDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
		-DTEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/src/camel/tests/data\"
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

	if(_in_check)
		add_check_test(${_name})
	endif(_in_check)
endmacro(add_camel_test_one)

macro(add_camel_tests _tests _in_check)
	foreach(test IN LISTS ${_tests})
		if(NOT "${test}" STREQUAL "")
			add_camel_test_one(${test} ${test}.c ${_in_check})
		endif(NOT "${test}" STREQUAL "")
	endforeach(test)
endmacro(add_camel_tests)

add_subdirectory(lib)
add_subdirectory(message)
add_subdirectory(folder)
add_subdirectory(smime)
add_subdirectory(misc)
add_subdirectory(mime-filter)

macro(build_only_installable_test _test_ident _sourcesvar _depsvar _defsvar _cflagsvar _incdirsvar _ldflagsvar)
	set(DEPENDENCIES
		edataserver
		etestserverutils
	)

	# Not using EXCLUDE_FROM_ALL here, to have these built always
	add_executable(${_test_ident} ${${_sourcesvar}})

	add_dependencies(${_test_ident}
		${DEPENDENCIES}
		${${_depsvar}}
	)

	target_compile_definitions(${_test_ident} PRIVATE
		-DG_LOG_DOMAIN=\"${_test_ident}\"
		-DSRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
		-DINSTALLED_TEST_DIR=\"${INSTALLED_TESTS_EXEC_DIR}\"
		${${_defsvar}}
	)

	target_compile_options(${_test_ident} PUBLIC
		${BACKEND_CFLAGS}
		${DATA_SERVER_CFLAGS}
		${${_cflagsvar}}
	)

	target_include_directories(${_test_ident} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_BINARY_DIR}/tests/test-server-utils
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/tests/test-server-utils
		${BACKEND_INCLUDE_DIRS}
		${DATA_SERVER_INCLUDE_DIRS}
		${${_incdirsvar}}
	)

	target_link_libraries(${_test_ident}
		${DEPENDENCIES}
		${${_depsvar}}
		${BACKEND_LDFLAGS}
		${DATA_SERVER_LDFLAGS}
		${${_ldflagsvar}}
	)
endmacro(build_only_installable_test)

macro(add_installable_test _test_ident _sourcesvar _depsvar _defsvar _cflagsvar _incdirsvar _ldflagsvar _ittype _itenviron)
	build_only_installable_test(${_test_ident} ${_sourcesvar} ${_depsvar} ${_defsvar} ${_cflagsvar} ${_incdirsvar} ${_ldflagsvar})

	add_check_test(${_test_ident})
	install_test_if_enabled(${_test_ident} ${_ittype} ${_itenviron})
endmacro(add_installable_test)

add_subdirectory(book-migration)
add_subdirectory(libebook)
add_subdirectory(libebook-contacts)
add_subdirectory(libecal)
add_subdirectory(libedata-book)
add_subdirectory(libedata-cal)
add_subdirectory(libedataserver)
add_subdirectory(test-server-utils)

set(extra_deps
	ecal
	etestserverutils
)

set(extra_defines
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

set(extra_cflags
	${CALENDAR_CFLAGS}
)

set(extra_incdirs
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

set(extra_ldflags
	${CALENDAR_LDFLAGS}
)

set(SOURCES
	ecal-test-utils.c
	ecal-test-utils.h
)

add_library(ecal-test-utils STATIC
	${SOURCES}
)

add_dependencies(ecal-test-utils
	edataserver
	${extra_deps}
)

target_compile_definitions(ecal-test-utils PRIVATE
	-DG_LOG_DOMAIN=\"ecal-test-utils\"
	${extra_defines}
)

target_compile_options(ecal-test-utils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(ecal-test-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(ecal-test-utils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

set(extra_deps
	ecal
	ecal-test-utils
)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-ecal-get-timezone
	test-ecal-add-timezone
	test-ecal-set-default-timezone
	test-ecal-recur-description
	test-ecal-get-alarm-email-address
	test-ecal-get-cal-address
	test-ecal-get-ldap-attribute
	test-ecal-get-capabilities
	test-ecal-get-default-object
	test-ecal-create-object
	test-ecal-create-object--2
	test-ecal-get-objects-for-uid
	test-ecal-remove-object
	test-ecal-get-object-list
	test-ecal-modify-object
	test-ecal-send-objects
	test-ecal-receive-objects
	test-ecal-get-query
)

foreach(_test ${TESTS})
	set(SOURCES ${_test}.c)

	add_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
		"session-exclusive"
		"TEST_INSTALLED_SERVICES=1"
	)
endforeach(_test)

# test-ecal-get-free-busy:
#   broken by design, the API needs to be fixed.
set(TESTS_SKIP
	test-ecal-get-free-busy
)

foreach(_test ${TESTS_SKIP})
	set(SOURCES ${_test}.c)

	build_only_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
	)
endforeach(_test)

add_subdirectory(client)

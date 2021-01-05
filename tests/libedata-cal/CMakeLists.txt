set(extra_deps
	ebackend
	ecal
	edata-cal
	etestserverutils
)

set(extra_defines
	-DINSTALLED_TEST_DIR=\"${INSTALLED_TESTS_EXEC_DIR}\"
)

set(extra_cflags
	${CALENDAR_CFLAGS}
)

set(extra_incdirs
	${CALENDAR_INCLUDE_DIRS}
)

set(extra_ldflags
	${CALENDAR_LDFLAGS}
)

set(SOURCES
	test-cal-cache-utils.c
	test-cal-cache-utils.h
)

add_library(data-cal-test-utils STATIC
	${SOURCES}
)

add_dependencies(data-cal-test-utils
	edataserver
	${extra_deps}
)
target_compile_definitions(data-cal-test-utils PRIVATE
	-DG_LOG_DOMAIN=\"data-cal-test-utils\"
	${extra_defines}
)

target_compile_options(data-cal-test-utils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(data-cal-test-utils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(data-cal-test-utils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

set(extra_deps
	ecal
	edata-cal
	data-cal-test-utils
)

set(extra_defines)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-cal-backend-sexp
	test-intervaltree
	test-cal-cache-getters
	test-cal-cache-intervals
	test-cal-cache-offline
	test-cal-cache-search
	test-cal-meta-backend
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
		--data-dir "${CMAKE_CURRENT_SOURCE_DIR}/components"
	)
endforeach(_test)

if(ENABLE_INSTALLED_TESTS)
	file(GLOB COMPONENTS ${CMAKE_CURRENT_SOURCE_DIR}/components/*.ics)

	install(FILES ${COMPONENTS}
		DESTINATION ${INSTALLED_TESTS_EXEC_DIR}/components
	)
endif(ENABLE_INSTALLED_TESTS)

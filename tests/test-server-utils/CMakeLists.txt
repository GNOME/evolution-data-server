add_subdirectory(services)

set(SOURCES
	e-test-server-utils.c
	e-test-server-utils.h
)

set(extra_deps
	ebook
	ecal
)

set(extra_defines)

set(extra_cflags
	${ADDRESSBOOK_CFLAGS}
	${CALENDAR_CFLAGS}
	${GIO_UNIX_CFLAGS}
)

set(extra_incdirs
	${ADDRESSBOOK_INCLUDE_DIRS}
	${CALENDAR_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
)

set(extra_ldflags
	${ADDRESSBOOK_LDFLAGS}
	${CALENDAR_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
)

# ***************************
# Utility library
# ***************************

add_library(etestserverutils SHARED ${SOURCES})

add_dependencies(etestserverutils
	edataserver
	evolution-addressbook-factory
	evolution-calendar-factory
	evolution-source-registry
	${extra_deps}
)

set_target_properties(etestserverutils PROPERTIES
	VERSION "0.0.0"
	SOVERSION 0
)

target_compile_definitions(etestserverutils PRIVATE
	-DG_LOG_DOMAIN=\"e-test-server-utils\"
	${extra_defines}
)

target_compile_options(etestserverutils PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${extra_cflags}
)

target_include_directories(etestserverutils PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${extra_incdirs}
)

target_link_libraries(etestserverutils
	edataserver
	${extra_deps}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${extra_ldflags}
)

if(ENABLE_INSTALLED_TESTS)
	install(TARGETS etestserverutils
		DESTINATION ${LIB_INSTALL_DIR}
	)
endif(ENABLE_INSTALLED_TESTS)

# ***************************
# Local Unit Test
# ***************************

set(SOURCES
	test-fixture.c
)

add_installable_test(test-fixture
	SOURCES
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
	"session-exclusive"
	"TEST_INSTALLED_SERVICES=1"
)

set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-file-factory.c
	e-cal-backend-file-events.c
	e-cal-backend-file-events.h
	e-cal-backend-file-journal.c
	e-cal-backend-file-journal.h
	e-cal-backend-file-todos.c
	e-cal-backend-file-todos.h
	e-cal-backend-file.c
	e-cal-backend-file.h
)

add_library(ecalbackendfile MODULE
	${SOURCES}
)

add_dependencies(ecalbackendfile
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendfile PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-file\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendfile PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendfile PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendfile
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendfile
	DESTINATION ${ecal_backenddir}
)

add_executable(test-interval-searches
	e-cal-backend-file.c
)

target_compile_definitions(test-interval-searches PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-file\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
	-DTEST_QUERY_RESULT=1
)

target_compile_options(test-interval-searches PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(test-interval-searches PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(test-interval-searches
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-http-factory.c
	e-cal-backend-http.c
	e-cal-backend-http.h
)

add_library(ecalbackendhttp MODULE
	${SOURCES}
)

add_dependencies(ecalbackendhttp
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendhttp PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-http\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendhttp PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendhttp PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendhttp
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendhttp
	DESTINATION ${ecal_backenddir}
)

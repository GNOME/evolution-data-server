set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-caldav-factory.c
	e-cal-backend-caldav.c
	e-cal-backend-caldav.h
)

add_library(ecalbackendcaldav MODULE
	${SOURCES}
)

add_dependencies(ecalbackendcaldav
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendcaldav PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-caldav\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendcaldav PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendcaldav PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendcaldav
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendcaldav
	DESTINATION ${ecal_backenddir}
)

set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-gtasks-factory.c
	e-cal-backend-gtasks.c
	e-cal-backend-gtasks.h
)

add_library(ecalbackendgtasks MODULE
	${SOURCES}
)

add_dependencies(ecalbackendgtasks
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendgtasks PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-gtasks\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendgtasks PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendgtasks PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendgtasks
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendgtasks
	DESTINATION ${ecal_backenddir}
)

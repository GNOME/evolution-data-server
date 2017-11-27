set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-weather-factory.c
	e-cal-backend-weather.c
	e-cal-backend-weather.h
	e-weather-source.c
	e-weather-source.h
)

add_library(ecalbackendweather MODULE
	${SOURCES}
)

add_dependencies(ecalbackendweather
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendweather PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-weather\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendweather PUBLIC
	${CALENDAR_CFLAGS}
	${LIBGWEATHER_CFLAGS}
)

target_include_directories(ecalbackendweather PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
	${LIBGWEATHER_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendweather
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
	${LIBGWEATHER_LDFLAGS}
)

install(TARGETS ecalbackendweather
	DESTINATION ${ecal_backenddir}
)

glib_mkenums(e-cal-enumtypes e-cal-types.h E_CAL_ENUMTYPES_H)

add_pkgconfig_file(libecal.pc.in libecal-${API_VERSION}.pc)

set(DEPENDENCIES
	edbus-private
	edataserver
)

set(SOURCES
	e-cal.c
	e-cal-client.c
	e-cal-client-view.c
	e-cal-component.c
	e-cal-recur.c
	e-cal-time-util.c
	e-cal-check-timezones.c
	e-cal-system-timezone.c
	e-cal-util.c
	e-cal-view.c
	e-cal-view-private.h
	e-reminder-watcher.c
	e-timezone-cache.c
	${CMAKE_CURRENT_BINARY_DIR}/e-cal-enumtypes.c
)

set(HEADERS
	libecal.h
	e-cal.h
	e-cal-client.h
	e-cal-client-view.h
	e-cal-component.h
	e-cal-recur.h
	e-cal-time-util.h
        e-cal-check-timezones.h
	e-cal-system-timezone.h
	e-cal-types.h
	e-cal-util.h
	e-cal-view.h
	e-reminder-watcher.h
	e-timezone-cache.h
	${CMAKE_CURRENT_BINARY_DIR}/e-cal-enumtypes.h
)

add_library(ecal SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(ecal
	${DEPENDENCIES}
)

set_target_properties(ecal PROPERTIES
	VERSION "${LIBECAL_CURRENT}.${LIBECAL_REVISION}.${LIBECAL_AGE}"
	SOVERSION ${LIBECAL_CURRENT}
	OUTPUT_NAME ecal-${API_VERSION}
)

target_compile_definitions(ecal PRIVATE
	-DG_LOG_DOMAIN=\"libecal\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DLIBECAL_COMPILATION
)

target_compile_options(ecal PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecal PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_CURRENT_BINARY_DIR}
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecal
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecal
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libecal
)

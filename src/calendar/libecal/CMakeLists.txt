glib_mkenums(e-cal-enumtypes e-cal-enums.h E_CAL_ENUMTYPES_H)

add_pkgconfig_file(libecal.pc.in libecal-${CAL_API_VERSION}.pc)

set(DEPENDENCIES
	edbus-private
	edataserver
)

set(SOURCES
	e-cal-client.c
	e-cal-client-view.c
	e-cal-component.c
	e-cal-component-alarm.c
	e-cal-component-alarm-instance.c
	e-cal-component-alarm-repeat.c
	e-cal-component-alarm-trigger.c
	e-cal-component-alarms.c
	e-cal-component-attendee.c
	e-cal-component-datetime.c
	e-cal-component-id.c
	e-cal-component-organizer.c
	e-cal-component-parameter-bag.c
	e-cal-component-period.c
	e-cal-component-property-bag.c
	e-cal-component-range.c
	e-cal-component-text.c
	e-cal-recur.c
	e-cal-time-util.c
	e-cal-check-timezones.c
	e-cal-system-timezone.c
	e-cal-util.c
	e-reminder-watcher.c
	e-timezone-cache.c
	${CMAKE_CURRENT_BINARY_DIR}/e-cal-enumtypes.c
)

set(HEADERS
	libecal.h
	e-cal-client.h
	e-cal-client-view.h
	e-cal-component.h
	e-cal-component-alarm.h
	e-cal-component-alarm-instance.h
	e-cal-component-alarm-repeat.h
	e-cal-component-alarm-trigger.h
	e-cal-component-alarms.h
	e-cal-component-attendee.h
	e-cal-component-datetime.h
	e-cal-component-id.h
	e-cal-component-parameter-bag.h
	e-cal-component-organizer.h
	e-cal-component-period.h
	e-cal-component-property-bag.h
	e-cal-component-range.h
	e-cal-component-text.h
	e-cal-enums.h
	e-cal-recur.h
	e-cal-time-util.h
	e-cal-check-timezones.h
	e-cal-system-timezone.h
	e-cal-util.h
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
	OUTPUT_NAME ecal-${CAL_API_VERSION}
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

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes ECal E)
set(gir_includes GObject-2.0 Gio-2.0 Soup-2.4 libxml2-2.0 ICalGLib-3.0)
set(gir_cflags
	-DLIBECAL_COMPILATION
	-DEDS_DISABLE_DEPRECATED=1
	-I${CMAKE_BINARY_DIR}/src/calendar
	-I${CMAKE_SOURCE_DIR}/src/calendar
)
set(gir_libdirs)
set(gir_libs ecal)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
)

gir_add_introspection_simple(
	ECal
	libecal
	${CAL_API_VERSION}
	"libecal/libecal.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

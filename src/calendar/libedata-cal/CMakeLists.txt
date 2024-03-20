add_pkgconfig_file(libedata-cal.pc.in libedata-cal-${CAL_API_VERSION}.pc)

set(DEPENDENCIES
	camel
	ebackend
	ecal
	edbus-private
	edataserver
)

set(SOURCES
	e-cal-backend.c
	e-cal-backend-factory.c
	e-cal-backend-intervaltree.c
	e-cal-backend-private.h
	e-cal-backend-sexp.c
	e-cal-backend-sync.c
	e-cal-backend-util.c
	e-cal-cache.c
	e-cal-meta-backend.c
	e-data-cal.c
	e-data-cal-factory.c
	e-data-cal-view.c
	e-subprocess-cal-factory.c
)

set(HEADERS
	libedata-cal.h
	e-cal-backend.h
	e-cal-backend-factory.h
	e-cal-backend-intervaltree.h
	e-cal-backend-sync.h
	e-cal-backend-util.h
	e-cal-backend-sexp.h
	e-cal-cache.h
	e-cal-meta-backend.h
	e-data-cal.h
	e-data-cal-factory.h
	e-data-cal-view.h
	e-subprocess-cal-factory.h
)

add_library(edata-cal SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(edata-cal
	${DEPENDENCIES}
)

set_target_properties(edata-cal PROPERTIES
	VERSION "${LIBEDATACAL_CURRENT}.${LIBEDATACAL_REVISION}.${LIBEDATACAL_AGE}"
	SOVERSION ${LIBEDATACAL_CURRENT}
	OUTPUT_NAME edata-cal-${CAL_API_VERSION}
)

target_compile_definitions(edata-cal PRIVATE
	-DG_LOG_DOMAIN=\"libedata-cal\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
	-DSUBPROCESS_CAL_BACKEND_PATH=\"${LIBEXEC_INSTALL_DIR}/evolution-calendar-factory-subprocess\"
	-DLIBEDATA_CAL_COMPILATION
)

target_compile_options(edata-cal PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(edata-cal PUBLIC
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

target_link_libraries(edata-cal
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS edata-cal
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedata-cal
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 Gio-2.0 ICalGLib-3.0)
set(gir_cflags
	-DLIBEDATA_CAL_COMPILATION
	-DEDS_DISABLE_DEPRECATED=1
	-I${CMAKE_BINARY_DIR}/src/calendar
	-I${CMAKE_SOURCE_DIR}/src/calendar
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libebackend
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/calendar/libecal
	${CMAKE_BINARY_DIR}/src/calendar/libedata-cal
)
set(gir_libs
	ebackend
	ecal
	edata-cal
	edataserver
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libebackend/EBackend-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/calendar/libecal/ECal-${CAL_API_VERSION}.gir
)

gir_add_introspection_simple(
	EDataCal
	libedata-cal
	${CAL_API_VERSION}
	"libedata-cal/libedata-cal.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

set(DEPENDENCIES
	ebackend
	edataserver
	edata-cal
	edbus-private
)

add_executable(evolution-calendar-factory-subprocess
	evolution-calendar-factory-subprocess.c
)

target_compile_definitions(evolution-calendar-factory-subprocess PRIVATE
	-DG_LOG_DOMAIN=\"evolution-calendar-factory-subprocess\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-calendar-factory-subprocess PUBLIC
	${CALENDAR_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-calendar-factory-subprocess PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_CURRENT_BINARY_DIR}
	${CALENDAR_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-calendar-factory-subprocess
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-calendar-factory-subprocess
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

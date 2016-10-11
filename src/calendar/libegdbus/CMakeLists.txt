set(SOURCES
	e-gdbus-cal-view.h
	e-gdbus-cal-view.c
)

set(DEPENDENCIES
	edataserver
)

add_library(egdbus-cal STATIC
	${SOURCES}
)

add_dependencies(egdbus-cal
	${DEPENDENCIES}
)

target_compile_definitions(egdbus-cal PRIVATE
	-DG_LOG_DOMAIN=\"egdbus-cal\"
)

target_compile_options(egdbus-cal PUBLIC
	${DATA_SERVER_CFLAGS}
)

target_include_directories(egdbus-cal PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src
	${DATA_SERVER_INCLUDE_DIRS}
)

target_link_libraries(egdbus-cal
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
)

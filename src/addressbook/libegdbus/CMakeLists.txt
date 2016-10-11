set(SOURCES
	e-gdbus-book-view.h
	e-gdbus-book-view.c
)

set(DEPENDENCIES
	edataserver
)

add_library(egdbus-book STATIC
	${SOURCES}
)

add_dependencies(egdbus-book
	${DEPENDENCIES}
)

target_compile_definitions(egdbus-book PRIVATE
	-DG_LOG_DOMAIN=\"egdbus-book\"
)

target_compile_options(egdbus-book PUBLIC
	${DATA_SERVER_CFLAGS}
)

target_include_directories(egdbus-book PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src
	${DATA_SERVER_INCLUDE_DIRS}
)

target_link_libraries(egdbus-book
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
)

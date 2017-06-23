set(DEPENDENCIES
	edataserver
)

set(SOURCES
	list-sources.c
)

add_executable(list-sources
	${SOURCES}
)

add_dependencies(list-sources
	${DEPENDENCIES}
)

target_compile_definitions(list-sources PRIVATE
	-DG_LOG_DOMAIN=\"list-sources\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_include_directories(list-sources PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(list-sources
	${DEPENDENCIES}
)

install(TARGETS list-sources
	DESTINATION ${privlibexecdir}
)

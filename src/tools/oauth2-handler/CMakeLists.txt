set(DEPENDENCIES
	edbus-private
)

add_executable(evolution-oauth2-handler
	oauth2-handler.c
)

add_dependencies(evolution-oauth2-handler
	${DEPENDENCIES}
)

target_compile_definitions(evolution-oauth2-handler PRIVATE
	-DG_LOG_DOMAIN=\"evolution-oauth2-handler\"
)

target_compile_options(evolution-oauth2-handler PUBLIC
	${DATA_SERVER_CFLAGS}
)

target_include_directories(evolution-oauth2-handler PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${DATA_SERVER_INCLUDE_DIRS}
)

target_link_libraries(evolution-oauth2-handler
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
)

install(TARGETS evolution-oauth2-handler
	DESTINATION ${privlibexecdir}
)

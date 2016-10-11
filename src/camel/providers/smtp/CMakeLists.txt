set(SOURCES
	camel-smtp-provider.c
	camel-smtp-settings.c
	camel-smtp-settings.h
	camel-smtp-transport.c
	camel-smtp-transport.h
)

set(DEPENDENCIES
	camel
)

add_library(camelsmtp MODULE ${SOURCES})

add_dependencies(camelsmtp
	${DEPENDENCIES}
)

target_compile_definitions(camelsmtp PRIVATE
	-DG_LOG_DOMAIN=\"camel-smtp-provider\"
)

target_compile_options(camelsmtp PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(camelsmtp PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(camelsmtp
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
)

install(TARGETS camelsmtp
	DESTINATION ${camel_providerdir}
)

install(FILES libcamelsmtp.urls
	DESTINATION ${camel_providerdir}
)

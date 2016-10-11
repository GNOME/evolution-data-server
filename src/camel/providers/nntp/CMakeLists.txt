set(SOURCES
	camel-nntp-folder.c
	camel-nntp-folder.h
	camel-nntp-private.h
	camel-nntp-provider.c
	camel-nntp-resp-codes.h
	camel-nntp-settings.c
	camel-nntp-settings.h
	camel-nntp-store-summary.c
	camel-nntp-store-summary.h
	camel-nntp-store.c
	camel-nntp-store.h
	camel-nntp-stream.c
	camel-nntp-stream.h
	camel-nntp-summary.c
	camel-nntp-summary.h
)

set(DEPENDENCIES
	camel
)

add_library(camelnntp MODULE ${SOURCES})

add_dependencies(camelnntp
	${DEPENDENCIES}
)

target_compile_definitions(camelnntp PRIVATE
	-DG_LOG_DOMAIN=\"camel-nntp-provider\"
)

target_compile_options(camelnntp PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(camelnntp PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(camelnntp
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
)

install(TARGETS camelnntp
	DESTINATION ${camel_providerdir}
)

install(FILES libcamelnntp.urls
	DESTINATION ${camel_providerdir}
)

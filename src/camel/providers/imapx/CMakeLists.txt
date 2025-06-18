set(SOURCES
	camel-imapx-provider.c
	camel-imapx-command.c
	camel-imapx-command.h
	camel-imapx-conn-manager.c
	camel-imapx-conn-manager.h
	camel-imapx-folder.c
	camel-imapx-folder.h
	camel-imapx-input-stream.c
	camel-imapx-input-stream.h
	camel-imapx-job.c
	camel-imapx-job.h
	camel-imapx-list-response.c
	camel-imapx-list-response.h
	camel-imapx-logger.c
	camel-imapx-logger.h
	camel-imapx-mailbox.c
	camel-imapx-mailbox.h
	camel-imapx-message-info.c
	camel-imapx-message-info.h
	camel-imapx-namespace.c
	camel-imapx-namespace.h
	camel-imapx-namespace-response.c
	camel-imapx-namespace-response.h
	camel-imapx-server.c
	camel-imapx-server.h
	camel-imapx-settings.c
	camel-imapx-settings.h
	camel-imapx-status-response.c
	camel-imapx-status-response.h
	camel-imapx-store.c
	camel-imapx-store.h
	camel-imapx-store-summary.c
	camel-imapx-store-summary.h
	camel-imapx-summary.c
	camel-imapx-summary.h
	camel-imapx-tokenise.h
	camel-imapx-utils.c
	camel-imapx-utils.h
)

set(DEPENDENCIES
	camel
)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/camel-imapx-tokenise.h
	COMMAND ${GPERF} -H imapx_hash -N imapx_tokenise_struct -L ANSI-C -o -t -k1,$$ ${CMAKE_CURRENT_SOURCE_DIR}/camel-imapx-tokens.txt --output-file=${CMAKE_CURRENT_BINARY_DIR}/camel-imapx-tokenise.h
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/camel-imapx-tokens.txt
)

add_library(camelimapx MODULE ${SOURCES})

add_dependencies(camelimapx
	${DEPENDENCIES}
)

target_compile_definitions(camelimapx PRIVATE
	-DG_LOG_DOMAIN=\"camel-imapx-provider\"
)

target_compile_options(camelimapx PUBLIC
	${CAMEL_CFLAGS}
	${GIO_UNIX_CFLAGS}
	${CALENDAR_CFLAGS}
)

target_include_directories(camelimapx PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CMAKE_CURRENT_BINARY_DIR}
	${CAMEL_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(camelimapx
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
	${CALENDAR_LDFLAGS}
)

install(TARGETS camelimapx
	DESTINATION ${camel_providerdir}
)

install(FILES libcamelimapx.urls
	DESTINATION ${camel_providerdir}
)

set(SOURCES
	camel-local-folder.c
	camel-local-folder.h
	camel-local-store.c
	camel-local-store.h
	camel-local-summary.c
	camel-local-summary.h
	camel-local-private.c
	camel-local-private.h
	camel-local-provider.c
	camel-maildir-folder.c
	camel-maildir-folder.h
	camel-maildir-message-info.c
	camel-maildir-message-info.h
	camel-maildir-store.c
	camel-maildir-store.h
	camel-maildir-summary.c
	camel-maildir-summary.h
	camel-mbox-folder.c
	camel-mbox-folder.h
	camel-mbox-message-info.c
	camel-mbox-message-info.h
	camel-mbox-store.c
	camel-mbox-store.h
	camel-mbox-summary.c
	camel-mbox-summary.h
	camel-mh-folder.c
	camel-mh-folder.h
	camel-mh-settings.c
	camel-mh-settings.h
	camel-mh-store.c
	camel-mh-store.h
	camel-mh-summary.c
	camel-mh-summary.h
)

set(DEPENDENCIES
	camel
)

if(NOT WIN32)
	list(APPEND SOURCES
		camel-spool-folder.c
		camel-spool-folder.h
		camel-spool-settings.c
		camel-spool-settings.h
		camel-spool-store.c
		camel-spool-store.h
		camel-spool-summary.c
		camel-spool-summary.h
	)
endif(NOT WIN32)

add_library(camellocal MODULE ${SOURCES})

add_dependencies(camellocal
	${DEPENDENCIES}
)

target_compile_definitions(camellocal PRIVATE
	-DG_LOG_DOMAIN=\"camel-local-provider\"
)

target_compile_options(camellocal PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(camellocal PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(camellocal
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
)

install(TARGETS camellocal
	DESTINATION ${camel_providerdir}
)

install(FILES libcamellocal.urls
	DESTINATION ${camel_providerdir}
)

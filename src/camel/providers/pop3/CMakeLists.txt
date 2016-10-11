set(SOURCES
	camel-pop3-engine.c
	camel-pop3-engine.h
	camel-pop3-folder.c
	camel-pop3-folder.h
	camel-pop3-provider.c
	camel-pop3-settings.c
	camel-pop3-settings.h
	camel-pop3-stream.c
	camel-pop3-stream.h
	camel-pop3-store.c
	camel-pop3-store.h
)

set(DEPENDENCIES
	camel
)

add_library(camelpop3 MODULE ${SOURCES})

add_dependencies(camelpop3
	${DEPENDENCIES}
)

target_compile_definitions(camelpop3 PRIVATE
	-DG_LOG_DOMAIN=\"camel-pop3-provider\"
)

target_compile_options(camelpop3 PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(camelpop3 PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(camelpop3
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
)

install(TARGETS camelpop3
	DESTINATION ${camel_providerdir}
)

install(FILES libcamelpop3.urls
	DESTINATION ${camel_providerdir}
)

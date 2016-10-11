set(SOURCES
	camel-sendmail-settings.c
	camel-sendmail-settings.h
	camel-sendmail-provider.c
	camel-sendmail-transport.c
	camel-sendmail-transport.h
)

set(DEPENDENCIES
	camel
)

add_library(camelsendmail MODULE ${SOURCES})

add_dependencies(camelsendmail
	${DEPENDENCIES}
)

target_compile_definitions(camelsendmail PRIVATE
	-DG_LOG_DOMAIN=\"camel-sendmail-provider\"
)

target_compile_options(camelsendmail PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(camelsendmail PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(camelsendmail
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
)

install(TARGETS camelsendmail
	DESTINATION ${camel_providerdir}
)

install(FILES libcamelsendmail.urls
	DESTINATION ${camel_providerdir}
)

unset(SOURCES)

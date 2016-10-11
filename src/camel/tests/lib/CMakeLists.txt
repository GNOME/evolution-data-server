set(SOURCES
	camel-test.c
	camel-test.h
	messages.c
	messages.h
	addresses.c
	addresses.h
	folders.c
	folders.h
	session.c
	session.h
	address-data.h
)

add_library(cameltest STATIC EXCLUDE_FROM_ALL
	${SOURCES}
)

add_dependencies(cameltest camel)

target_compile_definitions(cameltest PRIVATE
	-DG_LOG_DOMAIN=\"camel-test\"
	-DCAMEL_BUILD_DIR=\"${CMAKE_BINARY_DIR}/src/camel\"
)

target_compile_options(cameltest PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(cameltest PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(cameltest
	camel
	${CAMEL_LDFLAGS}
)

add_library(cameltest-provider STATIC EXCLUDE_FROM_ALL
	camel-test-provider.c
	camel-test-provider.h
)

add_dependencies(cameltest-provider camel)

target_compile_definitions(cameltest-provider PRIVATE
	-DG_LOG_DOMAIN=\"camel-test-provider\"
	-DCAMEL_BUILD_DIR=\"${CMAKE_BINARY_DIR}/src/camel\"
)

target_compile_options(cameltest-provider PUBLIC
	${CAMEL_CFLAGS}
)

target_include_directories(cameltest-provider PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
)

target_link_libraries(cameltest-provider
	camel
	${CAMEL_LDFLAGS}
)

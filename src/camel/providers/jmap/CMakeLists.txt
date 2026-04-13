set(SOURCES
	camel-jmap-folder.c
	camel-jmap-folder.h
	camel-jmap-provider.c
	camel-jmap-settings.c
	camel-jmap-settings.h
	camel-jmap-store.c
	camel-jmap-store.h
	camel-jmap-transport.c
	camel-jmap-transport.h
)

set(DEPENDENCIES
	camel
)

add_library(cameljmap MODULE ${SOURCES})

add_dependencies(cameljmap
	${DEPENDENCIES}
)

target_compile_definitions(cameljmap PRIVATE
	-DG_LOG_DOMAIN=\"camel-jmap-provider\"
)

target_compile_options(cameljmap PUBLIC
	${CAMEL_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${JSON_GLIB_CFLAGS}
)

target_include_directories(cameljmap PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/camel
	${CAMEL_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${JSON_GLIB_INCLUDE_DIRS}
)

target_link_libraries(cameljmap
	${DEPENDENCIES}
	${CAMEL_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${JSON_GLIB_LDFLAGS}
)

install(TARGETS cameljmap
	DESTINATION ${camel_providerdir}
)

install(FILES libcameljmap.urls
	DESTINATION ${camel_providerdir}
)

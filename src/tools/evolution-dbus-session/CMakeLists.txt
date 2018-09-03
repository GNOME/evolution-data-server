set(SOURCES
	evolution-dbus-session.c
)

add_executable(evolution-dbus-session
	${SOURCES}
)

target_compile_definitions(evolution-dbus-session PRIVATE
	-DG_LOG_DOMAIN=\"evolution-dbus-session\"
)

target_compile_options(edataserver PUBLIC
	${GNOME_PLATFORM_CFLAGS}
)

target_include_directories(evolution-dbus-session PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${GNOME_PLATFORM_INCLUDE_DIRS}
)

target_link_libraries(evolution-dbus-session
	${GNOME_PLATFORM_LDFLAGS}
)

install(TARGETS evolution-dbus-session
	DESTINATION ${privlibexecdir}
)

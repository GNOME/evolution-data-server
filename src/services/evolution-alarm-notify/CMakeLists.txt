if(WITH_SYSTEMD_USER_UNITS)
	configure_file(evolution-alarm-notify.service.in
		evolution-alarm-notify.service
		@ONLY
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-alarm-notify.service
		DESTINATION ${WITH_SYSTEMDUSERUNITDIR}
	)
endif(WITH_SYSTEMD_USER_UNITS)

set(DEPENDENCIES
	ecal
	edataserverui
)

set(SOURCES
	evolution-alarm-notify.c
	e-alarm-notify.h
	e-alarm-notify.c
)

add_executable(evolution-alarm-notify
	${SOURCES}
)

add_dependencies(evolution-alarm-notify
	${DEPENDENCIES}
)

target_compile_definitions(evolution-alarm-notify PRIVATE
	-DG_LOG_DOMAIN=\"evolution-alarm-notify\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-alarm-notify PUBLIC
	${CANBERRA_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-alarm-notify PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CANBERRA_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-alarm-notify
	${DEPENDENCIES}
	${CANBERRA_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${GTK_LDFLAGS}
)

if(WIN32)
	find_program(WINDRES windres)
	if(WINDRES)
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/evolution-alarm-notify-icon.o
			COMMAND ${WINDRES} ${CMAKE_CURRENT_SOURCE_DIR}/evolution-alarm-notify-icon.rc ${CMAKE_CURRENT_BINARY_DIR}/evolution-alarm-notify-icon.o
			DEPENDS evolution-alarm-notify-icon.rc
				evolution-alarm-notify.ico
		)

		target_link_libraries(evolution-alarm-notify
			${CMAKE_CURRENT_BINARY_DIR}/evolution-alarm-notify-icon.o
		)
	endif(WINDRES)
endif(WIN32)

install(TARGETS evolution-alarm-notify
	DESTINATION ${privlibexecdir}
)

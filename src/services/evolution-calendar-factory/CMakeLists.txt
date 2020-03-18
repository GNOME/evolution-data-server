set(SYSTEMD_SERVICE)
if(WITH_SYSTEMD_USER_UNITS)
	set(SYSTEMD_SERVICE "SystemdService=evolution-calendar-factory.service")

	configure_file(evolution-calendar-factory.service.in
		evolution-calendar-factory.service
		@ONLY
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-calendar-factory.service
		DESTINATION ${WITH_SYSTEMDUSERUNITDIR}
	)
endif(WITH_SYSTEMD_USER_UNITS)

configure_file(org.gnome.evolution.dataserver.Calendar.service.in
	${CALENDAR_DBUS_SERVICE_NAME}.service
	@ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CALENDAR_DBUS_SERVICE_NAME}.service
	DESTINATION ${WITH_DBUS_SERVICE_DIR}
)

set(DEPENDENCIES
	ebackend
	ecal
	edata-cal
	edataserver
)

set(SOURCES
	evolution-calendar-factory.c
)

add_executable(evolution-calendar-factory
	${SOURCES}
)

add_dependencies(evolution-calendar-factory
	${DEPENDENCIES}
)

target_compile_definitions(evolution-calendar-factory PRIVATE
	-DG_LOG_DOMAIN=\"evolution-calendar-factory\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-calendar-factory PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${CALENDAR_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-calendar-factory PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${CALENDAR_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-calendar-factory
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${CALENDAR_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-calendar-factory
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

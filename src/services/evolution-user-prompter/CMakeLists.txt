set(SYSTEMD_SERVICE)
if(WITH_SYSTEMD_USER_UNITS)
	set(SYSTEMD_SERVICE "SystemdService=evolution-user-prompter.service")

	configure_file(evolution-user-prompter.service.in
		evolution-user-prompter.service
		@ONLY
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-user-prompter.service
		DESTINATION ${WITH_SYSTEMDUSERUNITDIR}
	)
endif(WITH_SYSTEMD_USER_UNITS)

configure_file(org.gnome.evolution.dataserver.UserPrompter.service.in
	${USER_PROMPTER_DBUS_SERVICE_NAME}.service
	@ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${USER_PROMPTER_DBUS_SERVICE_NAME}.service
	DESTINATION ${WITH_DBUS_SERVICE_DIR}
)

set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edata-book
	edataserver
)

set(SOURCES
	prompt-user.h
	prompt-user-gtk.c
	evolution-user-prompter.c
)

add_executable(evolution-user-prompter
	${SOURCES}
)

add_dependencies(evolution-user-prompter
	${DEPENDENCIES}
)

target_compile_definitions(evolution-user-prompter PRIVATE
	-DG_LOG_DOMAIN=\"evolution-user-prompter\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-user-prompter PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-user-prompter PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-user-prompter
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-user-prompter
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

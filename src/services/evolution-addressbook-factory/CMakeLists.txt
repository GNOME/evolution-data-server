set(SYSTEMD_SERVICE)
if(WITH_SYSTEMD_USER_UNITS)
	set(SYSTEMD_SERVICE "SystemdService=evolution-addressbook-factory.service")

	configure_file(evolution-addressbook-factory.service.in
		evolution-addressbook-factory.service
		@ONLY
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-addressbook-factory.service
		DESTINATION ${WITH_SYSTEMDUSERUNITDIR}
	)
endif(WITH_SYSTEMD_USER_UNITS)

configure_file(org.gnome.evolution.dataserver.AddressBook.service.in
	org.gnome.evolution.dataserver.AddressBook.service
	@ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.evolution.dataserver.AddressBook.service
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
	evolution-addressbook-factory.c
)

add_executable(evolution-addressbook-factory
	${SOURCES}
)

add_dependencies(evolution-addressbook-factory
	${DEPENDENCIES}
)

target_compile_definitions(evolution-addressbook-factory PRIVATE
	-DG_LOG_DOMAIN=\"evolution-addressbook-factory\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-addressbook-factory PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${ADDRESSBOOK_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-addressbook-factory PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${ADDRESSBOOK_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-addressbook-factory
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${ADDRESSBOOK_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-addressbook-factory
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

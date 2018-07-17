set(SYSTEMD_SERVICE)
if(WITH_SYSTEMD_USER_UNITS)
	set(SYSTEMD_SERVICE "SystemdService=evolution-source-registry.service")

	configure_file(evolution-source-registry.service.in
		evolution-source-registry.service
		@ONLY
	)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-source-registry.service
		DESTINATION ${WITH_SYSTEMDUSERUNITDIR}
	)
endif(WITH_SYSTEMD_USER_UNITS)

configure_file(org.gnome.evolution.dataserver.Sources.service.in
	org.gnome.evolution.dataserver.Sources.service
	@ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.evolution.dataserver.Sources.service
	DESTINATION ${WITH_DBUS_SERVICE_DIR}
)

set(builtin_sources_files
	caldav-stub.source
	carddav-stub.source
	contacts-stub.source
	google-stub.source
	ldap-stub.source
	local-stub.source
	weather-stub.source
	webcal-stub.source
	birthdays.source
	local.source
	sendmail.source
	system-address-book.source
	system-calendar.source
	system-memo-list.source
	system-proxy.source
	system-task-list.source
	vfolder.source
)

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/builtin)
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/builtin)
endif(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/builtin)

set(builtin_sources)
foreach(_file ${builtin_sources_files})
	intltool_merge(${CMAKE_CURRENT_SOURCE_DIR}/builtin/${_file}.in ${CMAKE_CURRENT_BINARY_DIR}/builtin/${_file} -d -u)
	list(APPEND builtin_sources ${CMAKE_CURRENT_BINARY_DIR}/builtin/${_file})
endforeach(_file)

glib_compile_resources(${CMAKE_CURRENT_BINARY_DIR}/builtin evolution-source-registry-resource evolution_source_registry evolution-source-registry-resource.xml ${builtin_sources})

set(DEPENDENCIES
	camel
	ebackend
	edataserver
)

set(SOURCES
	evolution-source-registry.c
	evolution-source-registry-autoconfig.c
	evolution-source-registry-methods.h
	evolution-source-registry-migrate-basedir.c
	evolution-source-registry-migrate-proxies.c
	evolution-source-registry-migrate-sources.c
	evolution-source-registry-migrate-tweaks.c
	${CMAKE_CURRENT_BINARY_DIR}/evolution-source-registry-resource.c
	${CMAKE_CURRENT_BINARY_DIR}/evolution-source-registry-resource.h
)

add_executable(evolution-source-registry
	${SOURCES}
)

add_dependencies(evolution-source-registry
	${DEPENDENCIES}
)

target_compile_definitions(evolution-source-registry PRIVATE
	-DG_LOG_DOMAIN=\"evolution-source-registry\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-source-registry PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-source-registry PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-source-registry
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-source-registry
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set(SOURCES
	evolution-scan-gconf-tree-xml.c
	evolution-source-registry-migrate-sources.c
)

add_executable(evolution-scan-gconf-tree-xml
	${SOURCES}
)

add_dependencies(evolution-scan-gconf-tree-xml
	${DEPENDENCIES}
)

target_compile_definitions(evolution-scan-gconf-tree-xml PRIVATE
	-DG_LOG_DOMAIN=\"evolution-scan-gconf-tree-xml\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(evolution-scan-gconf-tree-xml PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${GTK_CFLAGS}
)

target_include_directories(evolution-scan-gconf-tree-xml PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
)

target_link_libraries(evolution-scan-gconf-tree-xml
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${GTK_LDFLAGS}
)

install(TARGETS evolution-scan-gconf-tree-xml
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

set(valafiles
	${CMAKE_CURRENT_SOURCE_DIR}/libedataserver-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-contacts-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
)

add_custom_target(vala ALL)

# ***********************************
# camel
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/camel/Camel-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps Camel-${API_VERSION}.gir)

list(APPEND valafiles
	${CMAKE_CURRENT_SOURCE_DIR}/camel-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/camel
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg posix
		--library camel-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS camel-${API_VERSION}.deps
		${gir_deps}
		${gir_fullname}
)

# ***********************************
# libedataserver
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/libedataserver/EDataServer-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataServer-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/libedataserver
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libedataserver-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libedataserver-${API_VERSION}.deps
		${gir_deps}
		${gir_fullname}
)

# ***********************************
# libebook-contacts
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/addressbook/libebook-contacts/EBookContacts-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBookContacts-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/libedataserver
		--pkg libedataserver-${API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libebook-contacts-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
		${CMAKE_CURRENT_SOURCE_DIR}/libebook-contacts-${API_VERSION}-custom.vala
	DEPENDS libebook-contacts-${API_VERSION}.deps
		libebook-contacts-${API_VERSION}-custom.vala
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)

# ***********************************
# libebook
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/addressbook/libebook/EBook-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBook-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libebook-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/addressbook/libebook-contacts
		--pkg libedataserver-${API_VERSION}
		--pkg libebook-contacts-${API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libebook-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libebook-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)

# ***********************************
# Install all VAPI files
# ***********************************

add_custom_target(vala-files
	DEPENDS ${valafiles}
)

add_dependencies(vala vala-files)

install(FILES ${valafiles}
	DESTINATION ${SHARE_INSTALL_DIR}/vala/vapi
)

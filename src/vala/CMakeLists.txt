set(valafiles
	${CMAKE_CURRENT_SOURCE_DIR}/camel-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libedataserver-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-contacts-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
)

if(HAVE_GTK)
	list(APPEND valafiles
		${CMAKE_CURRENT_SOURCE_DIR}/libedataserverui-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserverui-${API_VERSION}.vapi
	)
endif(HAVE_GTK)

add_custom_target(vala ALL)

# ***********************************
# camel
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps Camel-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
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

set(gir_fullname ${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataServer-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libedataserver-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libedataserver-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
		${gir_deps}
		${gir_fullname}
)

# ***********************************
# libedataserverui
# ***********************************

if(HAVE_GTK)
	set(gir_fullname ${CMAKE_BINARY_DIR}/src/libedataserverui/EDataServerUI-${API_VERSION}.gir)
	gir_girfilename_to_target(gir_deps EDataServerUI-${API_VERSION}.gir)

	add_dependencies(vala ${gir_deps})

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libedataserverui-${API_VERSION}.vapi
		COMMAND ${VAPIGEN}
			--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
			--vapidir=${CMAKE_CURRENT_BINARY_DIR}
			--girdir=${CMAKE_BINARY_DIR}/src/camel
			--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
			--pkg libedataserver-${API_VERSION}
			--pkg gio-2.0
			--pkg gtk+-3.0
			--pkg libxml-2.0
			--pkg libsoup-2.4
			--pkg posix
			--library libedataserverui-${API_VERSION}
			--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
			${gir_fullname}
		DEPENDS libedataserverui-${API_VERSION}.deps
			${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
			${gir_fullname}
			${gir_deps}
	)
endif(HAVE_GTK)

# ***********************************
# libebook-contacts
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts/EBookContacts-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBookContacts-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
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

set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libebook/EBook-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBook-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libebook-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
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
	DESTINATION ${SHARE_INSTALL_PREFIX}/vala/vapi
)

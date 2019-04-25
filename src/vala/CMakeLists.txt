# disabled until some issues are fixed;
# when fixed, then remove the conditions below and build the vala bindings unconditionally
set(ENABLE_EBACKEND 0)
set(ENABLE_DATACAL 0)
set(ENABLE_DATABOOK 0)

set(valafiles
	${CMAKE_CURRENT_SOURCE_DIR}/camel-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/camel-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libedataserver-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-contacts-${API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
	${CMAKE_CURRENT_SOURCE_DIR}/libecal-${CAL_API_VERSION}.deps
	${CMAKE_CURRENT_BINARY_DIR}/libecal-${CAL_API_VERSION}.vapi
)

if(ENABLE_EBACKEND)
	list(APPEND valafiles
		${CMAKE_CURRENT_SOURCE_DIR}/libebackend-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libebackend-${API_VERSION}.vapi
	)
endif(ENABLE_EBACKEND)

if(ENABLE_DATACAL)
	list(APPEND valafiles
		${CMAKE_CURRENT_SOURCE_DIR}/libedata-cal-${CAL_API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedata-cal-${CAL_API_VERSION}.vapi
	)
endif(ENABLE_DATACAL)

if(ENABLE_DATABOOK)
	list(APPEND valafiles
		${CMAKE_CURRENT_SOURCE_DIR}/libedata-book-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedata-book-${API_VERSION}.vapi
	)
endif(ENABLE_DATABOOK)

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
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
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
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
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
			--girdir=${CMAKE_BINARY_DIR}/src/calendar/libecal
			--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
			--pkg libedataserver-${API_VERSION}
			--pkg libecal-${CAL_API_VERSION}
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
			${CMAKE_CURRENT_BINARY_DIR}/libecal-${CAL_API_VERSION}.vapi
			${gir_fullname}
			${gir_deps}
	)
endif(HAVE_GTK)

# ***********************************
# libebackend
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/libebackend/EBackend-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBackend-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libebackend-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/src/libebackend
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
		--pkg libedataserver-${API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libebackend-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libebackend-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libebackend-${API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)

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
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
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
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
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
# libedata-book
# ***********************************

if(ENABLE_DATABOOK)
set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libedata-book/EDataBook-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataBook-${API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libedata-book-${API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libebackend
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
		--pkg libedataserver-${API_VERSION}
		--pkg libebackend-${API_VERSION}
		--pkg libebook-contacts-${API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libedata-book-${API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libedata-book-${API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libebackend-${API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)
endif(ENABLE_DATABOOK)

# ***********************************
# libecal
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/calendar/libecal/ECal-${CAL_API_VERSION}.gir)
gir_girfilename_to_target(gir_deps ECal-${CAL_API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libecal-${CAL_API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/src/calendar/libecal
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
		--pkg libedataserver-${API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libecal-${CAL_API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libecal-${CAL_API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)

# ***********************************
# libedata-cal
# ***********************************

if(ENABLE_DATACAL)
set(gir_fullname ${CMAKE_BINARY_DIR}/src/calendar/libedata-cal/EDataCal-${CAL_API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataCal-${CAL_API_VERSION}.gir)

add_dependencies(vala ${gir_deps})

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libedata-cal-${CAL_API_VERSION}.vapi
	COMMAND ${VAPIGEN}
		--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
		--vapidir=${CMAKE_CURRENT_BINARY_DIR}
		--girdir=${CMAKE_BINARY_DIR}/src/camel
		--girdir=${CMAKE_BINARY_DIR}/src/libebackend
		--girdir=${CMAKE_BINARY_DIR}/src/libedataserver
		--girdir=${CMAKE_BINARY_DIR}/src/calendar/libecal
		--girdir=${CMAKE_BINARY_DIR}/src/calendar/libedata-cal
		--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
		--pkg libedataserver-${API_VERSION}
		--pkg libebackend-${API_VERSION}
		--pkg libecal-${CAL_API_VERSION}
		--pkg gio-2.0
		--pkg libxml-2.0
		--pkg libsoup-2.4
		--pkg posix
		--library libedata-cal-${CAL_API_VERSION}
		--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
		${gir_fullname}
	DEPENDS libedata-cal-${CAL_API_VERSION}.deps
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libebackend-${API_VERSION}.vapi
		${CMAKE_CURRENT_BINARY_DIR}/libecal-${CAL_API_VERSION}.vapi
		${gir_fullname}
		${gir_deps}
)
endif(ENABLE_DATACAL)

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

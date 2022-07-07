# Filled by the _build_vala_files macro below
set(valafiles)

add_custom_target(vala ALL)

# ***********************************
# Helper macro
# ***********************************

macro(_build_vala_files _lib_name _gir_fullname_var _gir_deps_var _gir_dirs_var _vala_deps_var _extra_vapigen_files_var)
	list(APPEND valafiles
		${CMAKE_CURRENT_BINARY_DIR}/${_lib_name}.deps
		${CMAKE_CURRENT_BINARY_DIR}/${_lib_name}.vapi
	)

	add_dependencies(vala ${${_gir_deps_var}})

	set(gir_dirs_param)
	set(vala_deps_param)
	set(vala_deps_content "")
	set(vapi_deps)

	foreach(_item IN LISTS ${_gir_dirs_var})
		list(APPEND gir_dirs_param --girdir=${_item})
	endforeach()

	foreach(_item IN LISTS ${_vala_deps_var})
		# those beginning with '*' are built here, thus
		# skip them from the vapigen arguments
		string(SUBSTRING "${_item}" 0 1 _item_prefix)
		if("${_item_prefix}" STREQUAL "*")
			string(SUBSTRING "${_item}" 1, -1, _item)
			list(APPEND vapi_deps ${CMAKE_CURRENT_BINARY_DIR}/${_item}.vapi)
		endif("${_item_prefix}" STREQUAL "*")

		list(APPEND vala_deps_param --pkg ${_item})
		string(CONCAT vala_deps_content "${vala_deps_content}" "${_item}\n")
	endforeach()

	file(GENERATE
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_lib_name}.deps
		CONTENT "${vala_deps_content}"
	)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_lib_name}.vapi
		COMMAND ${VAPIGEN}
			--vapidir=${CMAKE_CURRENT_SOURCE_DIR}
			--vapidir=${CMAKE_CURRENT_BINARY_DIR}
			${gir_dirs_param}
			--girdir=${SHARE_INSTALL_PREFIX}/gir-1.0
			${vala_deps_param}
			--library ${_lib_name}
			--metadatadir=${CMAKE_CURRENT_SOURCE_DIR}
			${${_gir_fullname_var}}
			${${_extra_vapigen_files_var}}
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_lib_name}.deps
			${vapi_deps}
			${${_extra_vapigen_files_var}}
			${${_gir_deps_var}}
			${${_gir_fullname_var}}
	)
endmacro(_build_vala_files)

# ***********************************
# camel
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps Camel-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
)

set(vala_deps
	gio-2.0
	libxml-2.0
	posix
)

set(extra_vapigen_files
	${CMAKE_CURRENT_SOURCE_DIR}/camel-${API_VERSION}-custom.vala
)

_build_vala_files(camel-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libedataserver
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataServer-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)

set(vala_deps
	*camel-${API_VERSION}
	gio-2.0
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libedataserver-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libebackend
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/libebackend/EBackend-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBackend-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/libebackend
)

set(vala_deps
	*libedataserver-${API_VERSION}
	gio-2.0
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libebackend-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libebook-contacts
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts/EBookContacts-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBookContacts-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)

set(vala_deps
	*camel-${API_VERSION}
	*libedataserver-${API_VERSION}
	gio-2.0
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files
	${CMAKE_CURRENT_SOURCE_DIR}/libebook-contacts-${API_VERSION}-custom.vala
)

_build_vala_files(libebook-contacts-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libebook
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libebook/EBook-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EBook-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
)

set(vala_deps
	*camel-${API_VERSION}
	*libedataserver-${API_VERSION}
	*libebook-contacts-${API_VERSION}
	gio-2.0
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libebook-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libedata-book
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/addressbook/libedata-book/EDataBook-${API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataBook-${API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libebackend
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts
)

set(vala_deps
	*camel-${API_VERSION}
	*libedataserver-${API_VERSION}
	*libebackend-${API_VERSION}
	*libebook-contacts-${API_VERSION}
	gio-2.0
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libedata-book-${API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libecal
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/calendar/libecal/ECal-${CAL_API_VERSION}.gir)
gir_girfilename_to_target(gir_deps ECal-${CAL_API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/calendar/libecal
)

set(vala_deps
	*camel-${API_VERSION}
	*libedataserver-${API_VERSION}
	gio-2.0
	libical-glib
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libecal-${CAL_API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libedata-cal
# ***********************************

set(gir_fullname ${CMAKE_BINARY_DIR}/src/calendar/libedata-cal/EDataCal-${CAL_API_VERSION}.gir)
gir_girfilename_to_target(gir_deps EDataCal-${CAL_API_VERSION}.gir)

set(gir_dirs
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libebackend
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/calendar/libecal
	${CMAKE_BINARY_DIR}/src/calendar/libedata-cal
)

set(vala_deps
	*camel-${API_VERSION}
	*libedataserver-${API_VERSION}
	*libebackend-${API_VERSION}
	*libecal-${CAL_API_VERSION}
	gio-2.0
	libical-glib
	libsoup-3.0
	libxml-2.0
	posix
)

set(extra_vapigen_files)

_build_vala_files(libedata-cal-${CAL_API_VERSION}
	gir_fullname
	gir_deps
	gir_dirs
	vala_deps
	extra_vapigen_files
)

# ***********************************
# libedataserverui
# ***********************************

if(ENABLE_GTK)
	set(gir_fullname ${CMAKE_BINARY_DIR}/src/libedataserverui/EDataServerUI-${API_VERSION}.gir)
	gir_girfilename_to_target(gir_deps EDataServerUI-${API_VERSION}.gir)

	set(gir_dirs
		${CMAKE_BINARY_DIR}/src/camel
		${CMAKE_BINARY_DIR}/src/libedataserver
		${CMAKE_BINARY_DIR}/src/calendar/libecal
	)

	set(vala_deps
		*libedataserver-${API_VERSION}
		*libecal-${CAL_API_VERSION}
		gio-2.0
		gtk+-3.0
		libsoup-3.0
		libxml-2.0
		posix
	)

	set(extra_vapigen_files)

	_build_vala_files(libedataserverui-${API_VERSION}
		gir_fullname
		gir_deps
		gir_dirs
		vala_deps
		extra_vapigen_files
	)
endif(ENABLE_GTK)

if(ENABLE_GTK4)
	set(gir_fullname ${CMAKE_BINARY_DIR}/src/libedataserverui/EDataServerUI4-${LIBEDATASERVERUI4_API_VERSION}.gir)
	gir_girfilename_to_target(gir_deps EDataServerUI4-${LIBEDATASERVERUI4_API_VERSION}.gir)

	set(gir_dirs
		${CMAKE_BINARY_DIR}/src/camel
		${CMAKE_BINARY_DIR}/src/libedataserver
		${CMAKE_BINARY_DIR}/src/calendar/libecal
	)

	set(vala_deps
		*libedataserver-${API_VERSION}
		*libecal-${CAL_API_VERSION}
		gio-2.0
		gtk4
		libsoup-3.0
		libxml-2.0
		posix
	)

	set(extra_vapigen_files)

	_build_vala_files(libedataserverui4-${LIBEDATASERVERUI4_API_VERSION}
		gir_fullname
		gir_deps
		gir_dirs
		vala_deps
		extra_vapigen_files
	)
endif(ENABLE_GTK4)

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

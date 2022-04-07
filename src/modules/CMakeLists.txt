macro(add_simple_module _name _sourcesvar _depsvar _defsvar _cflagsvar _incdirsvar _ldflagsvar _destination)
	set(DEPENDENCIES
		ebackend
		edataserver
	)

	set(SOURCES
		${${_sourcesvar}}
	)

	add_library(${_name} MODULE
		${SOURCES}
	)

	set_target_properties(${_name} PROPERTIES
		PREFIX ""
	)

	add_dependencies(${_name}
		${DEPENDENCIES}
		${${_depsvar}}
	)

	target_compile_definitions(${_name} PRIVATE
		-DG_LOG_DOMAIN=\"${_name}\"
		${${_defsvar}}
	)

	target_compile_options(${_name} PUBLIC
		${BACKEND_CFLAGS}
		${DATA_SERVER_CFLAGS}
		${${_cflagsvar}}
	)

	target_include_directories(${_name} PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_SOURCE_DIR}/src
		${BACKEND_INCLUDE_DIRS}
		${DATA_SERVER_INCLUDE_DIRS}
		${${_incdirsvar}}
	)

	target_link_libraries(${_name}
		${DEPENDENCIES}
		${${_depsvar}}
		${BACKEND_LDFLAGS}
		${DATA_SERVER_LDFLAGS}
		${${_ldflagsvar}}
	)

	install(TARGETS ${_name}
		DESTINATION ${_destination}
	)
endmacro(add_simple_module)

macro(add_source_registry_module _name _sourcesvar _depsvar _defsvar _cflagsvar _incdirsvar _ldflagsvar)
	add_simple_module(${_name} ${_sourcesvar} ${_depsvar} ${_defsvar} ${_cflagsvar} ${_incdirsvar} ${_ldflagsvar} ${moduledir})
endmacro(add_source_registry_module)

macro(add_credentials_module _name _sourcesvar _depsvar _defsvar _cflagsvar _incdirsvar _ldflagsvar)
	add_simple_module(${_name} ${_sourcesvar} ${_depsvar} ${_defsvar} ${_cflagsvar} ${_incdirsvar} ${_ldflagsvar} ${credentialmoduledir})
endmacro(add_credentials_module)

add_subdirectory(cache-reaper)
add_subdirectory(google-backend)
add_subdirectory(outlook-backend)
add_subdirectory(webdav-backend)
add_subdirectory(yahoo-backend)
add_subdirectory(oauth2-services)

if(HAVE_GTK)
	add_subdirectory(trust-prompt)
endif(HAVE_GTK)

if(HAVE_GOA)
	add_subdirectory(gnome-online-accounts)
endif(HAVE_GOA)

if(NOT WIN32)
	add_subdirectory(secret-monitor)
endif(NOT WIN32)

add_subdirectory(reference)

if (ENABLE_GI_DOCGEN)
	set(CAMEL_GIR_PATH "${CMAKE_BINARY_DIR}/src/camel")
	configure_file(camel.toml.in camel.toml @ONLY)
	generate_gi_documentation(
		camel
		${CMAKE_CURRENT_BINARY_DIR}/camel.toml
		"${CAMEL_GIR_PATH}/Camel-${API_VERSION}.gir"
	)
	set(EDATASERVER_GIR_PATH "${CMAKE_BINARY_DIR}/src/libedataserver")
	configure_file(libedataserver.toml.in libedataserver.toml @ONLY)
	generate_gi_documentation(
		libedataserver
		${CMAKE_CURRENT_BINARY_DIR}/libedataserver.toml
		"${EDATASERVER_GIR_PATH}/EDataServer-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
	)
	set(EBACKEND_GIR_PATH "${CMAKE_BINARY_DIR}/src/libebackend")
	configure_file(libebackend.toml.in libebackend.toml @ONLY)
	generate_gi_documentation(
		libebackend
		${CMAKE_CURRENT_BINARY_DIR}/libebackend.toml
		"${EBACKEND_GIR_PATH}/EBackend-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
	)
	set(EBOOK_CONTACTS_GIR_PATH "${CMAKE_BINARY_DIR}/src/addressbook/libebook-contacts")
	configure_file(libebook-contacts.toml.in libebook-contacts.toml @ONLY)
	generate_gi_documentation(
		libebook-contacts
		${CMAKE_CURRENT_BINARY_DIR}/libebook-contacts.toml
		"${EBOOK_CONTACTS_GIR_PATH}/EBookContacts-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
	)
	set(EDATA_BOOK_GIR_PATH "${CMAKE_BINARY_DIR}/src/addressbook/libedata-book")
	configure_file(libedata-book.toml.in libedata-book.toml @ONLY)
	generate_gi_documentation(
		libedata-book
		${CMAKE_CURRENT_BINARY_DIR}/libedata-book.toml
		"${EDATA_BOOK_GIR_PATH}/EDataBook-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
			${EBOOK_CONTACTS_GIR_PATH}
			${EBACKEND_GIR_PATH}
	)
	set(EBOOK_GIR_PATH "${CMAKE_BINARY_DIR}/src/addressbook/libebook")
	configure_file(libebook.toml.in libebook.toml @ONLY)
	generate_gi_documentation(
		libebook
		${CMAKE_CURRENT_BINARY_DIR}/libebook.toml
		"${EBOOK_GIR_PATH}/EBook-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
			${EBOOK_CONTACTS_GIR_PATH}
			${EDATA_BOOK_GIR_PATH}
	)
	set(ECAL_GIR_PATH "${CMAKE_BINARY_DIR}/src/calendar/libecal")
	configure_file(libecal.toml.in libecal.toml @ONLY)
	generate_gi_documentation(
		libecal
		${CMAKE_CURRENT_BINARY_DIR}/libecal.toml
		"${ECAL_GIR_PATH}/ECal-${CAL_API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
	)
	set(EDATA_CAL_GIR_PATH "${CMAKE_BINARY_DIR}/src/calendar/libedata-cal")
	configure_file(libedata-cal.toml.in libedata-cal.toml @ONLY)
	generate_gi_documentation(
		libedata-cal
		${CMAKE_CURRENT_BINARY_DIR}/libedata-cal.toml
		"${EDATA_CAL_GIR_PATH}/EDataCal-${CAL_API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
			${ECAL_GIR_PATH}
			${EBACKEND_GIR_PATH}
	)
	set(EDATASERVERUI_GIR_PATH "${CMAKE_BINARY_DIR}/src/libedataserverui")
	if(ENABLE_GTK)
	configure_file(libedataserverui.toml.in libedataserverui.toml @ONLY)
	generate_gi_documentation(
		libedataserverui
		${CMAKE_CURRENT_BINARY_DIR}/libedataserverui.toml
		"${EDATASERVERUI_GIR_PATH}/EDataServerUI-${API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
			${ECAL_GIR_PATH}
	)
	endif(ENABLE_GTK)
	if(ENABLE_GTK4)
	configure_file(libedataserverui4.toml.in libedataserverui4.toml @ONLY)
	generate_gi_documentation(
		libedataserverui4
		${CMAKE_CURRENT_BINARY_DIR}/libedataserverui4.toml
		"${EDATASERVERUI_GIR_PATH}/EDataServerUI4-${LIBEDATASERVERUI4_API_VERSION}.gir"
		INCLUDE_PATHS
			${CAMEL_GIR_PATH}
			${EDATASERVER_GIR_PATH}
			${ECAL_GIR_PATH}
	)
	endif(ENABLE_GTK4)
endif (ENABLE_GI_DOCGEN)

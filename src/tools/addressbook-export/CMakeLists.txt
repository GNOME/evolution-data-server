set(DEPENDENCIES
	ebook
	ebook-contacts
	edataserver
)

set(SOURCES
	addressbook-export.c
)

add_executable(addressbook-export
	${SOURCES}
)

add_dependencies(addressbook-export
	${DEPENDENCIES}
)

target_compile_definitions(addressbook-export PRIVATE
	-DG_LOG_DOMAIN=\"addressbook-export\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
)

target_compile_options(addressbook-export PUBLIC
	${ADDRESSBOOK_CFLAGS}
)

target_include_directories(addressbook-export PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_CURRENT_BINARY_DIR}
	${ADDRESSBOOK_INCLUDE_DIRS}
)

target_link_libraries(addressbook-export
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
)

install(TARGETS addressbook-export
	DESTINATION ${privlibexecdir}
)

find_program(PERL perl)
if(PERL)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/csv2vcard.in
		${CMAKE_CURRENT_BINARY_DIR}/csv2vcard
		@ONLY
	)

	install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/csv2vcard
		DESTINATION ${privlibexecdir}
	)
else(PERL)
	message(WARNING "The 'perl' not found, not installing csv2vcard")
endif(PERL)

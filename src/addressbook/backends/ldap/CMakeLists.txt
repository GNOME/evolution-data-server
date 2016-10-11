set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

add_library(ebookbackendldap MODULE
	e-book-backend-ldap.c
	e-book-backend-ldap.h
	e-book-backend-ldap-factory.c
)

add_dependencies(ebookbackendldap
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendldap PRIVATE
	-DG_LOG_DOMAIN=\"e-book-backend-ldap\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
)

target_compile_options(ebookbackendldap PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LDAP_CFLAGS}
)

target_include_directories(ebookbackendldap PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LDAP_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendldap
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LDAP_LIBS}
)

install(TARGETS ebookbackendldap
	DESTINATION ${ebook_backenddir}
)

install(FILES evolutionperson.schema
	DESTINATION ${privdatadir}
)

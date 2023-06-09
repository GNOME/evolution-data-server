set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	edataserver
	edata-book
)

set(SOURCES
	e-book-backend-file.c
	e-book-backend-file.h
	e-book-backend-file-factory.c
	e-book-sqlite-keys.c
	e-book-sqlite-keys.h
)

if(ENABLE_MAINTAINER_MODE)
	list(APPEND SOURCES
		e-book-backend-dummy.c
		e-book-backend-dummy.h
		e-book-backend-dummy-meta.c
		e-book-backend-dummy-meta.h
	)
endif(ENABLE_MAINTAINER_MODE)

if(HAVE_LIBDB)
	list(APPEND SOURCES
		e-book-backend-file-migrate-bdb.c
		e-book-backend-file-migrate-bdb.h
	)
endif(HAVE_LIBDB)

add_library(ebookbackendfile MODULE
	${SOURCES}
)

add_dependencies(ebookbackendfile
	${DEPENDENCIES}
)

target_compile_definitions(ebookbackendfile PRIVATE
	-DG_LOG_DOMAIN=\"e-book-backend-file\"
	-DBACKENDDIR=\"${ebook_backenddir}\"
)

target_compile_options(ebookbackendfile PUBLIC
	${ADDRESSBOOK_CFLAGS}
	${LIBDB_CFLAGS}
)

target_include_directories(ebookbackendfile PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${ADDRESSBOOK_INCLUDE_DIRS}
	${LIBDB_INCLUDE_DIRS}
)

target_link_libraries(ebookbackendfile
	${DEPENDENCIES}
	${ADDRESSBOOK_LDFLAGS}
	${LIBDB_LIBS}
)

install(TARGETS ebookbackendfile
	DESTINATION ${ebook_backenddir}
)

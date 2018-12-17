glib_mkenums(e-backend-enumtypes e-backend-enums.h E_BACKEND_ENUMTYPES_H)

add_pkgconfig_file(libebackend.pc.in libebackend-${API_VERSION}.pc)

set(DEPENDENCIES
	edataserver
	edbus-private
)

set(SOURCES
	e-backend.c
	e-backend-factory.c
	e-cache.c
	e-cache-reaper.c
	e-cache-reaper-utils.c
	e-cache-reaper-utils.h
	e-collection-backend.c
	e-collection-backend-factory.c
	e-data-factory.c
	e-dbus-server.c
	e-file-cache.c
	e-oauth2-support.c
	e-offline-listener.c
	e-server-side-source.c
	e-server-side-source-credentials-provider.c
	e-source-registry-server.c
	e-sqlite3-vfs.c
	e-subprocess-factory.c
	e-user-prompter.c
	e-user-prompter-server.c
	e-user-prompter-server-extension.c
	e-webdav-collection-backend.c
	${CMAKE_CURRENT_BINARY_DIR}/e-backend-enumtypes.c
)

set(HEADERS
	libebackend.h
	e-backend.h
	e-backend-autocleanups.h
	e-backend-enums.h
	e-backend-factory.h
	e-cache.h
	e-cache-reaper.h
	e-collection-backend.h
	e-collection-backend-factory.h
	e-data-factory.h
	e-dbus-server.h
	e-file-cache.h
	e-oauth2-support.h
	e-offline-listener.h
	e-server-side-source.h
	e-server-side-source-credentials-provider.h
	e-source-registry-server.h
	e-sqlite3-vfs.h
	e-subprocess-factory.h
	e-user-prompter.h
	e-user-prompter-server.h
	e-user-prompter-server-extension.h
	e-webdav-collection-backend.h
	${CMAKE_CURRENT_BINARY_DIR}/e-backend-enumtypes.h
)

if(HAVE_LIBDB)
	list(APPEND SOURCES
		e-dbhash.c
		e-db3-utils.c
	)
	list(APPEND HEADERS
		e-dbhash.h
		e-db3-utils.h
	)
endif(HAVE_LIBDB)

add_library(ebackend SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(ebackend
	${DEPENDENCIES}
)

set_target_properties(ebackend PROPERTIES
	VERSION "${LIBEBACKEND_CURRENT}.${LIBEBACKEND_REVISION}.${LIBEBACKEND_AGE}"
	SOVERSION ${LIBEBACKEND_CURRENT}
	OUTPUT_NAME ebackend-${API_VERSION}
)

target_compile_definitions(ebackend PRIVATE
	-DG_LOG_DOMAIN=\"e-backend\"
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DMODULE_DIRECTORY=\"${moduledir}\"
	-DE_DATA_SERVER_PRIVDATADIR=\"${privdatadir}\"
	-DSYSTEM_WIDE_RO_SOURCES_DIRECTORY=\"${ro_sourcesdir}\"
	-DSYSTEM_WIDE_RW_SOURCES_DIRECTORY=\"${rw_sourcesdir}\"
	-DLIBEBACKEND_COMPILATION
)

target_compile_options(ebackend PUBLIC
	${BACKEND_CFLAGS}
	${LIBDB_CFLAGS}
	${CAMEL_CFLAGS}
	${SQLITE3_CFLAGS}
	${GCR_BASE_CFLAGS}
	${GIO_UNIX_CFLAGS}
)

target_include_directories(ebackend PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_CURRENT_BINARY_DIR}
	${BACKEND_INCLUDE_DIRS}
	${LIBDB_INCLUDE_DIRS}
	${CAMEL_INCLUDE_DIRS}
	${SQLITE3_INCLUDE_DIRS}
	${GCR_BASE_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
)

target_link_libraries(ebackend
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${LIBDB_LDFLAGS}
	${LIBDB_LIBS}
	${CAMEL_LDFLAGS}
	${SQLITE3_LDFLAGS}
	${GCR_BASE_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
)

install(TARGETS ebackend
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libebackend
)

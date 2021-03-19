glib_mkenums(e-source-enumtypes e-source-enums.h E_SOURCE_ENUMTYPES_H)

add_pkgconfig_file(libedataserver.pc.in libedataserver-${API_VERSION}.pc)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/eds-version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/eds-version.h
	@ONLY
)

# We put the C++ code into a separate static library, so that we can use
# the C linker for libebook-contacts.

add_library(edataserver-private STATIC
	e-alphabet-index-private.cpp
	e-transliterator-private.cpp
)

target_compile_definitions(edataserver-private PRIVATE
	-DG_LOG_DOMAIN=\"e-data-server\"
	-DLIBEDATASERVER_COMPILATION
)

target_compile_options(edataserver-private PUBLIC
	${DATA_SERVER_CFLAGS}
	${ICU_CFLAGS}
)

target_include_directories(edataserver-private PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${DATA_SERVER_INCLUDE_DIRS}
	${ICU_INCLUDE_DIRS}
)

target_link_libraries(edataserver-private
	${DATA_SERVER_LDFLAGS}
	${ICU_LDFLAGS}
)

set(DEPENDENCIES
	camel
	edataserver-private
	edbus-private
)

set(SOURCES
	e-alphabet-index-private.h
	e-cancellable-locks.c
	e-categories.c
	e-client.c
	e-client-private.h
	e-collator.c
	e-credentials.c
	e-extensible.c
	e-extension.c
	e-flag.c
	e-free-form-exp.c
	e-gdata-oauth2-authorizer.c
	e-iterator.c
	e-list.c
	e-list-iterator.c
	e-memory.c
	e-module.c
	e-network-monitor.c
	e-oauth2-service.c
	e-oauth2-service-base.c
	e-oauth2-service-google.c
	e-oauth2-service-outlook.c
	e-oauth2-service-yahoo.c
	e-oauth2-services.c
	e-operation-pool.c
	e-proxy.c
	e-secret-store.c
	e-sexp.c
	e-soup-auth-bearer.c
	e-soup-logger.c
	e-soup-session.c
	e-soup-ssl-trust.c
	e-source.c
	e-source-extension.c
	e-source-address-book.c
	e-source-alarms.c
	e-source-authentication.c
	e-source-autocomplete.c
	e-source-autoconfig.c
	e-source-backend.c
	e-source-calendar.c
	e-source-camel.c
	e-source-collection.c
	e-source-contacts.c
	e-source-credentials-provider.c
	e-source-credentials-provider-impl.c
	e-source-credentials-provider-impl-oauth2.c
	e-source-credentials-provider-impl-password.c
	e-source-goa.c
	e-source-ldap.c
	e-source-local.c
	e-source-mail-account.c
	e-source-mail-composition.c
	e-source-mail-identity.c
	e-source-mail-signature.c
	e-source-mail-submission.c
	e-source-mail-transport.c
	e-source-mdn.c
	e-source-memo-list.c
	e-source-offline.c
	e-source-openpgp.c
	e-source-proxy.c
	e-source-refresh.c
	e-source-registry.c
	e-source-registry-watcher.c
	e-source-resource.c
	e-source-revision-guards.c
	e-source-security.c
	e-source-selectable.c
	e-source-smime.c
	e-source-task-list.c
	e-source-uoa.c
	e-source-weather.c
	e-source-webdav.c
	e-debug-log.c
	e-time-utils.c
	e-transliterator-private.h
	e-uid.c
	e-url.c
	e-webdav-discover.c
	e-webdav-session.c
	e-data-server-util.c
	e-xml-document.c
	e-xml-hash-utils.c
	e-xml-utils.c
	libedataserver-private.h
	eds-version.c
	${CMAKE_CURRENT_BINARY_DIR}/e-source-enumtypes.c
)

set(HEADERS
	libedataserver.h
	e-cancellable-locks.h
	e-categories.h
	e-client.h
	e-collator.h
	e-credentials.h
	e-dataserver-autocleanups.h
	e-extensible.h
	e-extension.h
	e-flag.h
	e-free-form-exp.h
	e-gdata-oauth2-authorizer.h
	e-iterator.h
	e-list.h
	e-list-iterator.h
	e-memory.h
	e-module.h
	e-network-monitor.h
	e-oauth2-service.h
	e-oauth2-service-base.h
	e-oauth2-service-google.h
	e-oauth2-service-outlook.h
	e-oauth2-service-yahoo.h
	e-oauth2-services.h
	e-operation-pool.h
	e-proxy.h
	e-secret-store.h
	e-sexp.h
	e-soup-auth-bearer.h
	e-soup-logger.h
	e-soup-session.h
	e-soup-ssl-trust.h
	e-source.h
	e-source-address-book.h
	e-source-alarms.h
	e-source-authentication.h
	e-source-autocomplete.h
	e-source-autoconfig.h
	e-source-backend.h
	e-source-calendar.h
	e-source-camel.h
	e-source-collection.h
	e-source-contacts.h
	e-source-credentials-provider.h
	e-source-credentials-provider-impl.h
	e-source-credentials-provider-impl-oauth2.h
	e-source-credentials-provider-impl-password.h
	e-source-enums.h
	e-source-extension.h
	e-source-goa.h
	e-source-ldap.h
	e-source-local.h
	e-source-mail-account.h
	e-source-mail-composition.h
	e-source-mail-identity.h
	e-source-mail-signature.h
	e-source-mail-submission.h
	e-source-mail-transport.h
	e-source-mdn.h
	e-source-memo-list.h
	e-source-offline.h
	e-source-openpgp.h
	e-source-proxy.h
	e-source-refresh.h
	e-source-registry.h
	e-source-registry-watcher.h
	e-source-resource.h
	e-source-revision-guards.h
	e-source-security.h
	e-source-selectable.h
	e-source-smime.h
	e-source-task-list.h
	e-source-uoa.h
	e-source-weather.h
	e-source-webdav.h
	e-debug-log.h
	e-time-utils.h
	e-uid.h
	e-url.h
	e-webdav-discover.h
	e-webdav-session.h
	e-data-server-util.h
	e-xml-document.h
	e-xml-hash-utils.h
	e-xml-utils.h
	${CMAKE_CURRENT_BINARY_DIR}/e-source-enumtypes.h
	${CMAKE_CURRENT_BINARY_DIR}/eds-version.h
)

add_library(edataserver SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(edataserver
	${DEPENDENCIES}
)

set_target_properties(edataserver PROPERTIES
	VERSION "${LIBEDATASERVER_CURRENT}.${LIBEDATASERVER_REVISION}.${LIBEDATASERVER_AGE}"
	SOVERSION ${LIBEDATASERVER_CURRENT}
	OUTPUT_NAME edataserver-${API_VERSION}
)

target_compile_definitions(edataserver PRIVATE
	-DG_LOG_DOMAIN=\"e-data-server\"
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DE_DATA_SERVER_LOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DE_DATA_SERVER_IMAGESDIR=\"${imagesdir}\"
	-DE_DATA_SERVER_CREDENTIALMODULEDIR=\"${credentialmoduledir}\"
	-DE_DATA_SERVER_UIMODULEDIR=\"${uimoduledir}\"
	-DE_DATA_SERVER_PRIVDATADIR=\"${privdatadir}\"
	-DLIBEDATASERVER_COMPILATION
)

target_compile_options(edataserver PUBLIC
	${DATA_SERVER_CFLAGS}
	${GCR_BASE_CFLAGS}
	${GIO_UNIX_CFLAGS}
	${ICU_CFLAGS}
	${OAUTH2_JSON_GLIB_CFLAGS}
	${LIBGDATA_CFLAGS}
)

target_include_directories(edataserver PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/private
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${DATA_SERVER_INCLUDE_DIRS}
	${GCR_BASE_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
	${ICU_INCLUDE_DIRS}
	${OAUTH2_JSON_GLIB_INCLUDE_DIRS}
	${LIBGDATA_INCLUDE_DIRS}
)

target_link_libraries(edataserver
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
	${GCR_BASE_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
	${ICU_LDFLAGS}
	${OAUTH2_JSON_GLIB_LDFLAGS}
	${LIBGDATA_LDFLAGS}
)

install(TARGETS edataserver
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedataserver
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 Gio-2.0 Soup-2.4 libxml2-2.0 ${LIBGDATA_GIR})
set(gir_cflags
	-DLIBEDATASERVER_COMPILATION
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/camel
)
set(gir_libs
	camel
	edataserver
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
)

gir_filter_out_sources(gir_sources e-sexp.h)
gir_filter_out_sources(gir_sources -private.h)

gir_add_introspection_simple(
	EDataServer
	libedataserver
	${API_VERSION}
	"libedataserver/libedataserver.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

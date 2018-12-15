glib_mkenums(camel-enumtypes camel-enums.h CAMEL_ENUMTYPES_H)

add_executable(camel-gen-tables
	camel-gen-tables.c
)

target_include_directories(camel-gen-tables PUBLIC
	${CMAKE_BINARY_DIR}
)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/camel-mime-tables.c
	COMMAND ${CMAKE_CURRENT_BINARY_DIR}/camel-gen-tables >${CMAKE_CURRENT_BINARY_DIR}/camel-mime-tables.c
	DEPENDS camel-gen-tables
)

set(SOURCES
	camel.c
	camel-address.c
	camel-async-closure.c
	camel-block-file.c
	camel-certdb.c
	camel-charset-map.c
	camel-cipher-context.c
	camel-data-cache.c
	camel-data-wrapper.c
	camel-db.c
	camel-debug.c
	camel-file-utils.c
	camel-filter-driver.c
	camel-filter-input-stream.c
	camel-filter-output-stream.c
	camel-filter-search.c
	camel-folder-search.c
	camel-folder-summary.c
	camel-folder-thread.c
	camel-folder.c
	camel-gpg-context.c
	camel-html-parser.c
	camel-iconv.c
	camel-index.c
	camel-internet-address.c
	camel-junk-filter.c
	camel-local-settings.c
	camel-lock.c
	camel-medium.c
	camel-memchunk.c
	camel-mempool.c
	camel-message-info.c
	camel-message-info-base.c
	camel-mime-filter-basic.c
	camel-mime-filter-bestenc.c
	camel-mime-filter-canon.c
	camel-mime-filter-charset.c
	camel-mime-filter-crlf.c
	camel-mime-filter-enriched.c
	camel-mime-filter-from.c
	camel-mime-filter-gzip.c
	camel-mime-filter-html.c
	camel-mime-filter-index.c
	camel-mime-filter-linewrap.c
	camel-mime-filter-pgp.c
	camel-mime-filter-progress.c
	camel-mime-filter-tohtml.c
	camel-mime-filter-windows.c
	camel-mime-filter-yenc.c
	camel-mime-filter.c
	camel-mime-message.c
	camel-mime-parser.c
	camel-mime-part-utils.c
	camel-mime-part.c
	camel-mime-utils.c
	camel-msgport.c
	camel-multipart-encrypted.c
	camel-multipart-signed.c
	camel-multipart.c
	camel-named-flags.c
	camel-name-value-array.c
	camel-net-utils.c
	camel-network-service.c
	camel-network-settings.c
	camel-nntp-address.c
	camel-null-output-stream.c
	camel-object-bag.c
	camel-object.c
	camel-offline-folder.c
	camel-offline-settings.c
	camel-offline-store.c
	camel-operation.c
	camel-partition-table.c
	camel-provider.c
	camel-sasl-anonymous.c
	camel-sasl-cram-md5.c
	camel-sasl-digest-md5.c
	camel-sasl-gssapi.c
	camel-sasl-login.c
	camel-sasl-ntlm.c
	camel-sasl-plain.c
	camel-sasl-popb4smtp.c
	camel-sasl-xoauth2.c
	camel-sasl-xoauth2-google.c
	camel-sasl-xoauth2-outlook.c
	camel-sasl.c
	camel-search-private.c
	camel-search-sql-sexp.c
	camel-service.c
	camel-session.c
	camel-settings.c
	camel-sexp.c
	camel-smime-context.c
	camel-store-settings.c
	camel-store-summary.c
	camel-store.c
	camel-stream-buffer.c
	camel-stream-filter.c
	camel-stream-fs.c
	camel-stream-mem.c
	camel-stream-null.c
	camel-stream.c
	camel-string-utils.c
	camel-subscribable.c
	camel-text-index.c
	camel-transport.c
	camel-trie.c
	camel-uid-cache.c
	camel-url-scanner.c
	camel-url.c
	camel-utf8.c
	camel-utils.c
	camel-vee-data-cache.c
	camel-vee-folder.c
	camel-vee-message-info.c
	camel-vee-store.c
	camel-vee-summary.c
	camel-vtrash-folder.c
	camel-weak-ref-group.c
	${CMAKE_CURRENT_BINARY_DIR}/camel-enumtypes.c
	${CMAKE_CURRENT_BINARY_DIR}/camel-mime-tables.c
)

if(WIN32)
	list(APPEND SOURCES
		camel-win32.c
	)
else(WIN32)
	list(APPEND SOURCES
		camel-lock-client.c
		camel-movemail.c
		camel-stream-process.c
	)
endif(WIN32)

set(HEADERS
	camel.h
	camel-address.h
	camel-autocleanups.h
	camel-async-closure.h
	camel-block-file.h
	camel-certdb.h
	camel-charset-map.h
	camel-cipher-context.h
	camel-data-cache.h
	camel-data-wrapper.h
	camel-db.h
	camel-debug.h
	camel-enums.h
	camel-file-utils.h
	camel-filter-driver.h
	camel-filter-input-stream.h
	camel-filter-output-stream.h
	camel-filter-search.h
	camel-folder-search.h
	camel-folder-summary.h
	camel-folder-thread.h
	camel-folder.h
	camel-gpg-context.h
	camel-html-parser.h
	camel-iconv.h
	camel-index.h
	camel-internet-address.h
	camel-junk-filter.h
	camel-local-settings.h
	camel-lock-client.h
	camel-lock-helper.h
	camel-lock.h
	camel-medium.h
	camel-memchunk.h
	camel-mempool.h
	camel-message-info.h
	camel-message-info-base.h
	camel-mime-filter-basic.h
	camel-mime-filter-bestenc.h
	camel-mime-filter-canon.h
	camel-mime-filter-charset.h
	camel-mime-filter-crlf.h
	camel-mime-filter-enriched.h
	camel-mime-filter-from.h
	camel-mime-filter-gzip.h
	camel-mime-filter-html.h
	camel-mime-filter-index.h
	camel-mime-filter-linewrap.h
	camel-mime-filter-pgp.h
	camel-mime-filter-progress.h
	camel-mime-filter-tohtml.h
	camel-mime-filter-windows.h
	camel-mime-filter-yenc.h
	camel-mime-filter.h
	camel-mime-message.h
	camel-mime-parser.h
	camel-mime-part-utils.h
	camel-mime-part.h
	camel-mime-utils.h
	camel-movemail.h
	camel-msgport.h
	camel-multipart-encrypted.h
	camel-multipart-signed.h
	camel-multipart.h
	camel-named-flags.h
	camel-name-value-array.h
	camel-net-utils.h
	camel-network-service.h
	camel-network-settings.h
	camel-nntp-address.h
	camel-null-output-stream.h
	camel-object-bag.h
	camel-object.h
	camel-offline-folder.h
	camel-offline-settings.h
	camel-offline-store.h
	camel-operation.h
	camel-partition-table.h
	camel-provider.h
	camel-sasl-anonymous.h
	camel-sasl-cram-md5.h
	camel-sasl-digest-md5.h
	camel-sasl-gssapi.h
	camel-sasl-login.h
	camel-sasl-ntlm.h
	camel-sasl-plain.h
	camel-sasl-popb4smtp.h
	camel-sasl-xoauth2.h
	camel-sasl-xoauth2-google.h
	camel-sasl-xoauth2-outlook.h
	camel-sasl.h
	camel-search-private.h
	camel-search-sql-sexp.h
	camel-service.h
	camel-session.h
	camel-settings.h
	camel-sexp.h
	camel-smime-context.h
	camel-store-settings.h
	camel-store-summary.h
	camel-store.h
	camel-stream-buffer.h
	camel-stream-filter.h
	camel-stream-fs.h
	camel-stream-mem.h
	camel-stream-null.h
	camel-stream-process.h
	camel-stream.h
	camel-string-utils.h
	camel-subscribable.h
	camel-text-index.h
	camel-transport.h
	camel-trie.h
	camel-uid-cache.h
	camel-url-scanner.h
	camel-url.h
	camel-utf8.h
	camel-utils.h
	camel-vee-data-cache.h
	camel-vee-folder.h
	camel-vee-message-info.h
	camel-vee-store.h
	camel-vee-summary.h
	camel-vtrash-folder.h
	camel-weak-ref-group.h
	${CMAKE_CURRENT_BINARY_DIR}/camel-enumtypes.h
)

add_library(camel SHARED
	${SOURCES}
	${HEADERS}
)

set_target_properties(camel PROPERTIES
	VERSION "${LIBCAMEL_CURRENT}.${LIBCAMEL_REVISION}.${LIBCAMEL_AGE}"
	SOVERSION ${LIBCAMEL_CURRENT}
	OUTPUT_NAME camel-${API_VERSION}
	INSTALL_RPATH ""
)

target_compile_definitions(camel PRIVATE
	-DG_LOG_DOMAIN=\"camel\"
	-DCAMEL_LIBEXECDIR=\"${LIBEXEC_INSTALL_DIR}\"
	-DCAMEL_PROVIDERDIR=\"${camel_providerdir}\"
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DCAMEL_COMPILATION
)

target_compile_options(camel PUBLIC
	${CAMEL_CFLAGS}
	${SOCKET_CFLAGS}
	${ICONV_CFLAGS}
	${REGEX_CFLAGS}
	${LIBDWFL_CFLAGS}
)

target_include_directories(camel PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}
	${CAMEL_INCLUDE_DIRS}
	${SOCKET_INCLUDE_DIRS}
	${ICONV_INCLUDE_DIRS}
	${REGEX_INCLUDE_DIRS}
)

target_link_libraries(camel
	${CAMEL_LDFLAGS}
	${SOCKET_LDFLAGS}
	${ICONV_LDFLAGS}
	${REGEX_LDFLAGS}
	${LIBDWFL_LIBS}
)

install(TARGETS camel
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/camel
)

add_pkgconfig_file(camel.pc.in camel-${API_VERSION}.pc)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes Camel camel)
set(gir_includes GObject-2.0 Gio-2.0 libxml2-2.0)
set(gir_cflags -DCAMEL_COMPILATION)
set(gir_libdirs)
set(gir_libs camel)
set(gir_deps)

gir_add_introspection_simple(
	Camel
	camel
	${API_VERSION}
	"camel/camel.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

if(NOT WIN32)
	add_executable(camel-lock-helper
		camel-lock.c
		camel-lock.h
		camel-lock-helper.c
		camel-lock-helper.h
	)

	set_target_properties(camel-lock-helper PROPERTIES
		OUTPUT_NAME camel-lock-helper-${API_VERSION}
		INSTALL_RPATH ""
	)

	target_compile_definitions(camel-lock-helper PRIVATE
		-DG_LOG_DOMAIN=\"camel-lock-helper\"
		-DCAMEL_LIBEXECDIR=\"${LIBEXEC_INSTALL_DIR}\"
		-DCAMEL_PROVIDERDIR=\"${camel_providerdir}\"
		-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
		-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
		-DCAMEL_COMPILATION
	)

	target_compile_options(camel-lock-helper PUBLIC
		${GNOME_PLATFORM_CFLAGS}
	)

	target_include_directories(camel-lock-helper PUBLIC
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_CURRENT_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}
		${GNOME_PLATFORM_INCLUDE_DIRS}
	)

	target_link_libraries(camel-lock-helper
		${GNOME_PLATFORM_LDFLAGS}
	)

	install(TARGETS camel-lock-helper
		DESTINATION ${LIBEXEC_INSTALL_DIR}
	)
endif(NOT WIN32)

add_executable(camel-index-control
	camel-index-control.c
)

add_dependencies(camel-index-control camel)

set_target_properties(camel-index-control PROPERTIES
	OUTPUT_NAME camel-index-control-${API_VERSION}
)

target_compile_definitions(camel-index-control PRIVATE
	-DG_LOG_DOMAIN=\"camel-index-control-${API_VERSION}\"
	-DCAMEL_LIBEXECDIR=\"${LIBEXEC_INSTALL_DIR}\"
	-DCAMEL_PROVIDERDIR=\"${camel_providerdir}\"
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DLOCALEDIR=\"${LOCALE_INSTALL_DIR}\"
	-DCAMEL_COMPILATION
)

target_compile_options(camel-index-control PUBLIC
	${CAMEL_CFLAGS}
	${SOCKET_CFLAGS}
	${ICONV_CFLAGS}
	${REGEX_CFLAGS}
	${LIBDWFL_CFLAGS}
)

target_include_directories(camel-index-control PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}
	${CAMEL_INCLUDE_DIRS}
	${SOCKET_INCLUDE_DIRS}
	${ICONV_INCLUDE_DIRS}
	${REGEX_INCLUDE_DIRS}
)

target_link_libraries(camel-index-control
	camel
	${CAMEL_LDFLAGS}
	${SOCKET_LDFLAGS}
	${ICONV_LDFLAGS}
	${REGEX_LDFLAGS}
	${LIBDWFL_LDFLAGS}
)

install(TARGETS camel-index-control
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

add_executable(camel-gpg-photo-saver
	camel-gpg-photo-saver.c
)

set_target_properties(camel-gpg-photo-saver PROPERTIES
	INSTALL_RPATH ""
)

target_compile_definitions(camel-gpg-photo-saver PRIVATE
	-DG_LOG_DOMAIN=\"camel-gpg-photo-saver\"
)

target_compile_options(camel-gpg-photo-saver PUBLIC
	${GNOME_PLATFORM_CFLAGS}
)

target_include_directories(camel-gpg-photo-saver PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}
	${GNOME_PLATFORM_INCLUDE_DIRS}
)

target_link_libraries(camel-gpg-photo-saver
	${GNOME_PLATFORM_LDFLAGS}
)

install(TARGETS camel-gpg-photo-saver
	DESTINATION ${LIBEXEC_INSTALL_DIR}
)

add_subdirectory(providers)
add_subdirectory(tests)

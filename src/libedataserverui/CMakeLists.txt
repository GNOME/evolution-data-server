add_pkgconfig_file(libedataserverui.pc.in libedataserverui-${API_VERSION}.pc)

set(SOURCES
	e-cell-renderer-color.c
	e-credentials-prompter.c
	e-credentials-prompter-impl.c
	e-credentials-prompter-impl-oauth2.c
	e-credentials-prompter-impl-password.c
	e-trust-prompt.c
	e-webdav-discover-widget.c
)

set(HEADERS
	libedataserverui.h
	e-cell-renderer-color.h
	e-credentials-prompter.h
	e-credentials-prompter-impl.h
	e-credentials-prompter-impl-oauth2.h
	e-credentials-prompter-impl-password.h
	e-trust-prompt.h
	e-webdav-discover-widget.h
)

set(DEPENDENCIES
	camel
	ebackend
	edataserver
)

add_library(edataserverui SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(edataserverui
	${DEPENDENCIES}
)

set_target_properties(edataserverui PROPERTIES
	VERSION "${LIBEDATASERVERUI_CURRENT}.${LIBEDATASERVERUI_REVISION}.${LIBEDATASERVERUI_AGE}"
	SOVERSION ${LIBEDATASERVERUI_CURRENT}
	OUTPUT_NAME edataserverui-${API_VERSION}
)

target_compile_definitions(edataserverui PRIVATE
	-DG_LOG_DOMAIN=\"e-data-server-ui\"
	-DLIBEDATASERVERUI_COMPILATION
)

target_compile_options(edataserverui PUBLIC
	${BACKEND_CFLAGS}
	${DATA_SERVER_CFLAGS}
	${CAMEL_CFLAGS}
	${CODE_COVERAGE_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${GCR_BASE_CFLAGS}
	${GCR_CFLAGS}
	${GTK_CFLAGS}
	${OAUTH2_JSON_GLIB_CFLAGS}
	${OAUTH2_WEBKIT2GTK_CFLAGS}
)

target_include_directories(edataserverui PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${BACKEND_INCLUDE_DIRS}
	${DATA_SERVER_INCLUDE_DIRS}
	${CAMEL_INCLUDE_DIRS}
	${CODE_COVERAGE_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GCR_BASE_INCLUDE_DIRS}
	${GCR_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
	${OAUTH2_JSON_GLIB_INCLUDE_DIRS}
	${OAUTH2_WEBKIT2GTK_INCLUDE_DIRS}
)

target_link_libraries(edataserverui
	${DEPENDENCIES}
	${BACKEND_LDFLAGS}
	${DATA_SERVER_LDFLAGS}
	${CAMEL_LDFLAGS}
	${CODE_COVERAGE_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${GCR_BASE_LDFLAGS}
	${GCR_LDFLAGS}
	${GTK_LDFLAGS}
	${OAUTH2_JSON_GLIB_LDFLAGS}
	${OAUTH2_WEBKIT2GTK_LDFLAGS}
)

install(TARGETS edataserverui
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedataserverui
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 Gio-2.0 Gtk-3.0 Soup-2.4)
set(gir_cflags
	-DLIBEDATASERVERUI_COMPILATION
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)
set(gir_libs
	camel
	edataserver
	edataserverui
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
)

gir_add_introspection_simple(
	EDataServerUI
	libedataserverui
	${API_VERSION}
	"libedataserverui/libedataserverui.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)

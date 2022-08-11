if(ENABLE_GTK)
set(GTK_DEPENDENCY gtk+-3.0)
set(UI_VERSION)
set(UI_API_VERSION ${API_VERSION})
add_pkgconfig_file(libedataserverui.pc.in libedataserverui-${UI_API_VERSION}.pc)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/libedataserverui.h.in
	${CMAKE_CURRENT_BINARY_DIR}/libedataserverui${UI_VERSION}.h
	@ONLY
)

set(SOURCES
	e-buffer-tagger.c
	e-cell-renderer-color.c
	e-certificate-widget.c
	e-credentials-prompter.c
	e-credentials-prompter-impl.c
	e-credentials-prompter-impl-oauth2.c
	e-credentials-prompter-impl-password.c
	e-reminders-widget.c
	e-trust-prompt.c
	e-webdav-discover-widget.c
	libedataserverui-private.h
	libedataserverui-private.c
)

set(HEADERS
	${CMAKE_CURRENT_BINARY_DIR}/libedataserverui${UI_VERSION}.h
	e-buffer-tagger.h
	e-cell-renderer-color.h
	e-certificate-widget.h
	e-credentials-prompter.h
	e-credentials-prompter-impl.h
	e-credentials-prompter-impl-oauth2.h
	e-credentials-prompter-impl-password.h
	e-reminders-widget.h
	e-trust-prompt.h
	e-webdav-discover-widget.h
)

set(DEPENDENCIES
	camel
	ecal
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
	OUTPUT_NAME edataserverui-${UI_API_VERSION}
)

target_compile_definitions(edataserverui PRIVATE
	-DG_LOG_DOMAIN=\"e-data-server-ui\"
	-DLIBEDATASERVERUI_COMPILATION
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DE_DATA_SERVER_UIMODULEDIR=\"${uimoduledir}\"
	-DE_DATA_SERVER_ICONDIR=\"${icondir}\"
	-DGDK_VERSION_MAX_ALLOWED=${gdk_encoded_version}
	-DGDK_VERSION_MIN_REQUIRED=${gdk_encoded_version}
)

target_compile_options(edataserverui PUBLIC
	${DATA_SERVER_CFLAGS}
	${CAMEL_CFLAGS}
	${CODE_COVERAGE_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${GTK_CFLAGS}
	${JSON_GLIB_CFLAGS}
	${OAUTH2_WEBKITGTK_CFLAGS}
)

target_include_directories(edataserverui PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${DATA_SERVER_INCLUDE_DIRS}
	${CAMEL_INCLUDE_DIRS}
	${CODE_COVERAGE_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
	${JSON_GLIB_INCLUDE_DIRS}
	${OAUTH2_WEBKITGTK_INCLUDE_DIRS}
)

target_link_libraries(edataserverui
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
	${CAMEL_LDFLAGS}
	${CODE_COVERAGE_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${GTK_LDFLAGS}
	${JSON_GLIB_LDFLAGS}
	${OAUTH2_WEBKITGTK_LDFLAGS}
)

install(TARGETS edataserverui
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedataserverui
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 Gio-2.0 Gtk-3.0 Soup-3.0)
set(gir_cflags
	-DLIBEDATASERVERUI_COMPILATION
	-I${CMAKE_BINARY_DIR}/src/calendar
	-I${CMAKE_SOURCE_DIR}/src/calendar
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/calendar/libecal
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)
set(gir_libs
	camel
	ecal
	edataserver
	edataserverui
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/calendar/libecal/ECal-${CAL_API_VERSION}.gir
)

gir_add_introspection_simple(
	EDataServerUI
	libedataserverui
	${UI_API_VERSION}
	"libedataserverui${UI_VERSION}/libedataserverui${UI_VERSION}.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)
endif(ENABLE_GTK)

#-----------------------------------------------------------------------------

if(ENABLE_GTK4)
set(GTK_DEPENDENCY gtk4)
set(UI_VERSION 4)
set(UI_API_VERSION ${LIBEDATASERVERUI4_API_VERSION})
add_pkgconfig_file(libedataserverui.pc.in libedataserverui${UI_VERSION}-${UI_API_VERSION}.pc)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/libedataserverui.h.in
	${CMAKE_CURRENT_BINARY_DIR}/libedataserverui${UI_VERSION}.h
	@ONLY
)

set(SOURCES
	e-buffer-tagger.c
	e-cell-renderer-color.c
	e-certificate-widget.c
	e-credentials-prompter.c
	e-credentials-prompter-impl.c
	e-credentials-prompter-impl-oauth2.c
	e-credentials-prompter-impl-password.c
	e-reminders-widget.c
	e-trust-prompt.c
	e-webdav-discover-widget.c
	libedataserverui-private.h
	libedataserverui-private.c
)

set(HEADERS
	${CMAKE_CURRENT_BINARY_DIR}/libedataserverui${UI_VERSION}.h
	e-buffer-tagger.h
	e-cell-renderer-color.h
	e-certificate-widget.h
	e-credentials-prompter.h
	e-credentials-prompter-impl.h
	e-credentials-prompter-impl-oauth2.h
	e-credentials-prompter-impl-password.h
	e-reminders-widget.h
	e-trust-prompt.h
	e-webdav-discover-widget.h
)

set(DEPENDENCIES
	camel
	ecal
	edataserver
)

add_library(edataserverui${UI_VERSION} SHARED
	${SOURCES}
	${HEADERS}
)

add_dependencies(edataserverui${UI_VERSION}
	${DEPENDENCIES}
)

set_target_properties(edataserverui${UI_VERSION} PROPERTIES
	VERSION "${LIBEDATASERVERUI4_CURRENT}.${LIBEDATASERVERUI4_REVISION}.${LIBEDATASERVERUI4_AGE}"
	SOVERSION ${LIBEDATASERVERUI4_CURRENT}
	OUTPUT_NAME edataserverui${UI_VERSION}-${UI_API_VERSION}
)

target_compile_definitions(edataserverui${UI_VERSION} PRIVATE
	-DG_LOG_DOMAIN=\"e-data-server-ui${UI_VERSION}\"
	-DLIBEDATASERVERUI_COMPILATION
	-DE_DATA_SERVER_PREFIX=\"${CMAKE_INSTALL_PREFIX}\"
	-DE_DATA_SERVER_UIMODULEDIR=\"${uimoduledir}${UI_VERSION}\"
	-DE_DATA_SERVER_ICONDIR=\"${icondir}\"
	-DGDK_VERSION_MAX_ALLOWED=${gdk4_encoded_version}
	-DGDK_VERSION_MIN_REQUIRED=${gdk4_encoded_version}
)

target_compile_options(edataserverui${UI_VERSION} PUBLIC
	${DATA_SERVER_CFLAGS}
	${CAMEL_CFLAGS}
	${CODE_COVERAGE_CFLAGS}
	${GNOME_PLATFORM_CFLAGS}
	${GTK4_CFLAGS}
	${JSON_GLIB_CFLAGS}
	${OAUTH2_WEBKITGTK4_CFLAGS}
)

target_include_directories(edataserverui${UI_VERSION} PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${DATA_SERVER_INCLUDE_DIRS}
	${CAMEL_INCLUDE_DIRS}
	${CODE_COVERAGE_INCLUDE_DIRS}
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GTK4_INCLUDE_DIRS}
	${JSON_GLIB_INCLUDE_DIRS}
	${OAUTH2_WEBKITGTK4_INCLUDE_DIRS}
)

target_link_libraries(edataserverui${UI_VERSION}
	${DEPENDENCIES}
	${DATA_SERVER_LDFLAGS}
	${CAMEL_LDFLAGS}
	${CODE_COVERAGE_LDFLAGS}
	${GNOME_PLATFORM_LDFLAGS}
	${GTK4_LDFLAGS}
	${JSON_GLIB_LDFLAGS}
	${OAUTH2_WEBKITGTK4_LDFLAGS}
)

install(TARGETS edataserverui${UI_VERSION}
	DESTINATION ${LIB_INSTALL_DIR}
)

install(FILES ${HEADERS}
	DESTINATION ${privincludedir}/libedataserverui${UI_VERSION}
)

# create libedataserverui4/... in the build dir, thus .gir can find it
file(COPY ${HEADERS}
	DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/libedataserverui${UI_VERSION}/
)

set(gir_sources ${SOURCES} ${HEADERS})
set(gir_identifies_prefixes E)
set(gir_includes GObject-2.0 Gio-2.0 Gtk-4.0 Soup-3.0)
set(gir_cflags
	-DLIBEDATASERVERUI_COMPILATION
	-I${CMAKE_BINARY_DIR}/src/calendar
	-I${CMAKE_SOURCE_DIR}/src/calendar
	-I${CMAKE_CURRENT_BINARY_DIR}
)
set(gir_libdirs
	${CMAKE_BINARY_DIR}/src/private
	${CMAKE_BINARY_DIR}/src/calendar/libecal
	${CMAKE_BINARY_DIR}/src/camel
	${CMAKE_BINARY_DIR}/src/libedataserver
)
set(gir_libs
	camel
	ecal
	edataserver
	edataserverui${UI_VERSION}
)
set(gir_deps
	${CMAKE_BINARY_DIR}/src/camel/Camel-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/libedataserver/EDataServer-${API_VERSION}.gir
	${CMAKE_BINARY_DIR}/src/calendar/libecal/ECal-${CAL_API_VERSION}.gir
)

gir_add_introspection_simple(
	EDataServerUI${UI_VERSION}
	libedataserverui${UI_VERSION}
	${UI_API_VERSION}
	"libedataserverui${UI_VERSION}/libedataserverui${UI_VERSION}.h"
	gir_identifies_prefixes
	gir_includes
	gir_cflags
	gir_libdirs
	gir_libs
	gir_deps
	gir_sources
)
endif(ENABLE_GTK4)

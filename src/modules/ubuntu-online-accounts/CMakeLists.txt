# ******************************
# Data files
# ******************************

set(filedeps)

set(servicetype_files)
set(servicetype_files_gen)
list(APPEND servicetype_files
	mail.service-type
	calendar.service-type
	contacts.service-type
)

set(service_files)
set(service_files_gen)
list(APPEND service_files
	google-gmail.service
	google-calendar.service
	google-contacts.service
	windows-live-mail.service
	yahoo-mail.service
	yahoo-calendar.service
)

configure_file(evolution-data-server.application.in.in
	evolution-data-server.application.in
	@ONLY
)

intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server.application.in evolution-data-server.application --no-translations --xml-style --utf8)

configure_file(evolution-data-server-uoa.desktop.in.in
	evolution-data-server-uoa.desktop.in
	@ONLY
)

intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server-uoa.desktop.in evolution-data-server-uoa.desktop --desktop-style --utf8)

list(APPEND filedeps ${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server.application)
list(APPEND filedeps ${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server-uoa.desktop)

foreach(_file IN LISTS servicetype_files)
	configure_file(${_file}.in.in
		${_file}.in
		@ONLY
	)
	intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/${_file}.in ${_file} --no-translations --xml-style --utf8)
	list(APPEND servicetype_files_gen ${CMAKE_CURRENT_BINARY_DIR}/${_file})
	list(APPEND filedeps ${CMAKE_CURRENT_BINARY_DIR}/${_file})
endforeach(_file)

foreach(_file IN LISTS service_files)
	configure_file(${_file}.in.in
		${_file}.in
		@ONLY
	)
	intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/${_file}.in ${_file} --no-translations --xml-style --utf8)
	list(APPEND service_files_gen ${CMAKE_CURRENT_BINARY_DIR}/${_file})
	list(APPEND filedeps ${CMAKE_CURRENT_BINARY_DIR}/${_file})
endforeach(_file)

set(desktopdir ${SHARE_INSTALL_PREFIX}/applications)
pkg_check_variable(libaccountsprefixdir libaccounts-glib prefix)
pkg_check_variable(applicationdir libaccounts-glib applicationfilesdir)
pkg_check_variable(servicetypedir libaccounts-glib servicetypefilesdir)
pkg_check_variable(servicedir libaccounts-glib servicefilesdir)

# To honor configured prefix, but still use the right place
string(REPLACE "${libaccountsprefixdir}" "${CMAKE_INSTALL_PREFIX}" applicationdir "${applicationdir}")
string(REPLACE "${libaccountsprefixdir}" "${CMAKE_INSTALL_PREFIX}" servicetypedir "${servicetypedir}")
string(REPLACE "${libaccountsprefixdir}" "${CMAKE_INSTALL_PREFIX}" servicedir "${servicedir}")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server.application
	DESTINATION ${applicationdir}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server-uoa.desktop
	DESTINATION ${desktopdir}
)

install(FILES ${servicetype_files_gen}
	DESTINATION ${servicetypedir}
)

install(FILES ${service_files_gen}
	DESTINATION ${servicedir}
)

# ******************************
# Source registry module
# ******************************

set(sources
	module-ubuntu-online-accounts.c
	uoa-utils.c
	uoa-utils.h
)
set(extra_deps)
set(extra_filedeps
	${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server.application
	${CMAKE_CURRENT_BINARY_DIR}/evolution-data-server-uoa.desktop
	${servicetype_files_gen}
	${service_files_gen}
)
set(extra_defines)
set(extra_cflags
	${LIBACCOUNTS_GLIB_CFLAGS}
	${LIBSIGNON_GLIB_CFLAGS}
	${JSON_GLIB_CFLAGS}
	${REST_CFLAGS}
)
set(extra_incdirs
	${LIBACCOUNTS_GLIB_INCLUDE_DIRS}
	${LIBSIGNON_GLIB_INCLUDE_DIRS}
	${JSON_GLIB_INCLUDE_DIRS}
	${REST_INCLUDE_DIRS}
)
set(extra_ldflags
	${LIBACCOUNTS_GLIB_LDFLAGS}
	${LIBSIGNON_GLIB_LDFLAGS}
	${JSON_GLIB_LDFLAGS}
	${REST_LDFLAGS}
)

add_source_registry_module(module-ubuntu-online-accounts
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

add_custom_target(module-ubuntu-online-accounts-files
	DEPENDS ${filedeps}
)

add_dependencies(module-ubuntu-online-accounts module-ubuntu-online-accounts-files)

# ******************************
# Credentials module
# ******************************

set(sources
	module-credentials-uoa.c
	e-signon-session-password.c
	e-signon-session-password.h
)

add_credentials_module(module-credentials-uoa
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

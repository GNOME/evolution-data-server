# ********************************
# evolution-alarm-notify
# ********************************

set(desktopdir ${SHARE_INSTALL_PREFIX}/applications)
if(HAVE_GTK)
	set(autostartdir ${SYSCONF_INSTALL_DIR}/xdg/autostart)
	set(alarm_notify_icon hicolor_apps_scalable_org.gnome.Evolution-alarm-notify.svg)

	configure_file(org.gnome.Evolution-alarm-notify.desktop.in.in
		org.gnome.Evolution-alarm-notify.desktop.in
		@ONLY
	)

	i18n_merge_file(${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution-alarm-notify.desktop.in org.gnome.Evolution-alarm-notify.desktop ${CMAKE_SOURCE_DIR}/po)

	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution-alarm-notify.desktop
		DESTINATION ${autostartdir}
	)

	# To have GNOME Shell show the GNotification notifications from it; otherwise it ignores them
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution-alarm-notify.desktop
		DESTINATION ${desktopdir}
	)

	add_icon_cache_files("${SHARE_INSTALL_PREFIX}" alarm_notify_icon)
endif(HAVE_GTK)

# ********************************
# OAuth2 Handler helper
# ********************************

set(OAUTH2_SCHEMES "x-scheme-handler/eds-oauth2;")
if(NOT WITH_GOOGLE_CLIENT_ID STREQUAL "")
	set(CMAKE_REQUIRED_DEFINITIONS ${DATA_SERVER_CFLAGS})
	set(CMAKE_REQUIRED_INCLUDES ${CMAKE_BINARY_DIR} ${DATA_SERVER_INCLUDE_DIRS})
	set(CMAKE_REQUIRED_LIBRARIES ${DATA_SERVER_LDFLAGS})
	list(APPEND CMAKE_REQUIRED_DEFINITIONS -DG_LOG_DOMAIN=\"oauth2-value-helper\" -DBUILDING_VALUE_HELPER=1)
	file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/oauth2-google-client-id" _binary_dir_to_file)
	CHECK_C_SOURCE_RUNS("#define DECODE_KEY \"${WITH_GOOGLE_CLIENT_ID}\"
			     #define DECODE_TO_FILE \"${_binary_dir_to_file}\"
			     #define DECODE_REVERSED 1
			     #include \"${CMAKE_SOURCE_DIR}/src/libedataserver/e-oauth2-service.c\"" _decoded)
	file(READ ${_binary_dir_to_file} _google_oauth2_scheme)
	unset(_binary_dir_to_file)
	unset(_decoded)
	unset(CMAKE_REQUIRED_LIBRARIES)
	unset(CMAKE_REQUIRED_INCLUDES)
	unset(CMAKE_REQUIRED_DEFINITIONS)

	if(NOT _google_oauth2_scheme STREQUAL "")
		set(OAUTH2_SCHEMES "x-scheme-handler/${_google_oauth2_scheme};${OAUTH2_SCHEMES}")
	endif(NOT _google_oauth2_scheme STREQUAL "")
	unset(_google_oauth2_scheme)
endif(NOT WITH_GOOGLE_CLIENT_ID STREQUAL "")

configure_file(org.gnome.evolution-data-server.OAuth2-handler.desktop.in
	org.gnome.evolution-data-server.OAuth2-handler.desktop.in
	@ONLY
)

i18n_merge_file(${CMAKE_CURRENT_BINARY_DIR}/org.gnome.evolution-data-server.OAuth2-handler.desktop.in org.gnome.evolution-data-server.OAuth2-handler.desktop ${CMAKE_SOURCE_DIR}/po)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.evolution-data-server.OAuth2-handler.desktop
	DESTINATION ${desktopdir}
)

# ********************************
# GSettings schemas
# ********************************

set(SCHEMAS
	org.gnome.Evolution.DefaultSources.gschema.xml
	org.gnome.evolution-data-server.gschema.xml
	org.gnome.evolution-data-server.calendar.gschema.xml
	org.gnome.evolution-data-server.addressbook.gschema.xml
	org.gnome.evolution.eds-shell.gschema.xml
	org.gnome.evolution.shell.network-config.gschema.xml
)

set(BUILT_SCHEMAS)
foreach(_schema IN LISTS SCHEMAS)
	configure_file(${_schema}.in ${_schema} COPYONLY)
	list(APPEND BUILT_SCHEMAS ${CMAKE_CURRENT_BINARY_DIR}/${_schema})
endforeach(_schema)

add_custom_command(OUTPUT gschemas.compiled
	COMMAND ${GLIB_COMPILE_SCHEMAS} .
)

add_custom_target(data-files ALL
	DEPENDS gschemas.compiled
		$<$<BOOL:${HAVE_GTK}>:org.gnome.Evolution-alarm-notify.desktop>
		org.gnome.evolution-data-server.OAuth2-handler.desktop
)

add_gsettings_schemas(data-files ${BUILT_SCHEMAS})

# ********************************
# GConf convert file
# ********************************

install(FILES evolution-data-server.convert
	DESTINATION ${SHARE_INSTALL_PREFIX}/GConf/gsettings
)

# ********************************
# Images
# ********************************

set(IMAGES
	category_birthday_16.png
	category_business_16.png
	category_favorites_16.png
	category_gifts_16.png
	category_goals_16.png
	category_holiday_16.png
	category_holiday-cards_16.png
	category_hot-contacts_16.png
	category_ideas_16.png
	category_international_16.png
	category_key-customer_16.png
	category_miscellaneous_16.png
	category_personal_16.png
	category_phonecalls_16.png
	category_status_16.png
	category_strategies_16.png
	category_suppliers_16.png
	category_time-and-expenses_16.png
)

install(FILES ${IMAGES}
	DESTINATION ${imagesdir}
)

# ********************************
# Fallback icons
# ********************************

set(private_icons
	hicolor_status_16x16_appointment-missed.png
	hicolor_status_22x22_appointment-missed.png
	hicolor_status_24x24_appointment-missed.png
	hicolor_status_32x32_appointment-missed.png
	hicolor_status_48x48_appointment-missed.png
	hicolor_status_256x256_appointment-missed.png
	hicolor_status_16x16_appointment-soon.png
	hicolor_status_22x22_appointment-soon.png
	hicolor_status_24x24_appointment-soon.png
	hicolor_status_32x32_appointment-soon.png
	hicolor_status_48x48_appointment-soon.png
	hicolor_status_256x256_appointment-soon.png
	hicolor_status_16x16_dialog-password.png
	hicolor_status_22x22_dialog-password.png
	hicolor_status_24x24_dialog-password.png
	hicolor_status_32x32_dialog-password.png
	hicolor_status_48x48_dialog-password.png
	hicolor_status_256x256_dialog-password.png
	hicolor_status_16x16_dialog-warning.png
	hicolor_status_22x22_dialog-warning.png
	hicolor_status_24x24_dialog-warning.png
	hicolor_status_32x32_dialog-warning.png
	hicolor_status_48x48_dialog-warning.png
	hicolor_status_256x256_dialog-warning.png
)

add_icon_cache_files("${privdatadir}" private_icons)

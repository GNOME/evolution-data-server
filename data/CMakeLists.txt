# ********************************
# evolution-alarm-notify
# ********************************

set(autostartdir ${SYSCONF_INSTALL_DIR}/xdg/autostart)

configure_file(org.gnome.Evolution-alarm-notify.desktop.in.in
	org.gnome.Evolution-alarm-notify.desktop.in
	@ONLY
)

intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution-alarm-notify.desktop.in org.gnome.Evolution-alarm-notify.desktop --desktop-style --utf8)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution-alarm-notify.desktop
	DESTINATION ${autostartdir}
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
	intltool_merge(${_schema}.in ${_schema} --no-translations --xml-style --utf8)
	list(APPEND BUILT_SCHEMAS ${CMAKE_CURRENT_BINARY_DIR}/${_schema})
endforeach(_schema)

add_custom_command(OUTPUT gschemas.compiled
	COMMAND ${GLIB_COMPILE_SCHEMAS} .
)

add_custom_target(data-files ALL
	DEPENDS gschemas.compiled
		org.gnome.Evolution-alarm-notify.desktop
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

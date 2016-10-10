set(SCHEMAS
	org.gnome.Evolution.DefaultSources.gschema.xml
	org.gnome.evolution-data-server.gschema.xml
	org.gnome.evolution-data-server.calendar.gschema.xml
	org.gnome.evolution-data-server.addressbook.gschema.xml
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
)

add_gsettings_schemas(data-files ${BUILT_SCHEMAS})

install(FILES evolution-data-server.convert
	DESTINATION ${SHARE_INSTALL_DIR}/GConf/gsettings
)

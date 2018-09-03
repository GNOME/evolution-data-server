add_subdirectory(addressbook-export)
add_subdirectory(list-sources)

if(ENABLE_DBUS_SESSION_TOOL)
	add_subdirectory(evolution-dbus-session)
endif(ENABLE_DBUS_SESSION_TOOL)

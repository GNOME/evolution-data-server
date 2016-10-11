add_subdirectory(caldav)
add_subdirectory(contacts)
add_subdirectory(file)
add_subdirectory(http)

if(HAVE_LIBGDATA)
	add_subdirectory(gtasks)
endif(HAVE_LIBGDATA)

if(ENABLE_WEATHER)
	add_subdirectory(weather)
endif(ENABLE_WEATHER)

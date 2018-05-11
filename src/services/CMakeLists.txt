add_subdirectory(evolution-addressbook-factory)
add_subdirectory(evolution-calendar-factory)
add_subdirectory(evolution-source-registry)

if(HAVE_GTK)
	add_subdirectory(evolution-alarm-notify)
	add_subdirectory(evolution-user-prompter)
endif(HAVE_GTK)

add_subdirectory(camel)
add_subdirectory(evolution-data-server)

if(WITH_PRIVATE_DOCS)
	add_subdirectory(private)
endif(WITH_PRIVATE_DOCS)

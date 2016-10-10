if(HAVE_DB_LOAD)

	set(GENERATED_DBS_VERSIONS
		3.0
		3.2
		3.4
		3.6
	)

	set(GENERATED_DBS)

	foreach(_ver ${GENERATED_DBS_VERSIONS})
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_ver}/addressbook.db
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${_ver}
			COMMAND ${DB_LOAD} -f ${CMAKE_CURRENT_SOURCE_DIR}/${_ver}/addressbook.dump ${CMAKE_CURRENT_BINARY_DIR}/${_ver}/addressbook.db
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_ver}/addressbook.dump
		)
		list(APPEND GENERATED_DBS ${CMAKE_CURRENT_BINARY_DIR}/${_ver}/addressbook.db)
	endforeach(_ver)

	add_custom_target(libdb-addressbooks
		DEPENDS ${GENERATED_DBS}
	)

	add_dependencies(check libdb-addressbooks)

endif(HAVE_DB_LOAD)

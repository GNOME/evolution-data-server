set(DEPENDENCIES
	ebook
	ebook-contacts
	edataserver
)

set(SOURCES
	cursor-data.c
	cursor-data.h
	cursor-example.c
	cursor-example.h
	cursor-navigator.c
	cursor-navigator.h
	cursor-search.c
	cursor-search.h
	cursor-slot.c
	cursor-slot.h
	main.c
	${CMAKE_CURRENT_BINARY_DIR}/cursor-example-resources.c
	${CMAKE_CURRENT_BINARY_DIR}/cursor-example-resources.h
)

add_executable(cursor-example
	${SOURCES}
)

add_dependencies(cursor-example
	${DEPENDENCIES}
)

target_compile_definitions(cursor-example PRIVATE
	-DG_LOG_DOMAIN=\"cursor-example\"
	-DSRCDIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_compile_options(cursor-example PUBLIC
	${EXAMPLES_CFLAGS}
)

target_include_directories(cursor-example PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_CURRENT_BINARY_DIR}
	${EXAMPLES_INCLUDE_DIRS}
)

target_link_libraries(cursor-example
	${DEPENDENCIES}
	${EXAMPLES_LDFLAGS}
)

set(UI_FILES
	cursor-example.ui
	cursor-search.ui
	cursor-slot.ui
)

glib_compile_resources(${CMAKE_CURRENT_SOURCE_DIR} cursor-example-resources _cursor_example cursor-example.gresources.xml ${UI_FILES})

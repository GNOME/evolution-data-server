# GIDocgen.cmake
#
# Adds an option -DENABLE_GI_DOCGEN=OFF and helper commands
#
# Functions:
#
# gi_docgen(_output_filename_noext _define_name _enums_header ...)
#    runs glib-mkenums to generate enumtypes .h and .c files from multiple
#    _enums_header. It searches for files in the current source directory and
#    exports to the current binary directory.

include(CMakeParseArguments)

add_printable_option(ENABLE_GI_DOCGEN "Use gi-docgen to build documentation" OFF)
if(ENABLE_GI_DOCGEN)
	if(NOT ENABLE_INTROSPECTION)
		message(FATAL_ERROR "Documentation generation with gi-docgen requires introspection generation to be enabled, use -DENABLE_INTROSPECTION=ON to enable it, or disable gi-docgen documentation generation with -DENABLE_GI_DOCGEN=OFF")
	endif(NOT ENABLE_INTROSPECTION)
	find_program(GI_DOCGEN gi-docgen)
	if(NOT GI_DOCGEN)
		message(FATAL_ERROR "Cannot find gi-docgen. Install it or disable documentation generation with -DENABLE_GI_DOCGEN=OFF")
	endif(NOT GI_DOCGEN)
endif(ENABLE_GI_DOCGEN)

macro(generate_gi_documentation _target _config _gir_path)
	cmake_parse_arguments(GIDOCGEN_ARG "" "" "CONTENT_DIRS;INCLUDE_PATHS;TEMPLATE_DIRS" ${ARGN} )

	set(EXTRA_ARGS)
	foreach(_item IN LISTS GIDOCGEN_ARG_CONTENT_DIRS)
		list(APPEND EXTRA_ARGS "--content-dir=${_item}")
	endforeach()
	foreach(_item IN LISTS GIDOCGEN_ARG_INCLUDE_PATHS)
		list(APPEND EXTRA_ARGS "--add-include-path=${_item}")
	endforeach()
	foreach(_item IN LISTS GIDOCGEN_ARG_TEMPLATE_DIRS)
		list(APPEND EXTRA_ARGS "--templates-dir=${_item}")
	endforeach()
	get_filename_component(_gir_filename ${_gir_path} NAME)
	gir_girfilename_to_target(_gir_target ${_gir_filename})
	add_custom_target(
		${_target}-doc ALL
		COMMAND
			${GI_DOCGEN}
			generate
			--config ${_config}
			--content-dir ${CMAKE_CURRENT_SOURCE_DIR}
			--no-namespace-dir
			--output-dir ${CMAKE_CURRENT_BINARY_DIR}/${_target}
			--quiet
			${EXTRA_ARGS}
			${_gir_path}
		DEPENDS
			${_gir_target}
		WORKING_DIRECTORY
			${CMAKE_CURRENT_BINARY_DIR}
		COMMENT
		"Generate documentation for ${_target}"
	)
	install(
		DIRECTORY
			"${CMAKE_CURRENT_BINARY_DIR}/${_target}"
		DESTINATION
			"${SHARE_INSTALL_PREFIX}/doc"
	)
endmacro(generate_gi_documentation)

# GObjectIntrospection.cmake
#
# Adds an option -DENABLE_INTROSPECTION=OFF and helper commands which work only
# when the introspection is enabled.
#
# Most of the script is copied and tuned from libical, which states:
#    Copyright (C) 2010, Pino Toscano, <pino at kde.org>
#
#    Redistribution and use is allowed according to the terms of the BSD license.
#    For details see the accompanying COPYING-CMAKE-SCRIPTS file.

include(PrintableOptions)
include(PkgConfigEx)
include(CMakeParseArguments)

add_printable_option(ENABLE_INTROSPECTION "Enable GObject introspection" OFF)

if(ENABLE_INTROSPECTION)
	pkg_check_modules_for_option(ENABLE_INTROSPECTION "GObject introspection" GOBJECT_INTROSPECTION gobject-introspection-1.0>=1.59.1)

	pkg_check_variable(G_IR_SCANNER gobject-introspection-1.0 g_ir_scanner)
	pkg_check_variable(G_IR_COMPILER gobject-introspection-1.0 g_ir_compiler)

	if(NOT G_IR_SCANNER)
		message(FATAL_ERROR "g-ir-scanner not provided by gobject-introspection-1.0, you can disable GObject introspection by -DENABLE_INTROSPECTION=OFF")
	endif(NOT G_IR_SCANNER)
	if(NOT G_IR_COMPILER)
		message(FATAL_ERROR "g-ir-compiler not provided by gobject-introspection-1.0, you can disable GObject introspection by -DENABLE_INTROSPECTION=OFF")
	endif(NOT G_IR_COMPILER)
endif(ENABLE_INTROSPECTION)

macro(_gir_list_prefix _outvar _listvar _prefix)
	set(${_outvar})
	foreach(_item IN LISTS ${_listvar})
		list(APPEND ${_outvar} ${_prefix}${_item})
	endforeach()
endmacro(_gir_list_prefix)

macro(_gir_list_prefix_libs _outvar _listvar _prefix)
	set(${_outvar})
	foreach(_item IN LISTS ${_listvar})
		if(TARGET ${_item})
			get_target_property(_output_name ${_item} OUTPUT_NAME)
			list(APPEND ${_outvar} ${_prefix}${_output_name})
		else(TARGET ${_item})
			message(FATAL_ERROR "'${_item}' not found as a target, possibly typo or reorder target definitions")
		endif(TARGET ${_item})
	endforeach()
endmacro(_gir_list_prefix_libs)

macro(_gir_list_prefix_path_to_string _outvar _listvar _prefix)
	set(${_outvar})
	foreach(_item IN LISTS ${_listvar})
		get_filename_component(_dir "${_item}" DIRECTORY)
		if(_dir STREQUAL "")
			set(${_outvar} "${${_outvar}}${_prefix}/${_item}\n")
		else(_dir STREQUAL "")
			set(${_outvar} "${${_outvar}}${_item}\n")
		endif(_dir STREQUAL "")
	endforeach()
endmacro(_gir_list_prefix_path_to_string)

macro(gir_construct_names _prefix _version _out_girname _out_varsprefix)
	set(${_out_girname} "${_prefix}-${_version}.gir")
	set(_varsprefix ${${_out_girname}})

	string(REPLACE "-" "_" _varsprefix "${_varsprefix}")
	string(REPLACE "." "_" _varsprefix "${_varsprefix}")

	set(${_out_varsprefix} ${_varsprefix})
endmacro(gir_construct_names)

macro(gir_girfilename_to_target _outvar _girfilename)
	set(${_outvar})
	foreach(_gir_name "${_girfilename}" ${ARGN})
		string(REPLACE "-" "_" _gir_name "${_gir_name}")
		string(REPLACE "." "_" _gir_name "${_gir_name}")
		list(APPEND ${_outvar} gir-girs-${_gir_name})
	endforeach(_gir_name)
endmacro(gir_girfilename_to_target)

# the macro does something only if ENABLE_INTROSPECTION is ON
# optionally ${_gir_name}_SKIP_TYPELIB can be set to ON to not build .typelib file, only the .gir file
macro(gir_add_introspection gir)
	if(ENABLE_INTROSPECTION)
		set(_gir_girs)
		set(_gir_typelibs)

		set(_gir_name "${gir}")

		## Transform the gir filename to something which can reference through a variable
		## without automake/make complaining, eg Gtk-2.0.gir -> Gtk_2_0_gir
		string(REPLACE "-" "_" _gir_name "${_gir_name}")
		string(REPLACE "." "_" _gir_name "${_gir_name}")

		# Namespace and Version is either fetched from the gir filename
		# or the _NAMESPACE/_VERSION variable combo
		set(_gir_namespace "${${_gir_name}_NAMESPACE}")
		if (_gir_namespace STREQUAL "")
			string(REGEX REPLACE "([^-]+)-.*" "\\1" _gir_namespace "${gir}")
		endif ()
		set(_gir_version "${${_gir_name}_VERSION}")
		if (_gir_version STREQUAL "")
			string(REGEX REPLACE ".*-([^-]+).gir" "\\1" _gir_version "${gir}")
		endif ()

		# _PROGRAM is an optional variable which needs its own --program argument
		set(_gir_program "${${_gir_name}_PROGRAM}")
		if (NOT _gir_program STREQUAL "")
			set(_gir_program "--program=${_gir_program}")
		endif ()

		# Variables which provides a list of things
		_gir_list_prefix_libs(_gir_libraries ${_gir_name}_LIBS "--library=")
		_gir_list_prefix(_gir_packages ${_gir_name}_PACKAGES "--pkg=")
		_gir_list_prefix(_gir_includes ${_gir_name}_INCLUDES "--include=")

		# Reuse the LIBTOOL variable from by automake if it's set
		set(_gir_libtool "--no-libtool")

		_gir_list_prefix_path_to_string(_gir_files ${${_gir_name}_FILES} "${CMAKE_CURRENT_SOURCE_DIR}")
		file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${_gir_name}_files "${_gir_files}")

		add_custom_command(
			COMMAND ${CMAKE_COMMAND} -E env "CC='${CMAKE_C_COMPILER}'" LDFLAGS=
				${INTROSPECTION_SCANNER_ENV}
				${G_IR_SCANNER}
				${INTROSPECTION_SCANNER_ARGS}
				--namespace=${_gir_namespace}
				--nsversion=${_gir_version}
				${_gir_libtool}
				${_gir_program}
				${_gir_libraries}
				${_gir_packages}
				${_gir_includes}
				${${_gir_name}_SCANNERFLAGS}
				${${_gir_name}_CFLAGS}
				--filelist=${CMAKE_CURRENT_BINARY_DIR}/${_gir_name}_files
				--output ${CMAKE_CURRENT_BINARY_DIR}/${gir}
				--accept-unprefixed
				--sources-top-dirs=${CMAKE_SOURCE_DIR}
				--sources-top-dirs=${CMAKE_BINARY_DIR}
			DEPENDS ${${${_gir_name}_FILES}}
				${${_gir_name}_LIBS}
				${${_gir_name}_DEPS}
			OUTPUT ${gir}
			WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
			VERBATIM
		)
		list(APPEND _gir_girs ${CMAKE_CURRENT_BINARY_DIR}/${gir})
		install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${gir} DESTINATION ${SHARE_INSTALL_PREFIX}/gir-1.0)
		add_custom_target(gir-girs-${_gir_name} ALL DEPENDS ${_gir_girs})

		if(NOT DEFINED ${_gir_name}_SKIP_TYPELIB OR NOT ${${_gir_name}_SKIP_TYPELIB})
			string(REPLACE ".gir" ".typelib" _typelib "${gir}")
			add_custom_command(
				COMMAND ${G_IR_COMPILER}
					${INTROSPECTION_COMPILER_ARGS}
					--includedir=${CMAKE_CURRENT_SOURCE_DIR}
					--includedir=${SHARE_INSTALL_PREFIX}/gir-1.0
					${CMAKE_CURRENT_BINARY_DIR}/${gir}
					-o ${CMAKE_CURRENT_BINARY_DIR}/${_typelib}
				DEPENDS gir-girs-${_gir_name}
				OUTPUT ${_typelib}
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
			)
			list(APPEND _gir_typelibs ${CMAKE_CURRENT_BINARY_DIR}/${_typelib})
			install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_typelib} DESTINATION ${LIB_INSTALL_DIR}/girepository-1.0)
			add_custom_target(gir-typelibs-${_gir_name} ALL DEPENDS ${_gir_typelibs})
		endif(NOT DEFINED ${_gir_name}_SKIP_TYPELIB OR NOT ${${_gir_name}_SKIP_TYPELIB})
	endif(ENABLE_INTROSPECTION)
endmacro(gir_add_introspection)

macro(_gir_deps_to_cmake_targets _outvar _inlist)
	set(${_outvar})
	foreach(_item IN LISTS ${_inlist})
		get_filename_component(_filename "${_item}" NAME)
		string(REPLACE "-" "_" _filename "${_filename}")
		string(REPLACE "." "_" _filename "${_filename}")
		list(APPEND ${_outvar} gir-girs-${_filename})
	endforeach()
endmacro(_gir_deps_to_cmake_targets)

macro(_gir_deps_to_includedir _outvar _inlist)
	set(${_outvar})
	foreach(_item IN LISTS ${_inlist})
		get_filename_component(_directory "${_item}" DIRECTORY)
		list(APPEND ${_outvar} "--includedir=${_directory}")
	endforeach()
endmacro(_gir_deps_to_includedir)

macro(gir_add_introspection_simple gir_library pkg_export_prefix gir_library_version c_include gir_identifies_prefixes_var gir_includes_var extra_cflags_var gir_extra_libdirs_var gir_libs_var gir_deps_var gir_sources_var)
	gir_construct_names(${gir_library} ${gir_library_version} gir_name gir_vars_prefix)

	cmake_parse_arguments(gir "" "" "SCANNER_EXTRA_ARGS" ${ARGN})
	list(APPEND gir_SCANNER_EXTRA_ARGS "--warn-all")

	unset(INTROSPECTION_SCANNER_ARGS)
	unset(INTROSPECTION_SCANNER_ENV)
	unset(INTROSPECTION_COMPILER_ARGS)

	set(${gir_vars_prefix} ${gir_library})
	set(${gir_vars_prefix}_SCANNERFLAGS ${gir_SCANNER_EXTRA_ARGS})
	set(${gir_vars_prefix}_VERSION "${gir_library_version}")
	set(${gir_vars_prefix}_LIBRARY "${gir_vars_prefix}")
	set(${gir_vars_prefix}_INCLUDES ${${gir_includes_var}})
	set(${gir_vars_prefix}_CFLAGS
		-I${CMAKE_CURRENT_BINARY_DIR}
		-I${CMAKE_BINARY_DIR}
		-I${CMAKE_BINARY_DIR}/src
		-I${CMAKE_CURRENT_SOURCE_DIR}
		-I${CMAKE_SOURCE_DIR}
		-I${CMAKE_SOURCE_DIR}/src
		${${extra_cflags_var}}
	)
	set(${gir_vars_prefix}_LIBS ${${gir_libs_var}})
	set(${gir_vars_prefix}_FILES ${gir_sources_var})

	_gir_deps_to_includedir(INTROSPECTION_COMPILER_ARGS ${gir_deps_var})
	_gir_deps_to_cmake_targets(${gir_vars_prefix}_DEPS ${gir_deps_var})

	_gir_list_prefix(_gir_identifies_prefixes ${gir_identifies_prefixes_var} "--identifier-prefix=")
	_gir_list_prefix(_gir_extra_libdirs ${gir_extra_libdirs_var} "--library-path=")
	_gir_list_prefix(_gir_deps ${gir_deps_var} "--include-uninstalled=")

	string(REGEX MATCHALL "-L[^ ]*"
		_extra_library_path "${CMAKE_SHARED_LINKER_FLAGS}")

	set(INTROSPECTION_SCANNER_ARGS
		--add-include-path=${CMAKE_BINARY_DIR}
		--add-include-path=${CMAKE_BINARY_DIR}/src
		--add-include-path=${CMAKE_SOURCE_DIR}
		--add-include-path=${CMAKE_SOURCE_DIR}/src
		--add-include-path=${CMAKE_CURRENT_BINARY_DIR}
		--add-include-path=${CMAKE_CURRENT_SOURCE_DIR}
		--library-path=${CMAKE_BINARY_DIR}
		--library-path=${CMAKE_BINARY_DIR}/src
		--library-path=${CMAKE_CURRENT_BINARY_DIR}
		${_gir_extra_libdirs}
		${_gir_identifies_prefixes}
		${_gir_deps}
		--add-include-path=${SHARE_INSTALL_PREFIX}/gir-1.0
		--library-path=${LIB_INSTALL_DIR}
		${_extra_library_path}
		--pkg-export ${pkg_export_prefix}-${gir_library_version}
		--c-include=${c_include}
		--cflags-begin
		${${gir_vars_prefix}_CFLAGS}
		--cflags-end
		--verbose
	)

	if(WIN32)
		set(_loader_library_path_var "PATH")
		set(_loader_library_path_native "$ENV{PATH}")
	elseif(APPLE)
		set(_loader_library_path_var "DYLD_LIBRARY_PATH")
		set(_loader_library_path_native "$ENV{DYLD_LIBRARY_PATH}")
	else()
		set(_loader_library_path_var "LD_LIBRARY_PATH")
		set(_loader_library_path_native "$ENV{LD_LIBRARY_PATH}")
	endif ()

	file(TO_CMAKE_PATH
		"${_loader_library_path_native}"
		_loader_library_path_cmake
	)

	set(_extra_loader_library_path_cmake
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_CURRENT_BINARY_DIR}
		${${gir_extra_libdirs_var}}
		${LIB_INSTALL_DIR}
		${_loader_library_path_cmake}
	)

	file(TO_NATIVE_PATH
		"${_extra_loader_library_path_cmake}"
		_extra_loader_library_path_native
	)

	if(UNIX)
		string(REPLACE ";" ":"
			_extra_loader_library_path_native
			"${_extra_loader_library_path_native}"
		)
		string(REPLACE "\\ " " "
			_extra_loader_library_path_native
			"${_extra_loader_library_path_native}"
		)
	endif(UNIX)

	set(INTROSPECTION_SCANNER_ENV
		${_loader_library_path_var}="${_extra_loader_library_path_native}"
	)

	gir_add_introspection(${gir_name})

endmacro(gir_add_introspection_simple)

macro(gir_filter_out_sources _inout_listvar _contains_str)
	set(_tmp)
	foreach(_item IN LISTS ${_inout_listvar})
		string(FIND "${_item}" "${_contains_str}" _contains)
		if(_contains EQUAL -1)
			list(APPEND _tmp ${_prefix}${_item})
		endif(_contains EQUAL -1)
	endforeach()
	set(${_inout_listvar} ${_tmp})
endmacro(gir_filter_out_sources)

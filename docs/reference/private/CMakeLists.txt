set(SOURCEDIRS
	${CMAKE_BINARY_DIR}/src/private
)

set(DEPENDENCIES
	edbus-private
)

set(IGNORE_HEADERS
	e-dbus-localed.h
)

add_gtkdoc(libedbus-private e-dbus "EDS_DISABLE_DEPRECATED" SOURCEDIRS DEPENDENCIES IGNORE_HEADERS)

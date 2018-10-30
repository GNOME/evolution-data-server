configure_file(${CMAKE_CURRENT_SOURCE_DIR}/evolution-data-server-cursor-example.sgml.in
	${CMAKE_CURRENT_BINARY_DIR}/xml/evolution-data-server-cursor-example.sgml
	@ONLY
)

set(SOURCEDIRS
	${CMAKE_SOURCE_DIR}/src/libebackend
	${CMAKE_SOURCE_DIR}/src/libedataserver
	${CMAKE_BINARY_DIR}/src/libedataserver
	${CMAKE_SOURCE_DIR}/src/addressbook/libebook
	${CMAKE_SOURCE_DIR}/src/addressbook/libebook-contacts
	${CMAKE_SOURCE_DIR}/src/addressbook/libedata-book
	${CMAKE_SOURCE_DIR}/src/calendar/libecal
	${CMAKE_SOURCE_DIR}/src/calendar/libedata-cal
	${CMAKE_SOURCE_DIR}/tests/test-server-utils
)

set(DEPENDENCIES
	ebackend
	edataserver
	ebook
	ebook-contacts
	edata-book
	ecal
	edata-cal
	etestserverutils
	gtkdoc-camel
)

set(IGNORE_HEADERS
	e-alphabet-index-private.h
	e-backend-enumtypes.h
	e-book-enumtypes.h
	e-book-contacts-enumtypes.h
	e-book-view-private.h
	e-cal-enumtypes.h
	e-cal-view-private.h
	e-client-private.h
	e-data-cal-types.h
	e-phone-number-private.h
	e-source-enumtypes.h
	e-transliterator-private.h
	libedataserver-private.h
	ximian-vcard.h
)

add_gtkdoc(evolution-data-server e "EDS_DISABLE_DEPRECATED" SOURCEDIRS DEPENDENCIES IGNORE_HEADERS)

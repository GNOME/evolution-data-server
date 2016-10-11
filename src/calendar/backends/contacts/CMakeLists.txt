set(DEPENDENCIES
	ebackend
	ebook
	ebook-contacts
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-contacts-factory.c
	e-cal-backend-contacts.c
	e-cal-backend-contacts.h
)

add_library(ecalbackendcontacts MODULE
	${SOURCES}
)

add_dependencies(ecalbackendcontacts
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendcontacts PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-contacts\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendcontacts PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendcontacts PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/addressbook
	${CMAKE_SOURCE_DIR}/src/addressbook
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendcontacts
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendcontacts
	DESTINATION ${ecal_backenddir}
)

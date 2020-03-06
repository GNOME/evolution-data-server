set(DEPENDENCIES
	ebackend
	ecal
	edataserver
	edata-cal
)

set(SOURCES
	e-cal-backend-webdav-notes-factory.c
	e-cal-backend-webdav-notes.c
	e-cal-backend-webdav-notes.h
)

add_library(ecalbackendwebdavnotes MODULE
	${SOURCES}
)

add_dependencies(ecalbackendwebdavnotes
	${DEPENDENCIES}
)

target_compile_definitions(ecalbackendwebdavnotes PRIVATE
	-DG_LOG_DOMAIN=\"e-cal-backend-webdav-notes\"
	-DBACKENDDIR=\"${ecal_backenddir}\"
)

target_compile_options(ecalbackendwebdavnotes PUBLIC
	${CALENDAR_CFLAGS}
)

target_include_directories(ecalbackendwebdavnotes PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_BINARY_DIR}/src/calendar
	${CMAKE_SOURCE_DIR}/src/calendar
	${CALENDAR_INCLUDE_DIRS}
)

target_link_libraries(ecalbackendwebdavnotes
	${DEPENDENCIES}
	${CALENDAR_LDFLAGS}
)

install(TARGETS ecalbackendwebdavnotes
	DESTINATION ${ecal_backenddir}
)

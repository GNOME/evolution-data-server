add_subdirectory(db)

set(extra_deps
	ebook
	ebook-contacts
	edbus-private
	client-test-utils
)

set(extra_defines
	-DEDS_TEST_WORK_DIR=\"${CMAKE_BINARY_DIR}/tests/test-server-utils/cache\"
	-DEDS_TEST_SQLITE_BOOKS=\"${CMAKE_SOURCE_DIR}/tests/book-migration/db\"
	-DEDS_TEST_BUILT_BOOKS=\"${CMAKE_BINARY_DIR}/tests/book-migration/db\"
)

# If db_load is detected at configure time, then we've built
# an addressbook.db to test the migration from 3.6 -> Current.
#
# Instead of committing a BDB file directly which might have
# compatibility issues, we use a text dump (for SQLite we
# rely on compatilbility).
if(HAVE_DB_LOAD)
	list(APPEND extra_defines
		-DTEST_VERSIONS_WITH_BDB
	)
endif(HAVE_DB_LOAD)

set(extra_cflags
	${ADDRESSBOOK_CFLAGS}
)

set(extra_incdirs
	${ADDRESSBOOK_INCLUDE_DIRS}
	${CMAKE_BINARY_DIR}/tests/libebook/client
	${CMAKE_SOURCE_DIR}/tests/libebook/client
)

set(extra_ldflags
	${ADDRESSBOOK_LDFLAGS}
)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-migration
)

foreach(_test ${TESTS})
	set(SOURCES ${_test}.c)

	build_only_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
	)
	add_check_test(${_test} --build-dir "${CMAKE_BINARY_DIR}")
	if(HAVE_DB_LOAD)
		add_dependencies(${_test} libdb-addressbooks)
	endif(HAVE_DB_LOAD)
endforeach(_test)

# This is a little cheat, it's not a real test, but can be built with the macro
set(SOURCES setup-migration-test.c)

build_only_installable_test(setup-migration-test
	SOURCES
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

# This rule should be run once every stable release and then the
# newly created 'contacts.db' file added to git.
#
# For instance, when the EDS version is 3.12, the file:
#   $(top_srcdir)/tests/book-migration/db/3.12/contacts.db
# will be created as a result of running 'make setup-migration'.
#
# Note that the 'setup-migration-test' program can be compiled
# with EDS versions back to 3.0. If you really need to rebuild
# the older test sandboxes, then setup-migration-test can be
# manually compiled and used with older builds of EDS.
set(_use_db_version "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
add_custom_target(setup-migration
	COMMAND ${CMAKE_COMMAND} -E echo "Setting up new migration sandbox in ${CMAKE_SOURCE_DIR}/tests/book-migration/db/${_use_db_version}..."
	COMMAND ${CMAKE_BINARY_DIR}/tests/book-migration/setup-migration-test
		--use-test-sandbox --book-id ${_use_db_version}
		--contacts-directory ${CMAKE_SOURCE_DIR}/tests/book-migration/vcards
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/tests/book-migration/db/${_use_db_version}
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tests/test-server-utils/cache/evolution/addressbook/${_use_db_version}/contacts.db
		${CMAKE_SOURCE_DIR}/tests/book-migration/db/${_use_db_version}/
	COMMAND ${CMAKE_COMMAND} -E echo "Done."
	DEPENDS setup-migration-test
)

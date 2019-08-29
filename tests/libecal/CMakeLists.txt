set(extra_deps
	ecal
)

set(extra_defines)

set(extra_cflags
	${CALENDAR_CFLAGS}
)

set(extra_incdirs
	${CALENDAR_INCLUDE_DIRS}
)

set(extra_ldflags
	${CALENDAR_LDFLAGS}
)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-cal-client-refresh
	test-cal-client-add-timezone
	test-cal-client-create-object
	test-cal-client-remove-object
	test-cal-client-get-object-list
	test-cal-client-modify-object
	test-cal-client-send-objects
	test-cal-client-receive-objects
        test-cal-client-bulk-methods
	test-cal-client-get-attachment-uris
	test-cal-client-get-view
	test-cal-client-revision-view
	test-cal-client-get-revision
	test-cal-client-get-free-busy
	test-cal-component
	test-cal-recur
	test-cal-reminders
)

foreach(_test ${TESTS})
	set(SOURCES ${_test}.c)

	add_installable_test(${_test}
		SOURCES
		extra_deps
		extra_defines
		extra_cflags
		extra_incdirs
		extra_ldflags
		"session-exclusive"
		"TEST_INSTALLED_SERVICES=1"
	)
endforeach(_test)

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
	e-source-registry-test
	libedataserver-test
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

set(TESTS_SKIP
	e-collator-test
)

if(HAVE_GTK)
	list(APPEND TESTS_SKIP
		e-user-prompter-test
	)
endif(HAVE_GTK)

foreach(_test ${TESTS_SKIP})
	set(SOURCES ${_test}.c)

	build_only_installable_test(${_test}
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

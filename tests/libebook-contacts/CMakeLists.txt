set(extra_deps
	ebook-contacts
)

set(extra_defines)

set(extra_cflags
	${ADDRESSBOOK_CFLAGS}
)

set(extra_incdirs
	${ADDRESSBOOK_INCLUDE_DIRS}
)

set(extra_ldflags
	${ADDRESSBOOK_LDFLAGS}
)

# Should be kept ordered approximately from least to most difficult/complex
set(TESTS
	test-contact-types
	test-vcard-parsing
	test-untyped-phones
	test-type-param
	test-query
	test-phone-number
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

set(TESTS_SKIP
	pgp
#	pgp-mime
#	pkcs7
)

add_camel_tests(smime TESTS_SKIP OFF)

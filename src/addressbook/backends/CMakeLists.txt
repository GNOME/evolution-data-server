add_subdirectory(carddav)
add_subdirectory(file)

if(ENABLE_GOOGLE)
	add_subdirectory(google)
endif(ENABLE_GOOGLE)

if(HAVE_LDAP)
	add_subdirectory(ldap)
endif(HAVE_LDAP)

add_subdirectory(carddav)
add_subdirectory(file)

if(HAVE_LDAP)
	add_subdirectory(ldap)
endif(HAVE_LDAP)

if ENABLE_LDAP
LDAP_SUBDIR = ldap
else
LDAP_SUBDIR =
endif

if ENABLE_MAPI
MAPI_SUBDIR = mapi
else
MAPI_SUBDIR =
endif

if ENABLE_LDAP
LDAP_SUBDIR = galldap
else
LDAP_SUBDIR = 
endif

SUBDIRS = file vcf $(LDAP_SUBDIR) groupwise $(MAPI_SUBDIR)

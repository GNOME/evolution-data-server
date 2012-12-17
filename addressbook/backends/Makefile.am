if ENABLE_LDAP
LDAP_SUBDIR = ldap
else
LDAP_SUBDIR =
endif

if HAVE_GOOGLE
GOOGLE_SUBDIR = google
else
GOOGLE_SUBDIR = 
endif

SUBDIRS = file $(LDAP_SUBDIR) $(GOOGLE_SUBDIR) webdav

-include $(top_srcdir)/git.mk

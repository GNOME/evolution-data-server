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

SUBDIRS = file vcf $(LDAP_SUBDIR) $(GOOGLE_SUBDIR) webdav

# All subdirs go into dist tarballs regardless of configure options
DIST_SUBDIRS = file vcf ldap google webdav

-include $(top_srcdir)/git.mk

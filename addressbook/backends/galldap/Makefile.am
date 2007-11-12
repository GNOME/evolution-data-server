INCLUDES =						\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)			\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/addressbook			\
	$(LDAP_CFLAGS)					\
	-I$(top_srcdir)/servers/exchange/storage	\
	-I$(top_srcdir)/servers/exchange/lib		\	
	$(DB_CFLAGS)

# GConf schemas

schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_in_files = apps_galldap_addressbook.schemas.in.in
schema_DATA = $(schema_in_files:.schemas.in.in=-$(BASE_VERSION).schemas)
%-$(BASE_VERSION).schemas.in: %.schemas.in.in
	cp $< $@

@INTLTOOL_SCHEMAS_RULE@

install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schema_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $$p; \
		done \
	fi

extension_LTLIBRARIES = libebookbackendgalldap.la

libebookbackendgalldap_la_SOURCES =	\
	e-book-backend-galldap-factory.c	\
	e-book-backend-galldap-factory.h	\
	e-book-backend-galldap.c		\
	e-book-backend-galldap.h		


libebookbackendgalldap_la_LIBADD = 	\
	$(DB_LIBS)	\
	-lexchange-storage-1.2		\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la		\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		

libebookbackendgalldap_la_LDFLAGS =	\
	-module -avoid-version $(NO_UNDEFINED)

EXTRA_DIST =				\
	$(schema_in_files)
 
CLEANFILES = \
	apps_galldap_addressbook-$(BASE_VERSION).schemas

INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(LDAP_CFLAGS)					\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_LTLIBRARIES = libebookbackendldap.la

libebookbackendldap_la_SOURCES =			\
	e-book-backend-ldap.c				\
	e-book-backend-ldap.h

libebookbackendldap_la_LIBADD =						\
	$(LDAP_LIBS)							\
	$(top_builddir)/addressbook/libedata-book/libedata-book.la	\
	$(top_builddir)/libedataserver/libedataserver.la

LDAP_SCHEMA =						\
	evolutionperson.schema

ldapschemadir = $(privdatadir)
ldapschema_DATA= $(LDAP_SCHEMA)

EXTRA_DIST = $(LDAP_SCHEMA)
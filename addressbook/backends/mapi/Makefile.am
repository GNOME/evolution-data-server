INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_builddir)/servers/mapi			\
	$(LIBMAPI_CFLAGS)					\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

extension_LTLIBRARIES = libebookbackendmapi.la

libebookbackendmapi_la_SOURCES =			\
	e-book-backend-mapi.c				\
	e-book-backend-mapi.h				\
	e-book-backend-mapi-factory.c

libebookbackendmapi_la_LIBADD =						\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la		\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/servers/mapi/libexchangemapi-1.0.la		\
	$(EVOLUTION_ADDRESSBOOK_LIBS)					\
	$(LIBMAPI_LIBS)

libebookbackendmapi_la_LDFLAGS =	\
	-module -avoid-version $(NO_UNDEFINED)

#LDAP_SCHEMA =						\
#	evolutionperson.schema

#ldapschemadir = $(privdatadir)
#ldapschema_DATA= $(LDAP_SCHEMA)

#EXTRA_DIST = $(LDAP_SCHEMA) openldap-extract.h

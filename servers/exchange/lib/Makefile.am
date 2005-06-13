# Fix the code to not use E_DATA_SERVER_UI_CFLAGS

INCLUDES =                                                      \
	-DCONNECTOR_PREFIX=\""$(prefix)"\"			\
	-DCONNECTOR_LOCALEDIR=\""$(localedir)"\"		\
	$(LDAP_CFLAGS)						\
	$(KRB5_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(E_DATA_SERVER_CFLAGS)					\
	$(E_DATA_SERVER_UI_CFLAGS)				\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/servers/exchange/xntlm

noinst_LTLIBRARIES =						\
	libexchange.la

PROP_GENERATED = e2k-propnames.h e2k-propnames.c e2k-proptags.h

mapi_properties    = mapi-properties
e2k_propnames_h_in = $(srcdir)/e2k-propnames.h.in 
e2k_propnames_c_in = $(srcdir)/e2k-propnames.c.in 
e2k_proptags_h_in  = $(srcdir)/e2k-proptags.h.in 

e2k-propnames.h: $(e2k_propnames_h_in) $(mapi_properties)
	@echo Building $@
	@( awk '/^@AUTOGENERATE@/ {exit;} {print;}' $(e2k_propnames_h_in);					\
	  awk '/^x/ { printf "#define %-39s E2K_NS_MAPI_PROPTAG \"%s\"\n", $$2, $$1; }' $(mapi_properties);	\
	  awk '{if (tail) { print; }} /^@AUTOGENERATE@/ {tail=1;}' $(e2k_propnames_h_in) )			\
	> $@

e2k-propnames.c: $(e2k_propnames_c_in) $(mapi_properties)
	@echo Building $@
	@( awk '/^@AUTOGENERATE@/ {exit;} {print;}' $(e2k_propnames_c_in);					\
	  awk '/^x/ { print "\t{ \"" $$1 "\", \"" $$2 "\" },"; }' $(mapi_properties);				\
	  awk '{if (tail) { print; }} /^@AUTOGENERATE@/ {tail=1;}' $(e2k_propnames_c_in) )			\
	> $@

e2k-proptags.h: $(e2k_proptags_h_in) $(mapi_properties)
	@echo Building $@
	@( awk '/^@AUTOGENERATE@/ {exit;} {print;}' $(e2k_proptags_h_in);					\
	  awk '/^x/ { printf "#define E2K_PROPTAG_%-39s 0%s\n", $$2, $$1; }' $(mapi_properties);	\
	  awk '{if (tail) { print; }} /^@AUTOGENERATE@/ {tail=1;}' $(e2k_proptags_h_in) )			\
	> $@

BUILT_SOURCES = $(PROP_GENERATED)
NODIST_FILES = $(PROP_GENERATED)
CLEANFILES = $(PROP_GENERATED)

MARSHAL_GENERATED = e2k-marshal.c e2k-marshal.h

e2k-marshal.h: e2k-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=e2k_marshal $(srcdir)/e2k-marshal.list --header > e2k-marshal.tmp \
	&& mv e2k-marshal.tmp e2k-marshal.h ) \
	|| ( rm -f e2k-marshal.tmp && exit 1 )

e2k-marshal.c: e2k-marshal.h
	( (echo '#include "e2k-marshal.h"'; @GLIB_GENMARSHAL@ --prefix=e2k_marshal $(srcdir)/e2k-marshal.list --body) > e2k-marshal.tmp \
	&& mv e2k-marshal.tmp e2k-marshal.c ) \
	|| ( rm -f e2k-marshal.tmp && exit 1 )

BUILT_SOURCES += $(MARSHAL_GENERATED)
NODIST_FILES += $(MARSHAL_GENERATED)
CLEANFILES += $(MARSHAL_GENERATED)

if HAVE_KRB5
KERBEROS_FILES = 	\
	e2k-kerberos.c	\
	e2k-kerberos.h
else
KERBEROS_FILES =
endif


libexchange_la_SOURCES =					\
	$(MARSHAL_GENERATED)					\
	e2k-propnames.h						\
	e2k-proptags.h						\
	e2k-action.c						\
	e2k-action.h						\
	e2k-autoconfig.c					\
	e2k-autoconfig.h					\
	e2k-context.c						\
	e2k-context.h						\
	e2k-encoding-utils.c					\
	e2k-encoding-utils.h					\
	e2k-freebusy.c						\
	e2k-freebusy.h						\
	e2k-global-catalog.c					\
	e2k-global-catalog.h					\
	e2k-http-utils.c					\
	e2k-http-utils.h					\
	e2k-operation.c						\
	e2k-operation.h						\
	e2k-path.c						\
	e2k-path.h						\
	e2k-properties.c					\
	e2k-properties.h					\
	e2k-restriction.c					\
	e2k-restriction.h					\
	e2k-result.c						\
	e2k-result.h						\
	e2k-rule.c						\
	e2k-rule.h						\
	e2k-rule-xml.c						\
	e2k-rule-xml.h						\
	e2k-security-descriptor.c				\
	e2k-security-descriptor.h				\
	e2k-sid.c						\
	e2k-sid.h						\
	e2k-types.h						\
	e2k-uri.c						\
	e2k-uri.h						\
	e2k-utils.c						\
	e2k-utils.h						\
	e2k-validate.h						\
	e2k-xml-utils.c						\
	e2k-xml-utils.h						\
	$(KERBEROS_FILES)					\
	mapi.h

libexchangeincludedir = $(privincludedir)/exchange

libexchangeinclude_HEADERS = 					\
	e2k-autoconfig.h					\
	e2k-context.h						\
	e2k-freebusy.h						\
	e2k-global-catalog.h					\
	e2k-http-utils.h					\
	e2k-marshal.h						\
	e2k-operation.h						\
	e2k-properties.h					\
	e2k-propnames.h						\
	e2k-restriction.h					\
	e2k-result.h						\
	e2k-security-descriptor.h				\
	e2k-types.h						\
	e2k-uri.h						\
	e2k-utils.h						\
	e2k-xml-utils.h						\
	mapi.h

EXTRA_DIST =							\
	e2k-marshal.list					\
	$(e2k_propnames_h_in)					\
	$(e2k_propnames_c_in)					\
	$(e2k_proptags_h_in)

dist-hook:
	cd $(distdir); rm -f $(NODIST_FILES)

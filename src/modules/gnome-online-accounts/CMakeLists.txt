set(extra_deps)
set(sources
	module-gnome-online-accounts.c
	e-goa-client.c
	e-goa-client.h
	goaewsclient.c
	goaewsclient.h
)
set(extra_defines)
set(extra_cflags ${GOA_SERVER_CFLAGS})
set(extra_incdirs ${GOA_INCLUDE_DIRS})
set(extra_ldflags ${GOA_LDFLAGS})

add_source_registry_module(module-gnome-online-accounts
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

set(sources
	module-credentials-goa.c
	e-goa-client.c
	e-goa-client.h
	e-goa-password-based.c
	e-goa-password-based.h
)

add_credentials_module(module-credentials-goa
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

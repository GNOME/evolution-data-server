set(extra_deps)
if(HAVE_LIBGDATA)
	set(sources
		module-google-backend.c
	)
else(HAVE_LIBGDATA)
	set(sources
		module-google-backend.c
	)
endif(HAVE_LIBGDATA)
set(extra_defines)
set(extra_cflags ${LIBGDATA_CFLAGS})
set(extra_incdirs ${LIBGDATA_INCLUDE_DIRS})
set(extra_ldflags ${LIBGDATA_LDFLAGS})

add_source_registry_module(module-google-backend
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

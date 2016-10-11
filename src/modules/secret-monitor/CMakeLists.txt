set(extra_deps)
set(sources
	module-secret-monitor.c
)
set(extra_defines)
set(extra_cflags ${LIBSECRET_CFLAGS})
set(extra_incdirs ${LIBSECRET_INCLUDE_DIRS})
set(extra_ldflags ${LIBSECRET_LDFLAGS})

add_source_registry_module(module-secret-monitor
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

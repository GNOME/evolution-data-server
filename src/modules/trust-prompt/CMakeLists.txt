set(TRUST_PROMPT_SOURCES
	trust-prompt-gtk.c
)

set(TRUST_PROMPT_CFLAGS
	${GNOME_PLATFORM_CFLAGS}
	${GTK_CFLAGS}
	${GCR_CFLAGS}
)

set(TRUST_PROMPT_INCLUDE_DIRS
	${GNOME_PLATFORM_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
	${GCR_INCLUDE_DIRS}
)

set(TRUST_PROMPT_LDFLAGS
	${GNOME_PLATFORM_LDFLAGS}
	${GTK_LDFLAGS}
	${GCR_LDFLAGS}
)

set(extra_deps)
set(sources
	module-trust-prompt.c
	trust-prompt.h
	${TRUST_PROMPT_SOURCES}
)
set(extra_defines -DE_DATA_SERVER_ICONDIR=\"${icondir}\")
set(extra_cflags ${TRUST_PROMPT_CFLAGS})
set(extra_incdirs ${TRUST_PROMPT_INCLUDE_DIRS})
set(extra_ldflags ${TRUST_PROMPT_LDFLAGS})

add_source_registry_module(module-trust-prompt
	sources
	extra_deps
	extra_defines
	extra_cflags
	extra_incdirs
	extra_ldflags
)

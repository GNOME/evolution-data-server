
#include "camel-test-provider.h"
#include "camel-test.h"
#include "camel-exception.h"

#include "camel-provider.h"

void camel_test_provider_init(int argc, char **argv)
{
	char *name, *path;
	int i;
	CamelException ex = { 0 };

	for (i=0;i<argc;i++) {
		name = g_strdup_printf("libcamel%s.so", argv[i]);
		path = g_build_filename(CAMEL_BUILD_DIR, "providers", argv[i], ".libs", name, NULL);
		g_free(name);
		camel_provider_load(path, &ex);
		check_msg(!ex.id, "Cannot load provider for '%s', test aborted", argv[i]);
		g_free(path);
	}
}

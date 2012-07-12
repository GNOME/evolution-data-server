#include "camel-test-provider.h"
#include "camel-test.h"

void
camel_test_provider_init (gint argc,
                          const gchar **argv)
{
	gchar *name, *path;
	gint i;
	GError *error = NULL;

	for (i = 0; i < argc; i++) {
		name = g_strdup_printf ("libcamel%s."G_MODULE_SUFFIX, argv[i]);
		path = g_build_filename (CAMEL_BUILD_DIR, "providers", argv[i], ".libs", name, NULL);
		g_free (name);
		camel_provider_load (path, &error);
		check_msg (error == NULL, "Cannot load provider for '%s', test aborted", argv[i]);
		g_free (path);
	}
}

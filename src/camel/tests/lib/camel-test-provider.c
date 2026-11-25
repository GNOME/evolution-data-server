/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "camel-test-provider.h"
#include "camel-test.h"

void
camel_test_provider_init (gint argc,
                          const gchar **argv)
{
	const gchar *provider_dir;
	gchar *name, *path;
	gboolean installed;
	gint i;
	GError *error = NULL;

	installed = g_getenv ("TEST_INSTALLED_SERVICES") != NULL;
	provider_dir = g_getenv (EDS_CAMEL_PROVIDER_DIR);
	if (!provider_dir)
		provider_dir = CAMEL_PROVIDERDIR;

	for (i = 0; i < argc; i++) {
		name = g_strdup_printf ("libcamel%s."G_MODULE_SUFFIX, argv[i]);
		if (installed)
			path = g_build_filename (provider_dir, name, NULL);
		else
			path = g_test_build_filename (G_TEST_BUILT, "..", "providers", argv[i], name, NULL);
		g_free (name);
		camel_provider_load (path, &error);
		g_assert_no_error (error);
		g_free (path);
	}
}

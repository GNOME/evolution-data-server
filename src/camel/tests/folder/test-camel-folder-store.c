/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * test-folder-store.c -- basic store ops with local providers
 */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "session.h"

static const gchar *local_drivers[] = { "local" };

static void
test_folder_basic_provider (gconstpointer user_data)
{
	const gchar *provider = user_data;
	CamelSession *session;
	gchar *path;

	camel_test_init ();
	camel_test_provider_init (1, local_drivers);

	session = camel_test_session_new (camel_test_get_dir ());

	path = g_strdup_printf ("%s:///%s/%s", provider, camel_test_get_dir (), provider);
	test_folder_basic (session, path, TRUE, FALSE);
	g_free (path);

	g_clear_object (&session);
	camel_test_shutdown ();
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_data_func ("/Camel/Folder/Basic/mbox", "mbox", test_folder_basic_provider);
	g_test_add_data_func ("/Camel/Folder/Basic/mh", "mh", test_folder_basic_provider);
	g_test_add_data_func ("/Camel/Folder/Basic/maildir", "maildir", test_folder_basic_provider);

	return g_test_run ();
}

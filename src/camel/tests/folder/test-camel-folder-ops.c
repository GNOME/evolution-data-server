/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * test-camel-folder-ops.c -- folder message ops with local providers
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "messages.h"
#include "folders.h"
#include "session.h"

static const gchar *local_drivers[] = { "local" };

static void
test_folder_ops_provider (gconstpointer user_data)
{
	const gchar *provider = user_data;
	CamelSession *session;
	gchar *path;

	camel_test_init ();
	camel_test_provider_init (1, local_drivers);

	session = camel_test_session_new (camel_test_get_dir ());

	path = g_strdup_printf ("%s:///%s/%s", provider, camel_test_get_dir (), provider);
	test_folder_message_ops (session, path, TRUE, "testbox");
	g_free (path);

	g_assert_cmpuint (G_OBJECT (session)->ref_count, ==, 1);
	g_clear_object (&session);
	camel_test_shutdown ();
}

static void
test_folder_ops_spool (void)
{
	CamelSession *session;
	gchar *path;

	camel_test_init ();
	camel_test_provider_init (1, local_drivers);

	session = camel_test_session_new (camel_test_get_dir ());

	path = g_strdup_printf ("%s/testbox", camel_test_get_dir ());
	g_warn_if_fail (creat (path, 0600) != -1);
	g_free (path);
	path = g_strdup_printf ("spool:///%s/testbox", camel_test_get_dir ());
	test_folder_message_ops (session, path, TRUE, "INBOX");
	g_free (path);

	g_assert_cmpuint (G_OBJECT (session)->ref_count, ==, 1);
	g_clear_object (&session);
	camel_test_shutdown ();
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_data_func ("/Camel/folder/ops/mbox", "mbox", test_folder_ops_provider);
	g_test_add_data_func ("/Camel/folder/ops/mh", "mh", test_folder_ops_provider);
	g_test_add_data_func ("/Camel/folder/ops/maildir", "maildir", test_folder_ops_provider);
	g_test_add_func ("/Camel/folder/ops/spool", test_folder_ops_spool);

	return g_test_run ();
}

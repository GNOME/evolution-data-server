/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * test-folder-ops-imap.c -- remote IMAP folder ops
 */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "session.h"

static const gchar *imap_drivers[] = { "imap4" };

static const gchar *remote_providers[] = {
	"IMAP_TEST_URL",
};

static void
test_imap_folder_ops (void)
{
	CamelSession *session;
	gint i;
	gchar *path;

	/* clear out any camel-test data */

	session = camel_test_session_new (camel_test_get_dir ());

	for (i = 0; i < G_N_ELEMENTS (remote_providers); i++) {
		path = getenv (remote_providers[i]);

		if (path == NULL) {
			g_test_skip ("Set IMAP_TEST_URL to run this test");
			g_assert_cmpuint (G_OBJECT (session)->ref_count, ==, 1);
			g_clear_object (&session);
			return;
		}

		test_folder_message_ops (session, path, FALSE, "testbox");
		test_folder_message_ops (session, path, FALSE, "INBOX");
	}

	g_assert_cmpuint (G_OBJECT (session)->ref_count, ==, 1);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, imap_drivers);

	g_test_add_func ("/Camel/folder/ops/imap", test_imap_folder_ops);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

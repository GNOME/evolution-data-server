/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * test-folder-store-nntp.c -- remote NNTP store ops
 */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "session.h"

static const gchar *nntp_drivers[] = { "nntp" };

static const gchar *remote_providers[] = {
	"NNTP_TEST_URL",
};

static void
test_nntp_store (void)
{
	CamelSession *session;
	gint i;
	gchar *path;

	/* clear out any camel-test data */

	session = camel_test_session_new (camel_test_get_dir ());

	for (i = 0; i < G_N_ELEMENTS (remote_providers); i++) {
		path = getenv (remote_providers[i]);

		if (path == NULL) {
			g_test_skip ("Set NNTP_TEST_URL to run this test");
			g_clear_object (&session);
			return;
		}

		test_folder_basic (session, path, FALSE, FALSE);
	}

	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();
	camel_test_provider_init (1, nntp_drivers);

	g_test_add_func ("/Camel/folder/store/nntp", test_nntp_store);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

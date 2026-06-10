/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/* threaded folder testing */

#include <string.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "messages.h"
#include "session.h"

#define MAX_LOOP (10000)
#define MAX_THREADS (5)

#define d(x)

static const gchar *local_drivers[] = { "local" };
static const gchar *local_providers[] = {
	"mbox",
	"mh",
	"maildir"
};

static gchar *worker_path;
static CamelSession *worker_session;
static gint worker_testid;

static gpointer
worker (gpointer d)
{
	gint i;
	CamelStore *store;
	CamelService *service;
	CamelFolder *folder;

	for (i = 0; i < MAX_LOOP; i++) {
		gchar *uid;

		uid = g_strdup_printf ("test-uid-%d", i);
		service = camel_session_add_service (
			worker_session, uid, worker_path, CAMEL_PROVIDER_STORE, NULL);
		g_free (uid);

		g_assert_true (CAMEL_IS_STORE (service));
		store = CAMEL_STORE (service);

		folder = camel_store_get_folder_sync (
			store, "testbox",
			CAMEL_STORE_FOLDER_CREATE, NULL, NULL);
		if (worker_testid == 0) {
			g_object_unref (folder);
			g_object_unref (store);
		} else {
			g_object_unref (store);
			g_object_unref (folder);
		}
	}

	return NULL;
}

static void
test_thread_ops (gconstpointer user_data)
{
	gint testid = GPOINTER_TO_INT (user_data) / G_N_ELEMENTS (local_providers);
	gint provider_index = GPOINTER_TO_INT (user_data) % G_N_ELEMENTS (local_providers);
	GThread *threads[MAX_THREADS];
	gint i;
	GError *error = NULL;

	camel_test_provider_init (1, local_drivers);

	worker_session = camel_test_session_new (camel_test_get_dir ());
	worker_testid = testid;

	worker_path = g_strdup_printf ("%s:///%s/%s", local_providers[provider_index], camel_test_get_dir (), local_providers[provider_index]);

	for (i = 0; i < MAX_THREADS; i++) {
		threads[i] = g_thread_try_new (NULL, worker, NULL, &error);
		g_assert_no_error (error);
	}

	for (i = 0; i < MAX_THREADS; i++) {
		if (threads[i])
			g_thread_join (threads[i]);
	}

	g_free (worker_path);

	g_clear_object (&worker_session);
	camel_test_shutdown ();
}

gint
main (gint argc,
      gchar **argv)
{
	gint testid, j;

	camel_test_init (&argc, &argv);

	for (testid = 0; testid < 2; testid++) {
		for (j = 0; j < G_N_ELEMENTS (local_providers); j++) {
			gchar *test_path;

			test_path = g_strdup_printf (
				"/Camel/Folder/ThreadOps/%s/%s",
				testid == 0 ? "stacked" : "unstacked",
				local_providers[j]);
			g_test_add_data_func (
				test_path,
				GINT_TO_POINTER (testid * G_N_ELEMENTS (local_providers) + j),
				test_thread_ops);
			g_free (test_path);
		}
	}

	return g_test_run ();
}

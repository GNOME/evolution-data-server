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

#define MAX_MESSAGES (100)
#define MAX_THREADS (10)

static const gchar *local_drivers[] = { "local" };

static const gchar *local_providers[] = {
	"mbox",
	"mh",
	"maildir"
};

static void
test_add_message (CamelFolder *folder,
                  gint j)
{
	CamelMimeMessage *msg;
	gchar *content;
	gchar *subject;
	GError *error = NULL;

	msg = test_message_create_simple ();
	content = g_strdup_printf ("Test message %08x contents\n\n", j);
	test_message_set_content_simple (
		(CamelMimePart *) msg, 0, "text/plain",
							content, strlen (content));
	g_free (content);
	subject = g_strdup_printf ("Test message %08x subject", j);
	camel_mime_message_set_subject (msg, subject);

	camel_folder_append_message_sync (
		folder, msg, NULL, NULL, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
	g_clear_object (&msg);
}

struct _threadinfo {
	gint id;
	CamelFolder *folder;
};

static gpointer
worker (gpointer d)
{
	struct _threadinfo *info = d;
	gint i, j, id = info->id;
	gchar *sub, *content;
	CamelMimeMessage *msg;
	GPtrArray *res;
	gboolean success;
	GError *error = NULL;

	/* we add a message, search for it, twiddle some flags, delete it */
	/* and flat out */
	for (i = 0; i < MAX_MESSAGES; i++) {
		res = NULL;
		error = NULL;

		test_add_message (info->folder, id + i);

		sub = g_strdup_printf ("(match-all (header-contains \"subject\" \"message %08x subject\"))", id + i);

		success = camel_folder_search_sync (info->folder, sub, &res, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (res);
		g_assert_cmpuint (res->len, ==, 1);

		msg = camel_folder_get_message_sync (
			info->folder, (gchar *) res->pdata[0], NULL, &error);
		g_assert_no_error (error);

		content = g_strdup_printf ("Test message %08x contents\n\n", id + i);
		test_message_compare_content (camel_medium_get_content ((CamelMedium *) msg), content, strlen (content));
		g_free (content);

		j = g_random_int_range (0, 100);
		if (j <= 70) {
			camel_folder_delete_message (info->folder, res->pdata[0]);
		}

		g_clear_pointer (&res, g_ptr_array_unref);
		g_free (sub);

		g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
		g_clear_object (&msg);

		/* about 1-in 100 calls will expunge */
		j = g_random_int_range (0, 200);
		if (j <= 2) {
			camel_folder_expunge_sync (info->folder, NULL, &error);
			g_assert_no_error (error);
		}
	}

	return info;
}

static void
test_threaded_folder (gconstpointer user_data)
{
	gint provider_index = GPOINTER_TO_INT (user_data) / 2;
	gint index = GPOINTER_TO_INT (user_data) % 2;
	CamelSession *session;
	CamelStore *store;
	CamelService *service;
	CamelFolder *folder;
	GThread *threads[MAX_THREADS];
	struct _threadinfo *info;
	GPtrArray *uids;
	gchar *path;
	gchar *uid;
	gint i;
	GError *error = NULL;

	camel_test_init ();
	camel_test_provider_init (1, local_drivers);

	session = camel_test_session_new (camel_test_get_dir ());

	uid = g_strdup_printf ("test-uid-%d", provider_index);
	path = g_strdup_printf ("%s:///%s/%s", local_providers[provider_index], camel_test_get_dir (), local_providers[provider_index]);
	service = camel_session_add_service (
		session, uid, path,
		CAMEL_PROVIDER_STORE, &error);
	g_free (uid);
	g_assert_no_error (error);
	g_assert_true (CAMEL_IS_STORE (service));
	store = CAMEL_STORE (service);
	g_free (path);

	if (index == 0)
		folder = camel_store_get_folder_sync (
			store, "testbox",
			CAMEL_STORE_FOLDER_CREATE,
			NULL, &error);
	else
		folder = camel_store_get_folder_sync (
			store, "testbox",
			CAMEL_STORE_FOLDER_CREATE |
			CAMEL_STORE_FOLDER_BODY_INDEX,
			NULL, &error);
	g_assert_no_error (error);

	for (i = 0; i < MAX_THREADS; i++) {
		info = g_malloc (sizeof (*info));
		info->id = i * MAX_MESSAGES;
		info->folder = folder;

		threads[i] = g_thread_try_new (NULL, worker, info, &error);
		g_assert_no_error (error);
	}

	for (i = 0; i < MAX_THREADS; i++) {
		if (threads[i]) {
			info = g_thread_join (threads[i]);
			g_free (info);
		}
	}

	uids = camel_folder_dup_uids (folder);
	for (i = 0; i < (gint) uids->len; i++) {
		camel_folder_delete_message (folder, uids->pdata[i]);
	}
	g_clear_pointer (&uids, g_ptr_array_unref);

	camel_folder_expunge_sync (folder, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (G_OBJECT (folder)->ref_count, ==, 1);
	g_clear_object (&folder);

	camel_store_delete_folder_sync (
		store, "testbox", NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (G_OBJECT (store)->ref_count, ==, 1);
	g_clear_object (&store);

	g_clear_object (&session);
	camel_test_shutdown ();
}

gint
main (gint argc,
      gchar **argv)
{
	gint j, index;

	g_test_init (&argc, &argv, NULL);

	for (j = 0; j < G_N_ELEMENTS (local_providers); j++) {
		for (index = 0; index < 2; index++) {
			gchar *test_path;

			test_path = g_strdup_printf (
				"/Camel/Folder/Threaded/%s/%s",
				local_providers[j],
				index ? "indexed" : "nonindexed");
			g_test_add_data_func (
				test_path,
				GINT_TO_POINTER (j * 2 + index),
				test_threaded_folder);
			g_free (test_path);
		}
	}

	return g_test_run ();
}

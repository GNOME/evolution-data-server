/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <locale.h>
#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

#ifdef ENABLE_MAINTAINER_MODE

static void
set_dummy_backend (ESource *scratch,
		   ETestServerClosure *closure)
{
	ESourceBackend *backend_extension;

	g_assert_true (e_source_has_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK));

	backend_extension = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend_extension, "dummy");
}

static void
set_dummy_meta_backend (ESource *scratch,
			ETestServerClosure *closure)
{
	ESourceBackend *backend_extension;

	g_assert_true (e_source_has_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK));

	backend_extension = e_source_get_extension (scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend_extension, "dummy-meta");
}

static ETestServerClosure book_closure_dummy = { E_TEST_SERVER_ADDRESS_BOOK, set_dummy_backend, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_dummy = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, set_dummy_backend, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_dummy_meta = { E_TEST_SERVER_ADDRESS_BOOK, set_dummy_meta_backend, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_dummy_meta = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, set_dummy_meta_backend, 0, FALSE, NULL, FALSE };

#endif /* ENABLE_MAINTAINER_MODE */

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };
static ETestServerClosure book_closure_direct_sync = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_direct_async = { E_TEST_SERVER_DIRECT_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

typedef struct _ManualQueryData {
	GMainLoop *loop;
	EBookClientView *view;
	guint stamp;
	guint stamp_n_total;
	guint stamp_indices;
	guint stamp_content_changed;
	guint n_total;
	guint n_content_changed;
	gboolean waiting_complete;
	gboolean waiting_n_total;
	gboolean waiting_indices;
	gboolean waiting_content_changed;
} ManualQueryData;

typedef struct _ManualQueryWaitData {
	guint codeline;
	GMainLoop *loop;
	gboolean abort_on_timeout;
} ManualQueryWaitData;

static gboolean
manual_query_timeout (gpointer user_data)
{
	ManualQueryWaitData *data = user_data;

	if (data->abort_on_timeout) {
		g_error ("Call from line %u timed out", data->codeline);
		g_assert_not_reached ();
	}

	g_main_loop_quit (data->loop);

	return FALSE;
}

static void
manual_query_wait (guint codeline,
		   GMainLoop *loop,
		   guint interval,
		   gboolean abort_on_timeout)
{
	ManualQueryWaitData data;
	GSource *gsource;

	data.codeline = codeline;
	data.loop = loop;
	data.abort_on_timeout = abort_on_timeout;

	gsource = g_timeout_source_new_seconds (interval);
	g_source_set_callback (gsource, manual_query_timeout, &data, NULL);
	g_source_attach (gsource, g_main_loop_get_context (loop));

	g_main_loop_run (loop);

	g_source_destroy (gsource);
	g_source_unref (gsource);
}

static void
manual_query_flush_main_context (ManualQueryData *mqd)
{
	GMainContext *main_context = g_main_loop_get_context (mqd->loop);

	while (g_main_context_pending (main_context)) {
		g_main_context_iteration (main_context, FALSE);
	}
}

static void
manual_query_wait_complete (ManualQueryData *mqd)
{
	mqd->waiting_complete = TRUE;
	mqd->waiting_n_total = FALSE;
	mqd->waiting_indices = FALSE;
	mqd->waiting_content_changed = FALSE;

	manual_query_wait (__LINE__, mqd->loop, 3, TRUE);

	mqd->waiting_complete = FALSE;
}

static void
manual_query_wait_n_total (guint codeline,
			   ManualQueryData *mqd,
			   guint expected_n_total)
{
	manual_query_flush_main_context (mqd);

	if (mqd->stamp_n_total != mqd->stamp) {
		mqd->waiting_complete = FALSE;
		mqd->waiting_n_total = TRUE;
		mqd->waiting_indices = FALSE;
		mqd->waiting_content_changed = FALSE;

		manual_query_wait (codeline, mqd->loop, 3, TRUE);

		mqd->waiting_n_total = FALSE;
	}

	g_assert_cmpint (expected_n_total, ==, mqd->n_total);
}

static void
manual_query_wait_indices (guint codeline,
			   ManualQueryData *mqd,
			   const gchar *expected_str,
			   guint expected_index,
			   ...) G_GNUC_NULL_TERMINATED;

static void
manual_query_wait_indices (guint codeline,
			   ManualQueryData *mqd,
			   const gchar *expected_str,
			   guint expected_index,
			   ...)
{
	EBookIndices *indices;
	GHashTable *hash;
	guint ii;
	va_list va;

	manual_query_flush_main_context (mqd);

	if (mqd->stamp_indices != mqd->stamp) {
		mqd->waiting_complete = FALSE;
		mqd->waiting_n_total = FALSE;
		mqd->waiting_indices = TRUE;
		mqd->waiting_content_changed = FALSE;

		manual_query_wait (codeline, mqd->loop, 3, TRUE);

		mqd->waiting_indices = FALSE;
	}

	indices = e_book_client_view_dup_indices (mqd->view);
	g_assert_nonnull (indices);

	hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (ii = 0; indices[ii].chr != NULL; ii++) {
		g_hash_table_insert (hash, (gpointer) indices[ii].chr, GUINT_TO_POINTER (indices[ii].index));
	}

	va_start (va, expected_index);

	while (expected_str) {
		guint current_index = GPOINTER_TO_UINT (g_hash_table_lookup (hash, expected_str));

		g_assert_true (g_hash_table_contains (hash, expected_str));
		g_assert_cmpuint (current_index, ==, expected_index);

		expected_str = va_arg (va, const gchar *);
		if (expected_str)
			expected_index = va_arg (va, guint);
	}

	va_end (va);

	e_book_indices_free (indices);
	g_hash_table_destroy (hash);
}

typedef struct _ManualQueryContactsData {
	GMainLoop *loop;
	guint range_start;
	GPtrArray *contacts;
} ManualQueryContactsData;

static void
manual_query_contacts_received_cb (GObject *source_object,
				   GAsyncResult *result,
				   gpointer user_data)
{
	ManualQueryContactsData *mqcd = user_data;
	guint range_start = G_MAXUINT;
	gboolean success;
	GError *error = NULL;

	success = e_book_client_view_dup_contacts_finish (E_BOOK_CLIENT_VIEW (source_object), result, &range_start, &mqcd->contacts, &error);

	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (mqcd->contacts);
	g_assert_cmpuint (range_start, ==, mqcd->range_start);

	g_main_loop_quit (mqcd->loop);
}

static void
manual_query_verify_content_valist (ManualQueryData *mqd,
				    guint range_start,
				    guint range_length,
				    const gchar *expected_uid,
				    va_list va)
{
	ManualQueryContactsData mqcd;
	guint ii;

	mqcd.loop = mqd->loop;
	mqcd.range_start = range_start;
	mqcd.contacts = NULL;

	e_book_client_view_dup_contacts (mqd->view, range_start, range_length, NULL, manual_query_contacts_received_cb, &mqcd);

	g_main_loop_run (mqcd.loop);

	g_assert_nonnull (mqcd.contacts);

	ii = 0;

	while (expected_uid) {
		EContact *contact;
		const gchar *received_uid;

		g_assert_cmpuint (ii, <, mqcd.contacts->len);

		contact = g_ptr_array_index (mqcd.contacts, ii);
		ii++;

		g_assert_nonnull (contact);

		received_uid = e_contact_get_const (contact, E_CONTACT_UID);

		g_assert_cmpstr (expected_uid, ==, received_uid);

		expected_uid = va_arg (va, const gchar *);
	}

	g_ptr_array_unref (mqcd.contacts);
}

static void
manual_query_verify_content (ManualQueryData *mqd,
			     guint range_start,
			     guint range_length,
			     const gchar *expected_uid,
			     ...) G_GNUC_NULL_TERMINATED;

static void
manual_query_verify_content (ManualQueryData *mqd,
			     guint range_start,
			     guint range_length,
			     const gchar *expected_uid,
			     ...)
{
	va_list va;

	va_start (va, expected_uid);
	manual_query_verify_content_valist (mqd, range_start, range_length, expected_uid, va);
	va_end (va);
}

static void
manual_query_wait_content_changed (guint codeline,
				   ManualQueryData *mqd,
				   guint range_start,
				   guint range_length,
				   const gchar *expected_uid,
				   ...) G_GNUC_NULL_TERMINATED;

static void
manual_query_wait_content_changed (guint codeline,
				   ManualQueryData *mqd,
				   guint range_start,
				   guint range_length,
				   const gchar *expected_uid,
				   ...)
{
	va_list va;

	manual_query_flush_main_context (mqd);

	if (mqd->stamp_content_changed != mqd->stamp) {
		mqd->waiting_complete = FALSE;
		mqd->waiting_n_total = FALSE;
		mqd->waiting_indices = FALSE;
		mqd->waiting_content_changed = TRUE;

		manual_query_wait (codeline, mqd->loop, 3, TRUE);

		mqd->waiting_content_changed = FALSE;
	}

	va_start (va, expected_uid);
	manual_query_verify_content_valist (mqd, range_start, range_length, expected_uid, va);
	va_end (va);
}

static void
manual_query_objects_added_cb (EBookClientView *client_view,
			       const GSList *objects,
			       gpointer user_data)
{
	g_assert_not_reached ();
}

static void
manual_query_objects_modified_cb (EBookClientView *client_view,
				  const GSList *objects,
				  gpointer user_data)
{
	g_assert_not_reached ();
}

static void
manual_query_objects_removed_cb (EBookClientView *client_view,
				 const GSList *uids,
				 gpointer user_data)
{
	g_assert_not_reached ();
}

static void
manual_query_progress_cb (EBookClientView *client_view,
			  guint percent,
			  const gchar *message,
			  gpointer user_data)
{
}

static void
manual_query_complete_cb (EBookClientView *client_view,
			  const GError *error,
			  gpointer user_data)
{
	ManualQueryData *mqd = user_data;

	g_assert_no_error (error);

	if (mqd->waiting_complete)
		g_main_loop_quit (mqd->loop);
}

static void
manual_query_content_changed_cb (EBookClientView *client_view,
				 gpointer user_data)
{
	ManualQueryData *mqd = user_data;

	mqd->n_content_changed++;
	mqd->stamp_content_changed = mqd->stamp;

	if (mqd->waiting_content_changed)
		g_main_loop_quit (mqd->loop);
}

static void
manual_query_notify_n_total_cb (EBookClientView *client_view,
				GParamSpec *param,
				gpointer user_data)
{
	ManualQueryData *mqd = user_data;

	mqd->n_total = e_book_client_view_get_n_total (mqd->view);
	mqd->stamp_n_total = mqd->stamp;

	if (mqd->waiting_n_total)
		g_main_loop_quit (mqd->loop);
}

static void
manual_query_notify_indices_cb (EBookClientView *client_view,
				GParamSpec *param,
				gpointer user_data)
{
	ManualQueryData *mqd = user_data;

	mqd->stamp_indices = mqd->stamp;

	if (mqd->waiting_indices)
		g_main_loop_quit (mqd->loop);
}

static void
test_manual_query_view_client_sync (EBookClient *client,
				    GMainLoop *loop)
{
	EBookClientView *view = NULL;
	ManualQueryData mqd = { 0, };
	GError *error = NULL;
	gboolean success;

	success = e_book_client_get_view_sync (client, "(contains \"file_as\" \"X\")", &view, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (view);

	e_book_client_view_set_flags (view, E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY, &error);
	g_assert_no_error (error);

	mqd.loop = loop;
	mqd.view = view;
	mqd.stamp = 0;
	mqd.stamp_n_total = 0;
	mqd.stamp_indices = 0;
	mqd.stamp_content_changed = 0;
	mqd.n_total = 0;
	mqd.n_content_changed = 0;
	mqd.waiting_complete = FALSE;
	mqd.waiting_n_total = FALSE;
	mqd.waiting_indices = FALSE;
	mqd.waiting_content_changed = FALSE;

	/* These should not be called with manual query */
	g_signal_connect (view, "objects-added",
		G_CALLBACK (manual_query_objects_added_cb), NULL);
	g_signal_connect (view, "objects-modified",
		G_CALLBACK (manual_query_objects_modified_cb), NULL);
	g_signal_connect (view, "objects-removed",
		G_CALLBACK (manual_query_objects_removed_cb), NULL);

	/* These can be called */
	g_signal_connect (view, "progress",
		G_CALLBACK (manual_query_progress_cb), NULL);
	g_signal_connect (view, "complete",
		G_CALLBACK (manual_query_complete_cb), &mqd);
	g_signal_connect (view, "content-changed",
		G_CALLBACK (manual_query_content_changed_cb), &mqd);
	g_signal_connect (view, "notify::n-total",
		G_CALLBACK (manual_query_notify_n_total_cb), &mqd);
	g_signal_connect (view, "notify::indices",
		G_CALLBACK (manual_query_notify_indices_cb), &mqd);

	mqd.stamp++;

	/* Start with empty book */
	e_book_client_view_start (view, &error);
	g_assert_no_error (error);

	manual_query_wait_complete (&mqd);

	if (!add_contact_from_test_case_verify (client, "file-as-2", NULL))
		g_error ("Failed to load contact");

	/* Default is order by file-as ascending */
	manual_query_wait_n_total (__LINE__, &mqd, 1);
	manual_query_wait_indices (__LINE__, &mqd, "A", 0, NULL);
	manual_query_wait_content_changed (__LINE__, &mqd, 0, 1, "file-as-2", NULL);

	/* Each of the additions can invoke also other signal handlers, thus increase the stamp after every call */
	mqd.stamp++;

	if (!add_contact_from_test_case_verify (client, "file-as-1", NULL))
		g_error ("Failed to load contact");

	manual_query_flush_main_context (&mqd);
	mqd.stamp++;

	if (!add_contact_from_test_case_verify (client, "file-as-3", NULL))
		g_error ("Failed to load contact");

	manual_query_flush_main_context (&mqd);
	mqd.stamp++;

	if (!add_contact_from_test_case_verify (client, "file-as-4", NULL))
		g_error ("Failed to load contact");

	manual_query_flush_main_context (&mqd);
	mqd.stamp++;

	if (!add_contact_from_test_case_verify (client, "file-as-5", NULL))
		g_error ("Failed to load contact");

	mqd.stamp++;

	manual_query_wait_n_total (__LINE__, &mqd, 3);
	manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 1, "Z", 2, NULL);
	manual_query_wait_content_changed (__LINE__, &mqd, 0, 3, "file-as-2", "file-as-5", "file-as-3", NULL);

	mqd.stamp++;

	/* Reverse the sort order */
	{
		EBookClientViewSortFields tmp_fields[] = {
			{ E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_DESCENDING },
			{ E_CONTACT_FIELD_LAST, E_BOOK_CURSOR_SORT_ASCENDING }
		};

		success = e_book_client_view_set_sort_fields_sync (view, tmp_fields, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_indices (__LINE__, &mqd, "A", 2, "L", 1, "Z", 0, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 3, "file-as-3", "file-as-5", "file-as-2", NULL);
		manual_query_verify_content (&mqd, 0, 1, "file-as-3", NULL);
		manual_query_verify_content (&mqd, 1, 999, "file-as-5", "file-as-2", NULL);
	}

	mqd.stamp++;

	/* Return back the sort order */
	{
		EBookClientViewSortFields tmp_fields[] = {
			{ E_CONTACT_FILE_AS, E_BOOK_CURSOR_SORT_ASCENDING },
			{ E_CONTACT_FIELD_LAST, E_BOOK_CURSOR_SORT_ASCENDING }
		};

		success = e_book_client_view_set_sort_fields_sync (view, tmp_fields, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 1, "Z", 2, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 3, "file-as-2", "file-as-5", "file-as-3", NULL);
		manual_query_verify_content (&mqd, 0, 1, "file-as-2", NULL);
		manual_query_verify_content (&mqd, 1, 999, "file-as-5", "file-as-3", NULL);
	}

	mqd.stamp++;

	/* Modify contact, which is not part of the view */
	{
		EContact *contact = NULL;
		guint n_content_changed;

		success = e_book_client_get_contact_sync (client, "file-as-4", &contact, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (contact);

		e_contact_set (contact, E_CONTACT_EMAIL_1, "lost.case@no.where");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		/* Nothing should change, thus nothing to wait for */
		n_content_changed = mqd.n_content_changed;

		manual_query_verify_content (&mqd, 0, G_MAXUINT, "file-as-2", "file-as-5", "file-as-3", NULL);

		/* Make it part of the view */
		e_contact_set (contact, E_CONTACT_FILE_AS, "Lost X Case");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_n_total (__LINE__, &mqd, 4);
		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 1, "Z", 3, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 4, "file-as-2", "file-as-5", "file-as-4", "file-as-3", NULL);
		g_assert_cmpuint (n_content_changed + 1, ==, mqd.n_content_changed);

		mqd.stamp++;

		/* Modify contact with other than sort field */
		e_contact_set (contact, E_CONTACT_EMAIL_1, "xlost@no.where");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_content_changed (__LINE__, &mqd, 0, 4, "file-as-2", "file-as-5", "file-as-4", "file-as-3", NULL);
		g_assert_cmpuint (e_book_client_view_get_n_total (view), ==, 4);

		mqd.stamp++;

		/* Modify contact sort field, move it to the end */
		e_contact_set (contact, E_CONTACT_FILE_AS, "ZZZ X");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 1, "Z", 2, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 4, "file-as-2", "file-as-5", "file-as-3", "file-as-4", NULL);
		g_assert_cmpuint (e_book_client_view_get_n_total (view), ==, 4);

		mqd.stamp++;

		/* Modify contact sort field, move it to the beginning */
		e_contact_set (contact, E_CONTACT_FILE_AS, "AAA X");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 2, "Z", 3, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 4, "file-as-4", "file-as-2", "file-as-5", "file-as-3", NULL);
		g_assert_cmpuint (e_book_client_view_get_n_total (view), ==, 4);

		mqd.stamp++;

		/* Modify contact sort field, remove it from the view */
		e_contact_set (contact, E_CONTACT_FILE_AS, "Lost Case");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_n_total (__LINE__, &mqd, 3);
		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "L", 1, "Z", 2, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 3, "file-as-2", "file-as-5", "file-as-3", NULL);

		g_clear_object (&contact);
	}

	mqd.stamp++;

	/* Remove contact not being part of the view */
	{
		guint n_content_changed;

		success = e_book_client_remove_contact_by_uid_sync (client, "file-as-4", E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		/* Nothing to wait for */
		n_content_changed = mqd.n_content_changed;

		/* Remove contact which is part of the view */
		success = e_book_client_remove_contact_by_uid_sync (client, "file-as-5", E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_n_total (__LINE__, &mqd, 2);
		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "Z", 1, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 2, "file-as-2", "file-as-3", NULL);
		g_assert_cmpuint (n_content_changed + 1, ==, mqd.n_content_changed);
	}

	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_objects_added_cb), NULL);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_objects_modified_cb), NULL);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_objects_removed_cb), NULL);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_progress_cb), NULL);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_complete_cb), &mqd);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_content_changed_cb), &mqd);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_notify_n_total_cb), &mqd);
	g_signal_handlers_disconnect_by_func (view, G_CALLBACK (manual_query_notify_indices_cb), &mqd);

	g_clear_object (&view);
	mqd.view = NULL;

	/* Try with filled book */
	success = e_book_client_get_view_sync (client, "(contains \"x-evolution-any-field\"  \"\")", &view, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);
	g_assert_nonnull (view);

	e_book_client_view_set_flags (view, E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY, &error);
	g_assert_no_error (error);

	mqd.view = view;

	/* These should not be called with manual query */
	g_signal_connect (view, "objects-added",
		G_CALLBACK (manual_query_objects_added_cb), NULL);
	g_signal_connect (view, "objects-modified",
		G_CALLBACK (manual_query_objects_modified_cb), NULL);
	g_signal_connect (view, "objects-removed",
		G_CALLBACK (manual_query_objects_removed_cb), NULL);

	/* These can be called */
	g_signal_connect (view, "progress",
		G_CALLBACK (manual_query_progress_cb), NULL);
	g_signal_connect (view, "complete",
		G_CALLBACK (manual_query_complete_cb), &mqd);
	g_signal_connect (view, "content-changed",
		G_CALLBACK (manual_query_content_changed_cb), &mqd);
	g_signal_connect (view, "notify::n-total",
		G_CALLBACK (manual_query_notify_n_total_cb), &mqd);
	g_signal_connect (view, "notify::indices",
		G_CALLBACK (manual_query_notify_indices_cb), &mqd);

	mqd.stamp++;

	/* Start with filled book */
	e_book_client_view_start (view, &error);
	g_assert_no_error (error);

	manual_query_wait_complete (&mqd);
	manual_query_wait_n_total (__LINE__, &mqd, 3);
	manual_query_verify_content (&mqd, 0, 3, "file-as-2", "file-as-1", "file-as-3", NULL);

	mqd.stamp++;

	/* Modify existing contact - in a new view */
	{
		EContact *contact = NULL;

		success = e_book_client_get_contact_sync (client, "file-as-1", &contact, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);
		g_assert_nonnull (contact);

		e_contact_set (contact, E_CONTACT_NICKNAME, "Nick");

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_content_changed (__LINE__, &mqd, 0, G_MAXUINT, "file-as-2", "file-as-1", "file-as-3", NULL);
		g_assert_cmpuint (e_book_client_view_get_n_total (view), ==, 3);

		mqd.stamp++;

		success = e_book_client_remove_contact_by_uid_sync (client, "file-as-3", E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_n_total (__LINE__, &mqd, 2);
		manual_query_wait_indices (__LINE__, &mqd, "A", 0, "F", 1, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 2, "file-as-2", "file-as-1", NULL);

		e_contact_set (contact, E_CONTACT_NICKNAME, "Fury");

		mqd.stamp++;

		success = e_book_client_modify_contact_sync (client, contact, E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_content_changed (__LINE__, &mqd, 0, 2, "file-as-2", "file-as-1", NULL);
		g_assert_cmpuint (e_book_client_view_get_n_total (view), ==, 2);

		success = e_book_client_remove_contact_by_uid_sync (client, "file-as-1", E_BOOK_OPERATION_FLAG_NONE, NULL, &error);
		g_assert_no_error (error);
		g_assert_true (success);

		manual_query_wait_n_total (__LINE__, &mqd, 1);
		manual_query_wait_indices (__LINE__, &mqd, "A", 0, NULL);
		manual_query_wait_content_changed (__LINE__, &mqd, 0, 1, "file-as-2", NULL);

		g_clear_object (&contact);
	}

	g_clear_object (&view);
}

static void
test_manual_query_view_sync (ETestServerFixture *fixture,
			     gconstpointer user_data)
{
	EBookClient *main_client;
	EBookClient *client;
	ESource *source;
	const gchar *book_uid = NULL;
	GMainContext *context;
	GMainLoop *loop;
	GError *error = NULL;

	main_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	book_uid = e_source_get_uid (e_client_get_source (E_CLIENT (main_client)));

	context = g_main_context_new ();
	loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* Open the test book client in this thread */
	source = e_source_registry_ref_source (fixture->registry, book_uid);
	if (!source)
		g_error ("Unable to fetch source uid '%s' from the registry", book_uid);

	if (((ETestServerClosure *) user_data)->type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK)
		client = (EBookClient *) e_book_client_connect_direct_sync (fixture->registry, source, (guint32) -1, NULL, &error);
	else
		client = (EBookClient *) e_book_client_connect_sync (source, (guint32) -1, NULL, &error);

	if (!client)
		g_error ("Unable to create EBookClient for uid '%s': %s", book_uid, error->message);

	test_manual_query_view_client_sync (client, loop);

	g_main_context_pop_thread_default (context);

	g_main_context_unref (context);
	g_main_loop_unref (loop);
	g_clear_object (&source);
	g_clear_object (&client);
	g_clear_error (&error);
}

#define N_THREADS  5
#define N_CONTACTS 5

typedef struct {
	ESourceRegistry *registry;
	ETestServerClosure *closure;
	GThread         *thread;
	const gchar     *book_uid;
	EBookClient     *client;
	EBookClientView *view;

	GMainLoop       *loop;

	GMutex           complete_mutex;
	GCond            complete_cond;
	gboolean         complete;
	gint             n_contacts;
} ThreadData;

static void
objects_added (EBookClientView *view,
               const GSList *contacts,
               ThreadData *data)
{
	const GSList *l;

	g_assert_true (g_thread_self () == data->thread);

	for (l = contacts; l; l = l->next) {
		/* print_email (l->data); */

		data->n_contacts++;
	}
}

static void
objects_modified (EBookClientView *view,
                  const GSList *contacts,
                  ThreadData *data)
{
	const GSList *l;

	g_assert_true (g_thread_self () == data->thread);

	for (l = contacts; l; l = l->next) {
		/* print_email (l->data); */
	}
}

static void
objects_removed (EBookClientView *view,
                 const GSList *ids,
                 ThreadData *data)
{
	const GSList *l;

	g_assert_true (g_thread_self () == data->thread);

	for (l = ids; l; l = l->next) {
		/* printf ("   Removed contact: %s\n", (gchar *) l->data); */

		data->n_contacts--;
	}
}

static void
complete (EBookClientView *view,
          const GError *error,
          ThreadData *data)
{
	g_assert_true (g_thread_self () == data->thread);

	g_mutex_lock (&data->complete_mutex);
	data->complete = TRUE;
	g_cond_signal (&data->complete_cond);
	g_mutex_unlock (&data->complete_mutex);
}

static void
finish_thread_test (ThreadData *data)
{
	g_assert_cmpint (data->n_contacts, ==, N_CONTACTS);

	g_main_loop_quit (data->loop);
	g_thread_join (data->thread);
	g_mutex_clear (&data->complete_mutex);
	g_cond_clear (&data->complete_cond);
	g_clear_object (&data->view);
	g_slice_free (ThreadData, data);
}

/************************************
 *     Threads using async API      *
 ************************************/
static void
view_ready (GObject *source_object,
            GAsyncResult *res,
            gpointer user_data)
{
	ThreadData *data = (ThreadData *) user_data;
	GError *error = NULL;

	if (!e_book_client_get_view_finish (E_BOOK_CLIENT (source_object), res, &(data->view), &error))
		g_error ("Getting view failed: %s", error->message);

	g_signal_connect (data->view, "objects-added", G_CALLBACK (objects_added), data);
	g_signal_connect (data->view, "objects-modified", G_CALLBACK (objects_modified), data);
	g_signal_connect (data->view, "objects-removed", G_CALLBACK (objects_removed), data);
	g_signal_connect (data->view, "complete", G_CALLBACK (complete), data);

	e_book_client_view_set_fields_of_interest (data->view, NULL, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);

	e_book_client_view_start (data->view, &error);
	if (error)
		g_error ("start view: %s", error->message);
}

static void
start_thread_test_async (ThreadData *data)
{
	EBookQuery   *query;
	gchar        *sexp;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);

	e_book_client_get_view (data->client, sexp, NULL, view_ready, data);

	e_book_query_unref (query);
	g_free (sexp);
}

static void
connect_ready (GObject *source_object,
               GAsyncResult *res,
               gpointer user_data)
{
	ThreadData *data = (ThreadData *) user_data;
	GError     *error = NULL;

	data->client = (EBookClient *) e_book_client_connect_finish (res, &error);
	if (!data->client)
		g_error ("Error asynchronously connecting to client");

	start_thread_test_async (data);
}

static gpointer
test_view_thread_async (ThreadData *data)
{
	GMainContext    *context;
	ESource         *source;
	GError          *error = NULL;

	context = g_main_context_new ();
	data->loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* Open the test book client in this thread */
	source = e_source_registry_ref_source (data->registry, data->book_uid);
	if (!source)
		g_error ("Unable to fetch source uid '%s' from the registry", data->book_uid);

	if (data->closure->type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK) {
		/* There is no Async API to open a direct book for now, let's stick with the sync API
		 */
		data->client = (EBookClient *) e_book_client_connect_direct_sync (data->registry, source, (guint32) -1, NULL, &error);

		if (!data->client)
			g_error ("Unable to create EBookClient for uid '%s': %s", data->book_uid, error->message);

		/* Fetch the view right away */
		start_thread_test_async (data);

	} else {
		/* Connect asynchronously */
		e_book_client_connect (source, (guint32) -1, NULL, connect_ready, data);
	}

	g_main_loop_run (data->loop);

	g_object_unref (source);

	g_object_unref (data->client);
	g_main_context_pop_thread_default (context);
	g_main_loop_unref (data->loop);
	g_main_context_unref (context);

	return NULL;
}

/************************************
 *     Threads using sync API       *
 ************************************/
static void
start_thread_test_sync (ThreadData *data)
{
	EBookQuery   *query;
	gchar        *sexp;
	GError *error = NULL;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_view_sync (data->client, sexp,
					  &(data->view), NULL, &error))
		g_error ("Error getting view: %s", error->message);

	g_signal_connect (data->view, "objects-added", G_CALLBACK (objects_added), data);
	g_signal_connect (data->view, "objects-modified", G_CALLBACK (objects_modified), data);
	g_signal_connect (data->view, "objects-removed", G_CALLBACK (objects_removed), data);
	g_signal_connect (data->view, "complete", G_CALLBACK (complete), data);

	e_book_client_view_set_fields_of_interest (data->view, NULL, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);

	e_book_client_view_start (data->view, &error);
	if (error)
		g_error ("start view: %s", error->message);

	e_book_query_unref (query);
	g_free (sexp);
}

static gpointer
test_view_thread_sync (ThreadData *data)
{
	GMainContext    *context;
	ESource         *source;
	GError          *error = NULL;

	context = g_main_context_new ();
	data->loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* Open the test book client in this thread */
	source = e_source_registry_ref_source (data->registry, data->book_uid);
	if (!source)
		g_error ("Unable to fetch source uid '%s' from the registry", data->book_uid);

	if (data->closure->type == E_TEST_SERVER_DIRECT_ADDRESS_BOOK)
		data->client = (EBookClient *) e_book_client_connect_direct_sync (data->registry, source, (guint32) -1, NULL, &error);
	else
		data->client = (EBookClient *) e_book_client_connect_sync (source, (guint32) -1, NULL, &error);

	if (!data->client)
		g_error ("Unable to create EBookClient for uid '%s': %s", data->book_uid, error->message);

	start_thread_test_sync (data);

	g_main_loop_run (data->loop);

	g_object_unref (source);

	g_object_unref (data->client);
	g_main_context_pop_thread_default (context);
	g_main_loop_unref (data->loop);
	g_main_context_unref (context);

	return NULL;
}

static ThreadData *
create_test_thread (const gchar *book_uid,
		    ESourceRegistry *registry,
                    gconstpointer user_data,
                    gboolean sync)
{
	ThreadData  *data = g_slice_new0 (ThreadData);

	g_assert_nonnull (registry);

	data->book_uid = book_uid;
	data->registry = registry;
	data->closure = (ETestServerClosure *) user_data;

	g_mutex_init (&data->complete_mutex);
	g_cond_init (&data->complete_cond);

	if (sync)
		data->thread = g_thread_new ("test-thread", (GThreadFunc) test_view_thread_sync, data);
	else
		data->thread = g_thread_new ("test-thread", (GThreadFunc) test_view_thread_async, data);

	return data;
}

static void
test_concurrent_views (ETestServerFixture *fixture,
                       gconstpointer user_data,
                       gboolean sync)
{
	EBookClient *main_client;
	ESource *source;
	const gchar *book_uid = NULL;
	ThreadData **tests;
	gint i;

	main_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);
	source = e_client_get_source (E_CLIENT (main_client));
	book_uid = e_source_get_uid (source);

	/* Create out test contacts */
	if (!add_contact_from_test_case_verify (main_client, "custom-1", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-2", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-3", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-4", NULL) ||
	    !add_contact_from_test_case_verify (main_client, "custom-5", NULL)) {
		g_error ("Failed to create contacts for testing");
	}

	/* Create all concurrent threads accessing the same addressbook */
	tests = g_new0 (ThreadData *, N_THREADS);
	for (i = 0; i < N_THREADS; i++)
		tests[i] = create_test_thread (book_uid, fixture->registry, user_data, sync);

	/* Wait for all threads to receive the complete signal */
	for (i = 0; i < N_THREADS; i++) {
		g_mutex_lock (&(tests[i]->complete_mutex));
		while (!tests[i]->complete)
			g_cond_wait (
				&(tests[i]->complete_cond),
				&(tests[i]->complete_mutex));
		g_mutex_unlock (&(tests[i]->complete_mutex));
	}

	/* Finish all tests */
	for (i = 0; i < N_THREADS; i++)
		finish_thread_test (tests[i]);

	/* Cleanup */
	g_free (tests);
}

static void
test_concurrent_views_sync (ETestServerFixture *fixture,
                            gconstpointer user_data)
{
	test_concurrent_views (fixture, user_data, TRUE);
}

static void
test_concurrent_views_async (ETestServerFixture *fixture,
                             gconstpointer user_data)
{
	test_concurrent_views (fixture, user_data, FALSE);
}

gint
main (gint argc,
      gchar **argv)
{
	setlocale (LC_ALL, "en_US.UTF-8");
	/* if set, overwrite it, thus the backend uses expected locale for the collation */
	g_setenv ("LC_COLLATE", "en_US.UTF-8", TRUE);

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/ConcurrentViews/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_concurrent_views_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/ConcurrentViews/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_concurrent_views_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/ConcurrentViews/Sync",
		ETestServerFixture,
		&book_closure_direct_sync,
		e_test_server_utils_setup,
		test_concurrent_views_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/ConcurrentViews/Async",
		ETestServerFixture,
		&book_closure_direct_async,
		e_test_server_utils_setup,
		test_concurrent_views_async,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/ManualQueryView",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_manual_query_view_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/DirectAccess/ManualQueryView",
		ETestServerFixture,
		&book_closure_direct_sync,
		e_test_server_utils_setup,
		test_manual_query_view_sync,
		e_test_server_utils_teardown);

	#ifdef ENABLE_MAINTAINER_MODE
	if (!g_getenv ("TEST_INSTALLED_SERVICES")) {
		g_test_add (
			"/EBookClient/Dummy/ManualQueryView",
			ETestServerFixture,
			&book_closure_dummy,
			e_test_server_utils_setup,
			test_manual_query_view_sync,
			e_test_server_utils_teardown);
		g_test_add (
			"/EBookClient/DirectAccess/Dummy/ManualQueryView",
			ETestServerFixture,
			&book_closure_direct_dummy,
			e_test_server_utils_setup,
			test_manual_query_view_sync,
			e_test_server_utils_teardown);
		g_test_add (
			"/EBookClient/DummyMeta/ManualQueryView",
			ETestServerFixture,
			&book_closure_dummy_meta,
			e_test_server_utils_setup,
			test_manual_query_view_sync,
			e_test_server_utils_teardown);
		g_test_add (
			"/EBookClient/DirectAccess/DummyMeta/ManualQueryView",
			ETestServerFixture,
			&book_closure_direct_dummy_meta,
			e_test_server_utils_setup,
			test_manual_query_view_sync,
			e_test_server_utils_teardown);
	}
	#endif /* ENABLE_MAINTAINER_MODE */

	return e_test_server_utils_run (argc, argv);
}

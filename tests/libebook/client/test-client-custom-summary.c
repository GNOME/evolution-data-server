/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"


/****************************** Custom Book Creation *****************************/


typedef struct {
	GMainLoop       *loop;
	const gchar     *uid;
	ESourceRegistry *registry;
	ESource         *scratch;
	ESource         *source;
	EBookClient     *book;
} CreateBookData;

static gboolean
quit_idle (CreateBookData *data)
{
	g_main_loop_quit (data->loop);
	return FALSE;
}

static gboolean
create_book_idle (CreateBookData *data)
{
	GError *error = NULL;

	data->source = e_source_registry_ref_source (data->registry, data->uid);
	if (!data->source)
		g_error ("Unable to fetch newly created source uid '%s' from the registry", data->uid);

	data->book = e_book_client_new (data->source, &error);
	if (!data->book)
		g_error ("Unable to create the book: %s", error->message);

	g_idle_add ((GSourceFunc)quit_idle, data);

	return FALSE;
}

static gboolean
register_source_idle (CreateBookData *data)
{
	GError *error = NULL;
	ESourceBackend  *backend;
	ESourceBackendSummarySetup *setup;

	data->registry = e_source_registry_new_sync (NULL, &error);
	if (!data->registry)
		g_error ("Unable to create the registry: %s", error->message);

	data->scratch = e_source_new_with_uid (data->uid, NULL, &error);
	if (!data->scratch)
		g_error ("Failed to create source with uid '%s': %s", data->uid, error->message);

	backend = e_source_get_extension (data->scratch, E_SOURCE_EXTENSION_ADDRESS_BOOK);
	e_source_backend_set_backend_name (backend, "local");

	g_type_ensure (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
	setup = e_source_get_extension (data->scratch, E_SOURCE_EXTENSION_BACKEND_SUMMARY_SETUP);
	e_source_backend_summary_setup_set_summary_fields (setup,
							   E_CONTACT_FULL_NAME,
							   E_CONTACT_FAMILY_NAME,
							   E_CONTACT_EMAIL_1,
							   E_CONTACT_TEL,
							   E_CONTACT_EMAIL,
							   0);
	e_source_backend_summary_setup_set_indexed_fields (setup,
							   E_CONTACT_TEL, E_BOOK_INDEX_SUFFIX,
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FULL_NAME, E_BOOK_INDEX_SUFFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_PREFIX,
							   E_CONTACT_FAMILY_NAME, E_BOOK_INDEX_SUFFIX,
							   0);


	if (!e_source_registry_commit_source_sync (data->registry, data->scratch, NULL, &error))
		g_error ("Unable to add new source to the registry for uid %s: %s", data->uid, error->message);

	/* XXX e_source_registry_commit_source_sync isnt really sync... or else
	 * we could call e_source_registry_ref_source() immediately
	 */
	g_timeout_add (20, (GSourceFunc)create_book_idle, data);

	return FALSE;
}

static EBookClient *
ebook_test_utils_book_with_uid (const gchar *uid)
{
	CreateBookData data = { 0, };

	data.uid = uid;

	data.loop = g_main_loop_new (NULL, FALSE);
	g_idle_add ((GSourceFunc)register_source_idle, &data);
	g_main_loop_run (data.loop);
	g_main_loop_unref (data.loop);

	g_object_unref (data.scratch);
	g_object_unref (data.source);
	g_object_unref (data.registry);

	return data.book;
}

static EBookClient *
new_custom_temp_client (gchar **uri)
{
	EBookClient     *book;
	gchar           *uid;
	guint64          real_time = g_get_real_time ();

	uid  = g_strdup_printf ("test-book-%" G_GINT64_FORMAT, real_time);
	book = ebook_test_utils_book_with_uid (uid);

	if (uri)
		*uri = g_strdup (uid);

	g_free (uid);

	return book;
}

typedef struct {
	EBookClient *client;
	EBookQuery *query;
	gint num_contacts;
} ClientTestData;

static void
client_test_data_free (gpointer p)
{
	ClientTestData *const data = p;
	g_object_unref (data->client);

	if (data->query)
		e_book_query_unref (data->query);
	g_slice_free (ClientTestData, data);
}

static void
search_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_sync (data->client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_test_fail ();
		return;
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_object_slist (results);
	g_free (sexp);
}

static void
uid_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GSList *results = NULL;
	GError *error = NULL;
	gchar *sexp;

	sexp = e_book_query_to_string (data->query);

	if (!e_book_client_get_contacts_uids_sync (data->client, sexp, &results, NULL, &error)) {
		report_error ("get contact uids", &error);
		g_test_fail ();
		return;
	}

	g_assert_cmpint (g_slist_length (results), ==, data->num_contacts);
	e_util_free_string_slist (results);
	g_free (sexp);
}

static void
remove_test (gconstpointer p)
{
	const ClientTestData *const data = p;
	GError *error = NULL;

	if (!e_client_remove_sync (E_CLIENT (data->client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_test_fail ();
		return;
	}
}

static void
add_client_test (const gchar *path,
                 GTestDataFunc func,
                 EBookClient *client,
                 EBookQuery *query,
                 gint num_contacts)
{
	ClientTestData *data = g_slice_new (ClientTestData);

	data->client = g_object_ref (client);
	data->query = query;
	data->num_contacts = num_contacts;

	g_test_add_data_func_full (path, data, func, client_test_data_free);
}

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	EContact *contact_final;
	GError *error = NULL;

	g_test_init (&argc, &argv, NULL);
	main_initialize ();

	/* Setup */
	book_client = new_custom_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* Add contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", &contact_final) ||
	    !add_contact_from_test_case_verify (book_client, "custom-2", &contact_final) ||
	    !add_contact_from_test_case_verify (book_client, "custom-3", &contact_final) ||
	    !add_contact_from_test_case_verify (book_client, "custom-4", &contact_final) ||
	    !add_contact_from_test_case_verify (book_client, "custom-5", &contact_final) ||
	    !add_contact_from_test_case_verify (book_client, "custom-6", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}

	/* Add search tests that fetch contacts */
	add_client_test ("/client/search/exact/fn", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
	                 1);
	add_client_test ("/client/search/exact/name", search_test, book_client,
	                 e_book_query_vcard_field_test(EVC_N, E_BOOK_QUERY_IS, "Janet"),
	                 1);
	add_client_test ("/client/search/prefix/fn", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
	                 2);
	add_client_test ("/client/search/prefix/fn/percent", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "%"),
	                 1);
	add_client_test ("/client/search/suffix/phone", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
	                 2);
	add_client_test ("/client/search/suffix/email", search_test, book_client,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);

	/* Add search tests that fetch uids */
	add_client_test ("/client/search-uid/exact/fn", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown"),
	                 1);
	add_client_test ("/client/search-uid/exact/name", uid_test, book_client,
	                 e_book_query_vcard_field_test(EVC_N, E_BOOK_QUERY_IS, "Janet"),
	                 1);
	add_client_test ("/client/search-uid/prefix/fn", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B"),
	                 2);
	add_client_test ("/client/search-uid/prefix/fn/percent", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "%"),
	                 1);
	add_client_test ("/client/search-uid/suffix/phone", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999"),
	                 2);
	add_client_test ("/client/search-uid/suffix/email", uid_test, book_client,
	                 e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com"),
	                 2);

	/* Test remove operation */
	add_client_test ("/client/remove", remove_test, book_client, NULL, 0);

	/* Roll dices */
	g_object_unref (book_client);

	return g_test_run ();
}

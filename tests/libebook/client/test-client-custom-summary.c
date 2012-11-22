/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"


/* This forces the GType to be registered in a way that
 * avoids a "statement with no effect" compiler warning.
 * FIXME Use g_type_ensure() once we require GLib 2.34. */
#define REGISTER_TYPE(type) \
	(g_type_class_unref (g_type_class_ref (type)))

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

	REGISTER_TYPE (E_TYPE_SOURCE_BACKEND_SUMMARY_SETUP);
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

gint
main (gint argc,
      gchar **argv)
{
	EBookClient *book_client;
	EContact *contact_final;
	GError *error = NULL;
	EBookQuery *query;
	gchar *sexp;
	GSList *results = NULL;

	main_initialize ();

	/*
	 * Setup
	 */
	book_client = new_custom_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	/* Add contacts */
	if (!add_contact_from_test_case_verify (book_client, "custom-1", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-2", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-3", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-4", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}
	if (!add_contact_from_test_case_verify (book_client, "custom-5", &contact_final)) {
		g_object_unref (book_client);
		return 1;
	}

	/* Query exact */
	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_IS, "James Brown");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_assert_cmpint (g_slist_length (results), ==, 1);
	e_util_free_object_slist (results);
	e_book_query_unref (query);
	g_free (sexp);

	/* Query prefix */
	query = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_BEGINS_WITH, "B");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_assert_cmpint (g_slist_length (results), ==, 2);
	e_util_free_object_slist (results);
	e_book_query_unref (query);
	g_free (sexp);

	/* Query phone number suffix */
	query = e_book_query_field_test (E_CONTACT_TEL, E_BOOK_QUERY_ENDS_WITH, "999");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_object_unref (book_client);
		return 1;
	}
	e_util_free_object_slist (results);
	e_book_query_unref (query);
	g_free (sexp);

	/* Query email suffix */
	query = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_ENDS_WITH, "jackson.com");
	sexp = e_book_query_to_string (query);

	if (!e_book_client_get_contacts_sync (book_client, sexp, &results, NULL, &error)) {
		report_error ("get contacts", &error);
		g_object_unref (book_client);
		return 1;
	}
	g_assert_cmpint (g_slist_length (results), ==, 2);
	e_util_free_object_slist (results);
	e_book_query_unref (query);
	g_free (sexp);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return 0;
}

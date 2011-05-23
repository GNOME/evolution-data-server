/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/e-book-client.h>

#include "client-test-utils.h"

static gboolean
check_removed (EBookClient *book_client, const GSList *uids)
{
	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	while (uids) {
		GError *error = NULL;
		EContact *contact = NULL;

		if (!e_book_client_get_contact_sync (book_client, uids->data, &contact, NULL, &error) &&
		    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
			g_clear_error (&error);
		} else {
			report_error ("fail with get contact on removed contact", &error);
			if (contact)
				g_object_unref (contact);
			return FALSE;
		}

		uids = uids->next;
	}

	return TRUE;
}

static gboolean
fill_book_client (EBookClient *book_client, GSList **uids)
{
	EContact *contact;

	g_return_val_if_fail (book_client != NULL, FALSE);
	g_return_val_if_fail (uids != NULL, FALSE);

	*uids = NULL;

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		return FALSE;

	*uids = g_slist_append (*uids, e_contact_get (contact, E_CONTACT_UID));
	g_object_unref (contact);

	if (!add_contact_from_test_case_verify (book_client, "simple-2", &contact))
		return FALSE;

	*uids = g_slist_append (*uids, e_contact_get (contact, E_CONTACT_UID));
	g_object_unref (contact);

	return TRUE;
}

static void
remove_contacts_cb (GObject *source_object, GAsyncResult *result, gpointer uids)
{
	GError *error = NULL;

	if (!e_book_client_remove_contacts_finish (E_BOOK_CLIENT (source_object), result, &error)) {
		report_error ("remove contacts finish", &error);
		stop_main_loop (1);
		return;
	}

	stop_main_loop (check_removed (E_BOOK_CLIENT (source_object), uids) ? 0 : 1);
}

gint
main (gint argc, gchar **argv)
{
	EBookClient *book_client;
	GError *error = NULL;
	GSList *uids;

	main_initialize ();

        /*
         * Setup
         */
	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

        /*
         * Sync version
         */
	if (!fill_book_client (book_client, &uids)) {
		g_object_unref (book_client);
		return 1;
	}

	if (!e_book_client_remove_contacts_sync (book_client, uids, NULL, &error)) {
		report_error ("remove contact sync", &error);
		g_object_unref (book_client);
		g_slist_foreach (uids, (GFunc) g_free, NULL);
		g_slist_free (uids);
		return 1;
	}

	if (!check_removed (book_client, uids)) {
		g_object_unref (book_client);
		g_slist_foreach (uids, (GFunc) g_free, NULL);
		g_slist_free (uids);
		return 1;
	}

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

        /*
         * Async version
         */
	book_client = new_temp_client (NULL);
	g_return_val_if_fail (book_client != NULL, 1);

	if (!e_client_open_sync (E_CLIENT (book_client), FALSE, NULL, &error)) {
		report_error ("client open sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	if (!fill_book_client (book_client, &uids)) {
		g_object_unref (book_client);
		return 1;
	}

	e_book_client_remove_contacts (book_client, uids, NULL, remove_contacts_cb, uids);

	start_main_loop (NULL, NULL);

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);

	if (!e_client_remove_sync (E_CLIENT (book_client), NULL, &error)) {
		report_error ("client remove sync", &error);
		g_object_unref (book_client);
		return 1;
	}

	g_object_unref (book_client);

	return get_main_loop_stop_result ();
}

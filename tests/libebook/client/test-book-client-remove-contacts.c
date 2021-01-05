/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure_sync = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, FALSE };
static ETestServerClosure book_closure_async = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0, FALSE, NULL, TRUE };

static void
check_removed (EBookClient *book_client,
               const GSList *uids)
{
	g_return_if_fail (book_client != NULL);
	g_return_if_fail (uids != NULL);

	while (uids) {
		GError *error = NULL;
		EContact *contact = NULL;

		if (!e_book_client_get_contact_sync (book_client, uids->data, &contact, NULL, &error) &&
		    g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND)) {
			g_clear_error (&error);
		} else
			g_error ("fail with get contact on removed contact: %s", error->message);

		uids = uids->next;
	}
}

static gboolean
fill_book_client (EBookClient *book_client,
                  GSList **uids)
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
test_remove_contacts_sync (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	GSList *uids = NULL;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!fill_book_client (book_client, &uids))
		g_error ("Failed to add contacts");

	if (!e_book_client_remove_contacts_sync (book_client, uids, E_BOOK_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("remove contact sync: %s", error->message);

	/* This will assert they are actually removed */
	check_removed (book_client, uids);
	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

typedef struct {
	GSList *uids;
	GMainLoop *loop;
} RemoveData;

static void
remove_contacts_cb (GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GError *error = NULL;
	RemoveData *data = (RemoveData *) user_data;

	if (!e_book_client_remove_contacts_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("remove contacts finish: %s", error->message);

	check_removed (E_BOOK_CLIENT (source_object), data->uids);
	g_main_loop_quit (data->loop);
}

static void
test_remove_contacts_async (ETestServerFixture *fixture,
                            gconstpointer user_data)
{
	EBookClient *book_client;
	GSList *uids = NULL;
	RemoveData data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!fill_book_client (book_client, &uids))
		g_error ("Failed to add contacts");

	data.uids = uids;
	data.loop = fixture->loop;
	e_book_client_remove_contacts (book_client, uids, E_BOOK_OPERATION_FLAG_NONE, NULL, remove_contacts_cb, &data);

	g_main_loop_run (fixture->loop);

	g_slist_foreach (uids, (GFunc) g_free, NULL);
	g_slist_free (uids);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	client_test_utils_read_args (argc, argv);

	g_test_add (
		"/EBookClient/RemoveContacts/Sync",
		ETestServerFixture,
		&book_closure_sync,
		e_test_server_utils_setup,
		test_remove_contacts_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/RemoveContacts/Async",
		ETestServerFixture,
		&book_closure_async,
		e_test_server_utils_setup,
		test_remove_contacts_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

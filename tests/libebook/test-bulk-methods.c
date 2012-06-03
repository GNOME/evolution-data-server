#include <libebook/libebook.h>

#define BATCH_SIZE 50

static gboolean
check_string_in_slist (GSList *list,
                       const gchar *str)
{
	const GSList *l;

	for (l = list; l != NULL; l = l->next) {
		if (g_strcmp0 ((const gchar *) l->data, str) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean
test_bulk_add_remove (EBookClient *client,
                      const gchar *vcard_str,
                      gint batch_size)
{
	gint i;
	GSList *contacts = NULL;
	GSList *added_uids = NULL;
	GSList *book_uids = NULL;
	EBookQuery *query = NULL;
	gchar *sexp = NULL;
	const GSList *l;

	query = e_book_query_any_field_contains ("");
	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);

	for (i = 0; i < batch_size; ++i) {
		EContact *contact = e_contact_new_from_vcard (vcard_str);
		contacts = g_slist_append (contacts, contact);
	}

	g_print ("  * Bulk addition of %d contacts...\n", batch_size);
	/* Bulk addition */
	g_return_val_if_fail (e_book_client_add_contacts_sync (client, contacts, &added_uids, NULL, NULL), FALSE);
	g_return_val_if_fail (added_uids != NULL, FALSE);
	g_return_val_if_fail (added_uids->data != NULL, FALSE);
	g_return_val_if_fail (g_slist_length (added_uids) == batch_size, FALSE);

	/* Make sure the uids are in the address book */
	g_return_val_if_fail (e_book_client_get_contacts_uids_sync (client, sexp, &book_uids, NULL, NULL), FALSE);
	for (l = added_uids; l != NULL; l = l->next) {
		g_return_val_if_fail (check_string_in_slist (book_uids, (const gchar *) l->data), FALSE);
	}
	g_slist_free_full (book_uids, g_free);

	g_print ("  * Bulk removal of %d contacts...\n", batch_size);
	/* Bulk removal */
	g_return_val_if_fail (e_book_client_remove_contacts_sync (client, added_uids, NULL, NULL), FALSE);

	/* Make sure the uids are no longer in the address book */
	book_uids = NULL;
	g_return_val_if_fail (e_book_client_get_contacts_uids_sync (client, sexp, &book_uids, NULL, NULL), FALSE);
	for (l = added_uids; l != NULL; l = l->next) {
		g_return_val_if_fail (!check_string_in_slist (book_uids, (const gchar *) l->data), FALSE);
	}
	g_slist_free_full (book_uids, g_free);

	g_free (sexp);
	g_slist_free_full (added_uids, g_free);
	g_slist_free_full (contacts, g_object_unref);
	return TRUE;
}

static gboolean
test_bulk_modify (EBookClient *client,
                  const gchar *vcard_str,
                  gint batch_size)
{
	gint i;
	EContact *contact;
	GSList *contacts = NULL;
	GSList *added_uids = NULL;
	const GSList *l, *ll;
	const gchar *old_first_name = "xyz mix";
	const gchar *new_first_name = "abc mix";

	for (i = 0; i < batch_size; ++i) {
		EContact *contact = e_contact_new_from_vcard (vcard_str);
		contacts = g_slist_append (contacts, contact);
	}

	g_print ("  * Bulk addition of %d contacts...\n", batch_size);
	/* Bulk addition */
	g_return_val_if_fail (e_book_client_add_contacts_sync (client, contacts, &added_uids, NULL, NULL), FALSE);
	g_return_val_if_fail (added_uids != NULL, FALSE);
	g_return_val_if_fail (added_uids->data != NULL, FALSE);
	g_return_val_if_fail (g_slist_length (added_uids) == batch_size, FALSE);

	g_print ("  * Bulk modification of %d contacts...\n", batch_size);
	ll = added_uids;
	for (l = contacts; l != NULL; l = l->next) {
		const gchar * uid = ll->data;
		contact = E_CONTACT (l->data);

		/* Set contact UID */
		e_contact_set (contact, E_CONTACT_UID, uid);

		/* Change contact first name */
		e_contact_set (contact, E_CONTACT_GIVEN_NAME, new_first_name);

		ll = ll->next;
	}
	g_return_val_if_fail (e_book_client_modify_contacts_sync (client, contacts, NULL, NULL), FALSE);

	/* Validate */
	for (ll = added_uids; ll != NULL; ll = ll->next) {
		const gchar *first_name;
		const gchar *uid = ll->data;
		contact = NULL;

		g_return_val_if_fail (e_book_client_get_contact_sync (client, uid, &contact, NULL, NULL), FALSE);

		/* Check contact first name */
		first_name = e_contact_get_const (contact, E_CONTACT_GIVEN_NAME);
		g_return_val_if_fail (g_strcmp0 (first_name, new_first_name) == 0, FALSE);

		g_object_unref (contact);
	}

	/* Test failure case */
	g_print ("  * Bulk modification of %d contacts (expected failure)...\n", batch_size);
	contact = E_CONTACT (g_slist_nth_data (contacts, g_slist_length (contacts) - 2));
	g_return_val_if_fail (e_book_client_remove_contact_sync (client, contact, NULL, NULL), FALSE);
	for (l = contacts; l != NULL; l = l->next) {
		contact = E_CONTACT (l->data);
		/* Change contact first name */
		e_contact_set (contact, E_CONTACT_GIVEN_NAME, old_first_name);
	}
	g_return_val_if_fail (!e_book_client_modify_contacts_sync (client, contacts, NULL, NULL), FALSE);

	/* Remove the UID that no longer exists from the added_uid list */
	added_uids = g_slist_delete_link (added_uids, g_slist_nth (added_uids, g_slist_length (added_uids) - 2));

	/* Validate */
	for (ll = added_uids; ll != NULL; ll = ll->next) {
		const gchar *first_name;
		const gchar *uid = ll->data;
		contact = NULL;

		g_return_val_if_fail (e_book_client_get_contact_sync (client, uid, &contact, NULL, NULL), FALSE);

		/* Check contact first name */
		first_name = e_contact_get_const (contact, E_CONTACT_GIVEN_NAME);
		g_return_val_if_fail (g_strcmp0 (first_name, new_first_name) == 0, FALSE);

		g_object_unref (contact);
	}

	g_print ("  * Bulk removal of %d contacts...\n", batch_size);

	/* Bulk removal */
	g_return_val_if_fail (e_book_client_remove_contacts_sync (client, added_uids, NULL, NULL), FALSE);

	g_slist_free_full (added_uids, g_free);
	g_slist_free_full (contacts, g_object_unref);
	return TRUE;
}

gint main (gint argc, gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	EBookClient *client = NULL;
	const gchar
		*test_vcard_str =
			"BEGIN:VCARD\r\n"
			"VERSION:3.0\r\n"
			"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
			"FN:zyx mix\r\n"
			"N:zyx;mix;;;\r\n"
			"END:VCARD";

	g_type_init ();

	/* Create EBook Client */
	client = e_book_client_new_system (NULL);
	g_return_val_if_fail (client != NULL, 1);

	/* Open address book */
	g_return_val_if_fail (e_client_open_sync (E_CLIENT (client), FALSE, NULL, NULL), 1);

	g_print ("Testing bulk addition then removal...\n");
	g_return_val_if_fail (test_bulk_add_remove (client, test_vcard_str, BATCH_SIZE), 1);
	g_print ("Passed.\n");

	g_print ("Testing bulk modification...\n");
	g_return_val_if_fail (test_bulk_modify (client, test_vcard_str, BATCH_SIZE), 1);
	g_print ("Passed.\n");

	g_object_unref (client);
#endif /* ACCOUNT_MGMT */

	return 0;
}

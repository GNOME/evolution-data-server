/* A class to cache address  book conents on local file system
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Devashish Sharma <sdevashish@novell.com>
 */

/**
 * SECTION: e-book-backend-db-cache
 * @include: libedata-book/libedata-book.h
 * @short_description: A Berkeley DB cache facility for addressbooks
 *
 * This API is deprecated, use #EBookSqlite instead.
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <string.h>

#include <db.h>

#include "e-book-backend-db-cache.h"
#include "e-book-backend.h"
#include "e-book-backend-sexp.h"

static void
string_to_dbt (const gchar *str,
               DBT *dbt)
{
	memset (dbt, 0, sizeof (DBT));
	dbt->data = (gpointer) str;
	dbt->size = strlen (str) + 1;
	dbt->flags = DB_DBT_USERMEM;
}

static gchar *
get_filename_from_uri (const gchar *uri)
{
	const gchar *user_cache_dir;
	gchar *mangled_uri, *filename;

	user_cache_dir = e_get_user_cache_dir ();

	/* Mangle the URI to not contain invalid characters. */
	mangled_uri = g_strdelimit (g_strdup (uri), ":/", '_');

	filename = g_build_filename (
		user_cache_dir, "addressbook",
		mangled_uri, "cache.db", NULL);

	g_free (mangled_uri);

	return filename;
}

/**
 * e_book_backend_db_cache_set_filename:
 * @db:  DB Handle
 * @filename: filename to be set
 *
 * Set the filename for db cacahe file.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/

void
e_book_backend_db_cache_set_filename (DB *db,
                                      const gchar *filename)
{
	DBT uid_dbt, vcard_dbt;
	gint db_error;

	string_to_dbt ("filename", &uid_dbt);
	string_to_dbt (filename, &vcard_dbt);

	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}

}

/**
 * e_book_backend_db_cache_get_filename:
 * @db:  DB Handle
 *
 * Get the filename for db cache file.
 *
 * Returns: The filename for db cache file. Free with g_free()
 *   when done with it.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/

gchar *
e_book_backend_db_cache_get_filename (DB *db)
{
	DBT  uid_dbt, vcard_dbt;
	gint db_error;
	gchar *filename;

	string_to_dbt ("filename", &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db-<get failed with %d", db_error);
		return NULL;
	}
	else {
		filename = g_strdup (vcard_dbt.data);
		g_free (vcard_dbt.data);
		return filename;
	}
}

/**
 * e_book_backend_db_cache_get_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Get a cached contact. Note that the returned #EContact will be
 * newly created, and must be unreffed by the caller when no longer
 * needed.
 *
 * Returns: (transfer full) (nullable): A cached #EContact, or %NULL if @uid is not cached.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
EContact *
e_book_backend_db_cache_get_contact (DB *db,
                                     const gchar *uid)
{
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;
	EContact *contact = NULL;

	g_return_val_if_fail (uid != NULL, NULL);

	string_to_dbt (uid, &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt,0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
		return NULL;
	}

	contact = e_contact_new_from_vcard_with_uid ((const gchar *) vcard_dbt.data, uid);
	g_free (vcard_dbt.data);
	return contact;
}

/**
 * e_book_backend_db_cache_add_contact:
 * @db: DB Handle
 * @contact: an #EContact
 *
 * Adds @contact to @cache.
 *
 * Returns: %TRUE if the contact was cached successfully, %FALSE otherwise.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gboolean
e_book_backend_db_cache_add_contact (DB *db,
                                     EContact *contact)
{
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;
	gchar	*vcard_str;
	const gchar *uid;

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (!uid) {
		printf ("no uid\n");
		printf (
			"name:%s, email:%s\n",
			(gchar *) e_contact_get (contact, E_CONTACT_GIVEN_NAME),
			(gchar *) e_contact_get (contact, E_CONTACT_EMAIL_1));
		return FALSE;
	}
	string_to_dbt (uid, &uid_dbt);

	vcard_str = e_vcard_to_string (E_VCARD (contact));
	string_to_dbt (vcard_str, &vcard_dbt);

	/* db_error = db->del (db, NULL, &uid_dbt, 0); */
	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);

	g_free (vcard_str);

	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
		return FALSE;
	}
	else
		return TRUE;
}

/**
 * e_book_backend_db_cache_remove_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Removes the contact identified by @uid from @cache.
 *
 * Returns: %TRUE if the contact was found and removed, %FALSE otherwise.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gboolean
e_book_backend_db_cache_remove_contact (DB *db,
                                        const gchar *uid)

{
	DBT	uid_dbt;
	gint	db_error;

	g_return_val_if_fail (uid != NULL, FALSE);

	string_to_dbt (uid, &uid_dbt);
	db_error = db->del (db, NULL, &uid_dbt, 0);

	if (db_error != 0) {
		g_warning ("db->del failed with %d", db_error);
		return FALSE;
	}
	else
		return TRUE;

}

/**
 * e_book_backend_db_cache_check_contact:
 * @db: DB Handle
 * @uid: a unique contact ID
 *
 * Checks if the contact identified by @uid exists in @cache.
 *
 * Returns: %TRUE if the cache contains the contact, %FALSE otherwise.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gboolean
e_book_backend_db_cache_check_contact (DB *db,
                                       const gchar *uid)
{
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;

	g_return_val_if_fail (uid != NULL, FALSE);

	string_to_dbt (uid, &uid_dbt);
	memset (&vcard_dbt, 0 , sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt,0);
	if (db_error != 0)
		return FALSE;
	else {
		free (vcard_dbt.data);
		return TRUE;
	}
}

/**
 * e_book_backend_db_cache_get_contacts:
 * @db: DB Handle
 * @query: an s-expression
 *
 * Returns a list of #EContact elements from @cache matching @query.
 * When done with the list, the caller must unref the contacts and
 * free the list.
 *
 * Returns: (element-type EContact) (transfer full): A #GList of pointers to #EContact.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
GList *
e_book_backend_db_cache_get_contacts (DB *db,
                                      const gchar *query)
{
	DBC	*dbc;
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;
	GList *list = NULL;
	EBookBackendSExp *sexp = NULL;
	EContact *contact;

	if (query) {
		sexp = e_book_backend_sexp_new (query);
		if (!sexp)
			return NULL;
	}

	db_error = db->cursor (db, NULL, &dbc, 0);
	if (db_error != 0) {
		g_warning ("db->cursor failed with %d", db_error);
		if (sexp)
			g_object_unref (sexp);
		return NULL;
	}

	memset (&vcard_dbt, 0 , sizeof (vcard_dbt));
	memset (&uid_dbt, 0, sizeof (uid_dbt));
	db_error = dbc->c_get (dbc, &uid_dbt, &vcard_dbt, DB_FIRST);

	while (db_error == 0) {
		if (vcard_dbt.data && !strncmp (vcard_dbt.data, "BEGIN:VCARD", 11)) {
			contact = e_contact_new_from_vcard (vcard_dbt.data);

			if (!sexp || e_book_backend_sexp_match_contact (sexp, contact))
				list = g_list_prepend (list, contact);
			else
				g_object_unref (contact);
		}
		db_error = dbc->c_get (dbc, &uid_dbt, &vcard_dbt, DB_NEXT);
	}

	db_error = dbc->c_close (dbc);
	if (db_error != 0)
		g_warning ("db->c_close failed with %d", db_error);

	if (sexp)
		g_object_unref (sexp);

	return g_list_reverse (list);
}

/**
 * e_book_backend_db_cache_search:
 * @db: DB handle
 * @query: an s-expression
 *
 * Returns an array of pointers to unique contact ID strings for contacts
 * in @cache matching @query. When done with the array, the caller must
 * free the ID strings and the array.
 *
 * Returns: (element-type utf8) (transfer full): A #GPtrArray of pointers to contact ID strings.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
GPtrArray *
e_book_backend_db_cache_search (DB *db,
                                const gchar *query)
{
	GList *matching_contacts, *temp;
	GPtrArray *ptr_array;

	matching_contacts = e_book_backend_db_cache_get_contacts (db, query);
	ptr_array = g_ptr_array_new ();

	temp = matching_contacts;
	for (; matching_contacts != NULL; matching_contacts = g_list_next (matching_contacts)) {
		g_ptr_array_add (ptr_array, e_contact_get (matching_contacts->data, E_CONTACT_UID));
		g_object_unref (matching_contacts->data);
	}
	g_list_free (temp);

	return ptr_array;
}

/**
 * e_book_backend_db_cache_exists:
 * @uri: URI for the cache
 *
 * Checks if an #EBookBackendCache exists at @uri.
 *
 * Returns: %TRUE if cache exists, %FALSE if not.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gboolean
e_book_backend_db_cache_exists (const gchar *uri)
{
	gchar *file_name;
	gboolean exists = FALSE;
	file_name = get_filename_from_uri (uri);

	if (file_name && g_file_test (file_name, G_FILE_TEST_EXISTS))
		exists = TRUE;

	g_free (file_name);

	return exists;
}

/**
 * e_book_backend_db_cache_set_populated:
 * @db: DB handle
 *
 * Flags @cache as being populated - that is, it is up-to-date on the
 * contents of the book it's caching.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
void
e_book_backend_db_cache_set_populated (DB *db)
{
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;

	string_to_dbt ("populated", &uid_dbt);
	string_to_dbt ("TRUE", &vcard_dbt);
	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}

}

/**
 * e_book_backend_db_cache_is_populated:
 * @db: DB Handle
 *
 * Checks if @cache is populated.
 *
 * Returns: %TRUE if @cache is populated, %FALSE otherwise.
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gboolean
e_book_backend_db_cache_is_populated (DB *db)
{
	DBT	uid_dbt, vcard_dbt;
	gint	db_error;

	string_to_dbt ("populated", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		return FALSE;
	}
	else {
		free (vcard_dbt.data);
		return TRUE;
	}
}

/**
 * e_book_backend_db_cache_set_time:
 * @db: A Berkeley DB handle
 * @t: The time in string format
 *
 * Since: 2.26
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
void
e_book_backend_db_cache_set_time (DB *db,
                                  const gchar *t)
{
	DBT uid_dbt, vcard_dbt;
	gint db_error;

	string_to_dbt ("last_update_time", &uid_dbt);
	string_to_dbt (t, &vcard_dbt);

	db_error = db->put (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->put failed with %d", db_error);
	}
}

/**
 * e_book_backend_db_cache_get_time:
 * @db: A Berkeley DB handle
 *
 * Since: 2.26
 *
 * Deprecated: 3.12: Use #EBookSqlite instead
 **/
gchar *
e_book_backend_db_cache_get_time (DB *db)
{
	DBT uid_dbt, vcard_dbt;
	gint db_error;
	gchar *t = NULL;

	string_to_dbt ("last_update_time", &uid_dbt);
	memset (&vcard_dbt, 0, sizeof (vcard_dbt));
	vcard_dbt.flags = DB_DBT_MALLOC;

	db_error = db->get (db, NULL, &uid_dbt, &vcard_dbt, 0);
	if (db_error != 0) {
		g_warning ("db->get failed with %d", db_error);
	} else {
		t = g_strdup (vcard_dbt.data);
		free (vcard_dbt.data);
	}

	return t;
}

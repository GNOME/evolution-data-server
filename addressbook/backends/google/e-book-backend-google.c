/* e-book-backend-google.c - Google contact backendy.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010 Philip Withnall
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#include <config.h>
#include <string.h>
#include <errno.h>

#include <libedataserver/e-proxy.h>
#include <libebook/e-contact.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend-cache.h>
#include <gdata/gdata-service.h>
#include <gdata/services/contacts/gdata-contacts-service.h>
#include <gdata/services/contacts/gdata-contacts-query.h>
#include <gdata/services/contacts/gdata-contacts-contact.h>

#include "e-book-backend-google.h"
#include "util.h"

#define URI_GET_CONTACTS "://www.google.com/m8/feeds/contacts/default/full"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_BACKEND_SYNC)

typedef enum {
	NO_CACHE,
	ON_DISK_CACHE,
	IN_MEMORY_CACHE
} CacheType;

struct _EBookBackendGooglePrivate {
	EDataBookMode mode;
	GList *bookviews;

	gchar *username;
	CacheType cache_type;
	union {
		EBookBackendCache *on_disk;
		struct {
			GHashTable *contacts;
			GHashTable *gdata_entries;
			GTimeVal last_updated;
		} in_memory;
	} cache;

	gboolean offline;
	GDataService *service;
	EProxy *proxy;
	guint refresh_interval;
	gboolean use_ssl;

	/* Whether the backend is being used by a view at the moment; if it isn't, we don't need to do updates or send out notifications */
	gboolean live_mode;

	/* In live mode we will send out signals in an idle_handler */
	guint idle_id;

	guint refresh_id;
};

gboolean __e_book_backend_google_debug__;

static void data_book_error_from_gdata_error (GError **dest_err, GError *error);

static void
cache_init (EBookBackend *backend, gboolean on_disk)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	const gchar *cache_dir;

	cache_dir = e_book_backend_get_cache_dir (backend);

	if (on_disk) {
		gchar *filename;

		filename = g_build_filename (cache_dir, "cache.xml", NULL);
		priv->cache_type = ON_DISK_CACHE;
		priv->cache.on_disk = e_book_backend_cache_new (filename);
		g_free (filename);
	} else {
		priv->cache_type = IN_MEMORY_CACHE;
		priv->cache.in_memory.contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		priv->cache.in_memory.gdata_entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		memset (&priv->cache.in_memory.last_updated, 0, sizeof (GTimeVal));
	}
}

static EContact *
cache_add_contact (EBookBackend *backend, GDataEntry *entry)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact;
	const gchar *uid;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contact = _e_contact_new_from_gdata_entry (entry);
		_e_contact_add_gdata_entry_xml (contact, entry);
		e_book_backend_cache_add_contact (priv->cache.on_disk, contact);
		_e_contact_remove_gdata_entry_xml (contact);
		return contact;
	case IN_MEMORY_CACHE:
		contact = _e_contact_new_from_gdata_entry (entry);
		uid = e_contact_get_const (contact, E_CONTACT_UID);
		g_hash_table_insert (priv->cache.in_memory.contacts, g_strdup (uid), g_object_ref (contact));
		g_hash_table_insert (priv->cache.in_memory.gdata_entries, g_strdup (uid), g_object_ref (entry));
		return contact;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static gboolean
cache_remove_contact (EBookBackend *backend, const gchar *uid)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	gboolean success = TRUE;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_remove_contact (priv->cache.on_disk, uid);
	case IN_MEMORY_CACHE:
		success = g_hash_table_remove (priv->cache.in_memory.contacts, uid);
		return success && g_hash_table_remove (priv->cache.in_memory.gdata_entries, uid);
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static gboolean
cache_has_contact (EBookBackend *backend, const gchar *uid)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_check_contact (priv->cache.on_disk, uid);
	case IN_MEMORY_CACHE:
		return g_hash_table_lookup (priv->cache.in_memory.contacts, uid) ? TRUE : FALSE;
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static EContact *
cache_get_contact (EBookBackend *backend, const gchar *uid, GDataEntry **entry)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contact = e_book_backend_cache_get_contact (priv->cache.on_disk, uid);
		if (contact) {
			if (entry) {
				const gchar *entry_xml, *edit_link;

				entry_xml = _e_contact_get_gdata_entry_xml (contact, &edit_link);
				*entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, entry_xml, -1, NULL));

				if (*entry) {
					GDataLink *link = gdata_link_new (edit_link, GDATA_LINK_EDIT);
					gdata_entry_add_link (*entry, link);
					g_object_unref (link);
				}
			}

			_e_contact_remove_gdata_entry_xml (contact);
		}
		return contact;
	case IN_MEMORY_CACHE:
		contact = g_hash_table_lookup (priv->cache.in_memory.contacts, uid);
		if (entry) {
			*entry = g_hash_table_lookup (priv->cache.in_memory.gdata_entries, uid);
			if (*entry)
				g_object_ref (*entry);
		}

		if (contact)
			g_object_ref (contact);

		return contact;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static GList *
_g_hash_table_to_list (GHashTable *ht)
{
	GList *l = NULL;
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, ht);
	while (g_hash_table_iter_next (&iter, &key, &value))
		l = g_list_prepend (l, g_object_ref (G_OBJECT (value)));

	l = g_list_reverse (l);

	return l;
}

static GList *
cache_get_contacts (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *contacts, *iter;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contacts = e_book_backend_cache_get_contacts (priv->cache.on_disk, "(contains \"x-evolution-any-field\" \"\")");
		for (iter = contacts; iter; iter = iter->next)
			_e_contact_remove_gdata_entry_xml (iter->data);

		return contacts;
	case IN_MEMORY_CACHE:
		return _g_hash_table_to_list (priv->cache.in_memory.contacts);
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static void
cache_freeze (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	if (priv->cache_type == ON_DISK_CACHE)
		e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache.on_disk));
}

static void
cache_thaw (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	if (priv->cache_type == ON_DISK_CACHE)
		e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache.on_disk));
}

static gchar *
cache_get_last_update (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_get_time (priv->cache.on_disk);
	case IN_MEMORY_CACHE:
		if (priv->cache.in_memory.contacts)
			return g_time_val_to_iso8601 (&priv->cache.in_memory.last_updated);
		break;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static gboolean
cache_get_last_update_tv (EBookBackend *backend, GTimeVal *tv)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	gchar *last_update;
	gint rv;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		last_update = e_book_backend_cache_get_time (priv->cache.on_disk);
		rv = last_update ? g_time_val_from_iso8601 (last_update, tv) : FALSE;
		g_free (last_update);
		return rv;
	case IN_MEMORY_CACHE:
		memcpy (tv, &priv->cache.in_memory.last_updated, sizeof (GTimeVal));
		return priv->cache.in_memory.contacts != NULL;
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static void
cache_set_last_update (EBookBackend *backend, GTimeVal *tv)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	gchar *_time;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		_time = g_time_val_to_iso8601 (tv);
		/* Work around a bug in EBookBackendCache */
		e_file_cache_remove_object (E_FILE_CACHE (priv->cache.on_disk), "last_update_time");
		e_book_backend_cache_set_time (priv->cache.on_disk, _time);
		g_free (_time);
		return;
	case IN_MEMORY_CACHE:
		memcpy (&priv->cache.in_memory.last_updated, tv, sizeof (GTimeVal));
	case NO_CACHE:
	default:
		break;
	}
}

static gboolean
cache_needs_update (EBookBackend *backend, guint *remaining_secs)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GTimeVal last, current;
	guint diff;
	gboolean rv;

	if (remaining_secs)
		*remaining_secs = G_MAXUINT;

	/* We never want to update in offline mode */
	if (priv->offline)
		return FALSE;

	rv = cache_get_last_update_tv (backend, &last);

	if (!rv)
		return TRUE;

	g_get_current_time (&current);
	if (last.tv_sec > current.tv_sec) {
		g_warning ("last update is in the feature?");

		/* Do an update so we can fix this */
		return TRUE;
	}
	diff = current.tv_sec - last.tv_sec;

	if (diff >= priv->refresh_interval)
		return TRUE;

	if (remaining_secs)
		*remaining_secs = priv->refresh_interval - diff;

	__debug__ ("No update needed. Next update needed in %d secs", priv->refresh_interval - diff);

	return FALSE;
}

static void
on_contact_added (EBookBackend *backend, EContact *contact)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *iter;

	if (!priv->live_mode)
		return;

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), g_object_ref (contact));
}

static void
on_contact_removed (EBookBackend *backend, const gchar *uid)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *iter;

	if (!priv->live_mode)
		return;

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_remove (E_DATA_BOOK_VIEW (iter->data), g_strdup (uid));
}

static void
on_contact_changed (EBookBackend *backend, EContact *contact)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *iter;

	if (!priv->live_mode)
		return;

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), g_object_ref (contact));
}

static void
on_sequence_complete (EBookBackend *backend, GError *error)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *iter;
	GError *err = NULL;

	if (!priv->live_mode)
		return;

	if (error) {

		data_book_error_from_gdata_error (&err, error);

		__debug__ ("Book-view query failed: %s", error->message);
	}

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_complete (E_DATA_BOOK_VIEW (iter->data), err);

	if (err)
		g_error_free (err);
}

static void
process_subsequent_entry (GDataEntry *entry, EBookBackend *backend)
{
	gboolean is_deleted, is_cached;
	const gchar *uid;

	__debug__ (G_STRFUNC);
	uid = gdata_entry_get_id (entry);
	is_deleted = gdata_contacts_contact_is_deleted (GDATA_CONTACTS_CONTACT (entry));

	is_cached = cache_has_contact (backend, uid);
	if (is_deleted) {
		/* Do we have this item in our cache? */
		if (is_cached) {
			cache_remove_contact (backend, uid);
			on_contact_removed (backend, uid);
		}
	} else {
		EContact *contact = cache_add_contact (backend, entry);

		if (is_cached)
			on_contact_changed (backend, contact);
		else
			on_contact_added (backend, contact);

		g_object_unref (contact);
	}
}

static void
process_initial_entry (GDataEntry *entry, EBookBackend *backend)
{
	EContact *contact;

	__debug__ (G_STRFUNC);

	contact = cache_add_contact (backend, entry);
	on_contact_added (backend, contact);
	g_object_unref (contact);
}

static gboolean
get_new_contacts_in_chunks (EBookBackend *backend, gint chunk_size, GError **error)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GDataFeed *feed;
	GDataQuery *query;
	gchar *last_updated;
	GError *our_error = NULL;
	gboolean rv = TRUE;
	GTimeVal current_time;
	gint results;

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (priv->service, FALSE);

	last_updated = cache_get_last_update (backend);
	cache_freeze (backend);

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 1, chunk_size));
	if (last_updated) {
		GTimeVal updated;

		g_assert (g_time_val_from_iso8601 (last_updated, &updated) == TRUE);
		gdata_query_set_updated_min (query, &updated);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	/* Get the paginated results */
	do {
		GList *entries;

		/* Run the query */
		feed = gdata_contacts_service_query_contacts (GDATA_CONTACTS_SERVICE (priv->service), query, NULL, NULL, NULL, &our_error);

		if (our_error) {
			on_sequence_complete (backend, our_error);
			g_propagate_error (error, our_error);

			rv = FALSE;
			goto out;
		}

		entries = gdata_feed_get_entries (feed);
		results = entries ? g_list_length (entries) : 0;
		__debug__ ("Feed has %d entries", results);

		/* Process the entries from this page */
		if (last_updated)
			g_list_foreach (entries, (GFunc) process_subsequent_entry, backend);
		else
			g_list_foreach (entries, (GFunc) process_initial_entry, backend);
		g_object_unref (feed);

		/* Move to the next page */
		gdata_query_next_page (query);
	} while (results == chunk_size);

	/* Finish updating the cache */
	g_get_current_time (&current_time);
	cache_set_last_update (backend, &current_time);
	on_sequence_complete (backend, NULL);

out:
	g_free (last_updated);
	cache_thaw (backend);

	return rv;
}

static gboolean cache_refresh_if_needed (EBookBackend *backend, GError **error);

static gboolean
on_refresh_timeout (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	__debug__ (G_STRFUNC);

	priv->refresh_id = 0;
	if (priv->live_mode)
		cache_refresh_if_needed (backend, NULL);

	return FALSE;
}

static gboolean
cache_refresh_if_needed (EBookBackend *backend, GError **error)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	guint remaining_secs;
	gint rv = TRUE;
	gboolean install_timeout;

	__debug__ (G_STRFUNC);

	if (priv->offline || !priv->service) {
		__debug__ ("We are not connected to Google%s.", priv->offline ? " (offline mode)" : "");
		return TRUE;
	}

	install_timeout = (priv->live_mode && priv->refresh_interval > 0 && 0 == priv->refresh_id);

	if (cache_needs_update (backend, &remaining_secs)) {
		rv = get_new_contacts_in_chunks (backend, 32, error);
		if (install_timeout)
			priv->refresh_id = g_timeout_add_seconds (priv->refresh_interval, (GSourceFunc) on_refresh_timeout, backend);
	} else {
		if (install_timeout) {
			__debug__ ("Installing timeout with %d seconds", remaining_secs);
			priv->refresh_id = g_timeout_add_seconds (remaining_secs, (GSourceFunc) on_refresh_timeout, backend);
		}
	}
	return rv;
}

static void
cache_destroy (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		g_object_unref (priv->cache.on_disk);
		break;
	case IN_MEMORY_CACHE:
		g_hash_table_destroy (priv->cache.in_memory.contacts);
		g_hash_table_destroy (priv->cache.in_memory.gdata_entries);
		break;
	case NO_CACHE:
	default:
		break;
	}

	priv->cache_type = NO_CACHE;
}

static void
e_book_backend_google_create_contact (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *vcard_str, EContact **out_contact, GError **perror)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact;
	GError *error = NULL;
	GDataEntry *entry, *new_entry;
	gchar *xml;

	__debug__ (G_STRFUNC);

	__debug__ ("Creating: %s", vcard_str);
	*out_contact = NULL;

	if (priv->mode != E_DATA_BOOK_MODE_REMOTE) {
		g_propagate_error (perror, EDB_ERROR (OFFLINE_UNAVAILABLE));
		return;
	}

	g_return_if_fail (priv->service);

	/* Build the GDataEntry from the vCard */
	contact = e_contact_new_from_vcard (vcard_str);
	entry = _gdata_entry_new_from_e_contact (contact);
	g_object_unref (contact);

	/* Debug XML output */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	__debug__ ("new entry with xml: %s", xml);
	g_free (xml);

	/* Insert the entry on the server */
	new_entry = GDATA_ENTRY (
		gdata_contacts_service_insert_contact (
			GDATA_CONTACTS_SERVICE (priv->service),
			GDATA_CONTACTS_CONTACT (entry),
			NULL, &error));
	g_object_unref (entry);

	if (!new_entry) {
		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Creating contact failed: %s", error->message);
		g_error_free (error);

		return;
	}

	/* Add the new contact to the cache */
	*out_contact = cache_add_contact (E_BOOK_BACKEND (backend), new_entry);
	g_object_unref (new_entry);
}

static void
e_book_backend_google_remove_contacts (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList *id_list, GList **ids, GError **perror)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *id_iter;

	__debug__ (G_STRFUNC);

	*ids = NULL;

	if (priv->mode != E_DATA_BOOK_MODE_REMOTE) {
		g_propagate_error (perror, EDB_ERROR (OFFLINE_UNAVAILABLE));
		return;
	}

	g_return_if_fail (priv->service);

	for (id_iter = id_list; id_iter; id_iter = id_iter->next) {
		GError *error = NULL;
		const gchar *uid;
		GDataEntry *entry = NULL;
		EContact *cached_contact;

		/* Get the contact and associated GDataEntry from the cache */
		uid = id_iter->data;
		cached_contact = cache_get_contact (E_BOOK_BACKEND (backend), uid, &entry);

		if (!cached_contact) {
			/* Only the last error will be reported */
			g_clear_error (perror);
			if (perror)
				*perror = EDB_ERROR (CONTACT_NOT_FOUND);
			__debug__ ("Deleting contact %s failed: Contact not found in cache.", uid);

			continue;
		}

		g_object_unref (cached_contact);

		/* Remove the contact from the cache */
		cache_remove_contact (E_BOOK_BACKEND (backend), uid);

		/* Delete the contact from the server */
		if (!gdata_service_delete_entry (GDATA_SERVICE (priv->service), entry, NULL, &error)) {
			/* Only last error will be reported */
			data_book_error_from_gdata_error (perror, error);
			__debug__ ("Deleting contact %s failed: %s", uid, error->message);
			g_error_free (error);
		} else {
			/* Success! */
			*ids = g_list_append (*ids, g_strdup (uid));
		}

		g_object_unref (entry);
	}

	/* On error, return the last one */
	if (!*ids) {
		if (perror && !*perror)
			*perror = EDB_ERROR (OTHER_ERROR);
	}
}

static void
e_book_backend_google_modify_contact (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *vcard_str, EContact **out_contact, GError **perror)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact, *cached_contact;
	GError *error = NULL;
	GDataEntry *entry = NULL, *new_entry;
	gchar *xml;
	const gchar *uid;

	__debug__ (G_STRFUNC);

	__debug__ ("Updating: %s", vcard_str);
	*out_contact = NULL;

	if (priv->mode != E_DATA_BOOK_MODE_REMOTE) {
		g_propagate_error (perror, EDB_ERROR (OFFLINE_UNAVAILABLE));
		return;
	}

	g_return_if_fail (priv->service);

	/* Get the new contact and its UID */
	contact = e_contact_new_from_vcard (vcard_str);
	uid = e_contact_get (contact, E_CONTACT_UID);

	/* Get the old cached contact with the same UID and its associated GDataEntry */
	cached_contact = cache_get_contact (E_BOOK_BACKEND (backend), uid, &entry);

	if (!cached_contact) {
		__debug__ ("Modifying contact failed: Contact with uid %s not found in cache.", uid);
		g_object_unref (contact);

		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	}

	g_object_unref (cached_contact);

	/* Update the old GDataEntry from the new contact */
	_gdata_entry_update_from_e_contact (entry, contact);
	g_object_unref (contact);

	/* Output debug XML */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	__debug__ ("Before:\n%s", xml);
	g_free (xml);

	/* Update the contact on the server */
	new_entry = GDATA_ENTRY (
		gdata_contacts_service_update_contact (
			GDATA_CONTACTS_SERVICE (priv->service),
			GDATA_CONTACTS_CONTACT (entry),
			NULL, &error));
	g_object_unref (entry);

	if (!new_entry) {
		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Modifying contact failed: %s", error->message);
		g_error_free (error);

		return;
	}

	/* Output debug XML */
	xml = NULL;
	if (new_entry)
		xml = gdata_parsable_get_xml (GDATA_PARSABLE (new_entry));
	__debug__ ("After:\n%s", xml);
	g_free (xml);

	/* Add the new entry to the cache */
	*out_contact = cache_add_contact (E_BOOK_BACKEND (backend), new_entry);
	g_object_unref (new_entry);
}

static void
e_book_backend_google_get_contact (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *uid, gchar **vcard_str, GError **perror)
{
	EContact *contact;
	GError *error = NULL;

	__debug__ (G_STRFUNC);

	/* Refresh the cache */
	cache_refresh_if_needed (E_BOOK_BACKEND (backend), &error);

	if (error) {
		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Getting contact with uid %s failed: %s", uid, error->message);
		g_error_free (error);

		return;
	}

	/* Get the contact */
	contact = cache_get_contact (E_BOOK_BACKEND (backend), uid, NULL);

	if (!contact) {
		__debug__ ("Getting contact with uid %s failed: Contact not found in cache.", uid);

		g_propagate_error (perror, EDB_ERROR (CONTACT_NOT_FOUND));
		return;
	}

	/* Success! Build and return a vCard of the contacts */
	*vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	g_object_unref (contact);
}

static void
e_book_backend_google_get_contact_list (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *query, GList **contacts, GError **perror)
{
	EBookBackendSExp *sexp;
	GError *error = NULL;
	GList *all_contacts;

	__debug__ (G_STRFUNC);

	*contacts = NULL;

	/* Refresh the cache */
	cache_refresh_if_needed (E_BOOK_BACKEND (backend), &error);

	if (error) {
		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Getting all contacts failed: %s", error->message);
		g_clear_error (&error);

		return;
	}

	/* Get all contacts */
	sexp = e_book_backend_sexp_new (query);
	all_contacts = cache_get_contacts (E_BOOK_BACKEND (backend));

	for (; all_contacts; all_contacts = g_list_delete_link (all_contacts, all_contacts)) {
		EContact *contact = all_contacts->data;

		/* If the search expression matches the contact, include it in the search results */
		if (e_book_backend_sexp_match_contact (sexp, contact)) {
			gchar *vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			*contacts = g_list_append (*contacts, vcard_str);
		}

		g_object_unref (contact);
	}

	g_object_unref (sexp);
}

static gboolean
on_refresh_idle (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	priv->idle_id = 0;
	cache_refresh_if_needed (backend, NULL);

	return FALSE;
}

static void
set_live_mode (EBookBackend *backend, gboolean live_mode)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	__debug__ (G_STRFUNC);
	priv->live_mode = live_mode;

	if (!live_mode && priv->refresh_id > 0) {
		g_source_remove (priv->refresh_id);
		priv->refresh_id = 0;
	}

	if (priv->live_mode)
		cache_refresh_if_needed (backend, NULL);
}

static void
e_book_backend_google_start_book_view (EBookBackend *backend, EDataBookView *bookview)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *cached_contacts;

	g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (bookview));

	__debug__ (G_STRFUNC);

	priv->bookviews = g_list_append (priv->bookviews, bookview);

	e_data_book_view_ref (bookview);
	e_data_book_view_notify_status_message (bookview, "Loading...");

	/* Ensure that we're ready to support a view */
	set_live_mode (backend, TRUE);

	/* Update the cache if necessary */
	if (cache_needs_update (backend, NULL)) {
		if (!priv->service) {
			/* We need authorization first */
			e_book_backend_notify_auth_required (backend);
		} else {
			/* Update in an idle function, so that this call doesn't block */
			priv->idle_id = g_idle_add ((GSourceFunc) on_refresh_idle, backend);
		}
	}

	/* Get the contacts */
	cached_contacts = cache_get_contacts (backend);
	__debug__ ("%d contacts found in cache", g_list_length (cached_contacts));

	/* Notify the view that all the contacts have changed (i.e. been added) */
	for (; cached_contacts; cached_contacts = g_list_delete_link (cached_contacts, cached_contacts)) {
		EContact *contact = cached_contacts->data;
		e_data_book_view_notify_update (bookview, contact);
		g_object_unref (contact);
	}

	e_data_book_view_notify_complete (bookview, NULL /* Success */);
}

static void
e_book_backend_google_stop_book_view (EBookBackend *backend, EDataBookView *bookview)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *link;

	__debug__ (G_STRFUNC);

	/* Remove the view from the list of active views */
	if ((link = g_list_find (priv->bookviews, bookview)) != NULL) {
		priv->bookviews = g_list_delete_link (priv->bookviews, link);
		e_data_book_view_unref (bookview);
	}

	/* If there are no book views left, we can stop doing certain things, like refreshes */
	if (!priv->bookviews)
		set_live_mode (backend, FALSE);
}

static void
proxy_settings_changed (EProxy *proxy, EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	SoupURI *proxy_uri = NULL;
	gchar *uri;

	if (!priv || !priv->service)
		return;

	/* Build the URI which libgdata would use to query contacts */
	uri = g_strconcat (priv->use_ssl ? "https" : "http", URI_GET_CONTACTS, NULL);

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, uri))
		proxy_uri = e_proxy_peek_uri_for (proxy, uri);
	gdata_service_set_proxy_uri (GDATA_SERVICE (priv->service), proxy_uri);

	g_free (uri);
}

static void
e_book_backend_google_authenticate_user (EBookBackendSync *backend, EDataBook *book, guint32 opid,
                                         const gchar *username, const gchar *password, const gchar *auth_method, GError **perror)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GError *error = NULL;
	gboolean match;

	__debug__ (G_STRFUNC);

	if (priv->mode != E_DATA_BOOK_MODE_REMOTE)
		return;

	if (priv->service) {
		g_warning ("Connection to Google already established.");
		e_book_backend_notify_writable (E_BOOK_BACKEND (backend), TRUE);
		return;
	}

	if (!username || username[0] == 0 || !password || password[0] == 0) {
		g_propagate_error (perror, EDB_ERROR (AUTHENTICATION_FAILED));
		return;
	}

	match = (strcmp (username, priv->username) == 0);
	if (!match) {
		g_warning ("Username given when loading source and on authentication did not match!");
		g_propagate_error (perror, EDB_ERROR (AUTHENTICATION_REQUIRED));
		return;
	}

	/* Set up the service and proxy */
	priv->service = GDATA_SERVICE (gdata_contacts_service_new ("evolution-client-0.1.0"));

	priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (priv->proxy);

	proxy_settings_changed (priv->proxy, E_BOOK_BACKEND (backend));
	g_signal_connect (priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), backend);

	/* Authenticate with the server */
	if (!gdata_service_authenticate (priv->service, priv->username, password, NULL, &error)) {
		g_object_unref (priv->service);
		priv->service = NULL;
		g_object_unref (priv->proxy);
		priv->proxy = NULL;

		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Authentication failed: %s", error->message);
		g_error_free (error);

		return;
	}

	/* Update the cache if neccessary */
	cache_refresh_if_needed (E_BOOK_BACKEND (backend), &error);

	if (error) {
		data_book_error_from_gdata_error (perror, error);
		__debug__ ("Authentication failed: %s", error->message);
		g_error_free (error);

		return;
	}

	e_book_backend_notify_writable (E_BOOK_BACKEND (backend), TRUE);
}

static void
e_book_backend_google_get_supported_auth_methods (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **methods, GError **perror)
{
	__debug__ (G_STRFUNC);

	*methods = g_list_prepend (NULL, g_strdup ("plain/password"));
}

static void
e_book_backend_google_get_required_fields (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **fields_out, GError **perror)
{
	__debug__ (G_STRFUNC);

	*fields_out = NULL;
}

static void
e_book_backend_google_get_supported_fields (EBookBackendSync *backend, EDataBook *book, guint32 opid, GList **fields_out, GError **perror)
{
	GList *fields = NULL;
	guint i;
	const gint supported_fields[] = {
		E_CONTACT_FULL_NAME,
		E_CONTACT_EMAIL_1,
		E_CONTACT_EMAIL_2,
		E_CONTACT_EMAIL_3,
		E_CONTACT_EMAIL_4,
		E_CONTACT_ADDRESS_LABEL_HOME,
		E_CONTACT_ADDRESS_LABEL_WORK,
		E_CONTACT_ADDRESS_LABEL_OTHER,
		E_CONTACT_PHONE_HOME,
		E_CONTACT_PHONE_HOME_FAX,
		E_CONTACT_PHONE_BUSINESS,
		E_CONTACT_PHONE_BUSINESS_FAX,
		E_CONTACT_PHONE_MOBILE,
		E_CONTACT_PHONE_PAGER,
		E_CONTACT_IM_AIM,
		E_CONTACT_IM_JABBER,
		E_CONTACT_IM_YAHOO,
		E_CONTACT_IM_MSN,
		E_CONTACT_IM_ICQ,
		E_CONTACT_IM_SKYPE,
		E_CONTACT_IM_GADUGADU,
		E_CONTACT_IM_GROUPWISE,
		E_CONTACT_ADDRESS,
		E_CONTACT_ADDRESS_HOME,
		E_CONTACT_ADDRESS_WORK,
		E_CONTACT_ADDRESS_OTHER,
		E_CONTACT_NAME,
		E_CONTACT_GIVEN_NAME,
		E_CONTACT_FAMILY_NAME,
		E_CONTACT_PHONE_ASSISTANT,
		E_CONTACT_PHONE_BUSINESS_2,
		E_CONTACT_PHONE_CALLBACK,
		E_CONTACT_PHONE_CAR,
		E_CONTACT_PHONE_COMPANY,
		E_CONTACT_PHONE_HOME_2,
		E_CONTACT_PHONE_ISDN,
		E_CONTACT_PHONE_OTHER,
		E_CONTACT_PHONE_OTHER_FAX,
		E_CONTACT_PHONE_PRIMARY,
		E_CONTACT_PHONE_RADIO,
		E_CONTACT_PHONE_TELEX,
		E_CONTACT_PHONE_TTYTDD,
		E_CONTACT_IM_AIM_HOME_1,
		E_CONTACT_IM_AIM_HOME_2,
		E_CONTACT_IM_AIM_HOME_3,
		E_CONTACT_IM_AIM_WORK_1,
		E_CONTACT_IM_AIM_WORK_2,
		E_CONTACT_IM_AIM_WORK_3,
		E_CONTACT_IM_GROUPWISE_HOME_1,
		E_CONTACT_IM_GROUPWISE_HOME_2,
		E_CONTACT_IM_GROUPWISE_HOME_3,
		E_CONTACT_IM_GROUPWISE_WORK_1,
		E_CONTACT_IM_GROUPWISE_WORK_2,
		E_CONTACT_IM_GROUPWISE_WORK_3,
		E_CONTACT_IM_JABBER_HOME_1,
		E_CONTACT_IM_JABBER_HOME_2,
		E_CONTACT_IM_JABBER_HOME_3,
		E_CONTACT_IM_JABBER_WORK_1,
		E_CONTACT_IM_JABBER_WORK_2,
		E_CONTACT_IM_JABBER_WORK_3,
		E_CONTACT_IM_YAHOO_HOME_1,
		E_CONTACT_IM_YAHOO_HOME_2,
		E_CONTACT_IM_YAHOO_HOME_3,
		E_CONTACT_IM_YAHOO_WORK_1,
		E_CONTACT_IM_YAHOO_WORK_2,
		E_CONTACT_IM_YAHOO_WORK_3,
		E_CONTACT_IM_MSN_HOME_1,
		E_CONTACT_IM_MSN_HOME_2,
		E_CONTACT_IM_MSN_HOME_3,
		E_CONTACT_IM_MSN_WORK_1,
		E_CONTACT_IM_MSN_WORK_2,
		E_CONTACT_IM_MSN_WORK_3,
		E_CONTACT_IM_ICQ_HOME_1,
		E_CONTACT_IM_ICQ_HOME_2,
		E_CONTACT_IM_ICQ_HOME_3,
		E_CONTACT_IM_ICQ_WORK_1,
		E_CONTACT_IM_ICQ_WORK_2,
		E_CONTACT_IM_ICQ_WORK_3,
		E_CONTACT_EMAIL,
		E_CONTACT_IM_GADUGADU_HOME_1,
		E_CONTACT_IM_GADUGADU_HOME_2,
		E_CONTACT_IM_GADUGADU_HOME_3,
		E_CONTACT_IM_GADUGADU_WORK_1,
		E_CONTACT_IM_GADUGADU_WORK_2,
		E_CONTACT_IM_GADUGADU_WORK_3,
		E_CONTACT_TEL,
		E_CONTACT_IM_SKYPE_HOME_1,
		E_CONTACT_IM_SKYPE_HOME_2,
		E_CONTACT_IM_SKYPE_HOME_3,
		E_CONTACT_IM_SKYPE_WORK_1,
		E_CONTACT_IM_SKYPE_WORK_2,
		E_CONTACT_IM_SKYPE_WORK_3,
		E_CONTACT_SIP,
		E_CONTACT_ORG,
		E_CONTACT_ORG_UNIT,
		E_CONTACT_TITLE,
		E_CONTACT_ROLE,
		E_CONTACT_NOTE
	};

	__debug__ (G_STRFUNC);

	/* Add all the fields above to the list */
	for (i = 0; i < G_N_ELEMENTS (supported_fields); i++) {
		const gchar *field_name = e_contact_field_name (supported_fields[i]);
		fields = g_list_prepend (fields, g_strdup (field_name));
	}

	*fields_out = fields;
}

static void
e_book_backend_google_get_changes (EBookBackendSync *backend, EDataBook *book, guint32 opid, const gchar *change_id, GList **changes_out, GError **perror)
{
	__debug__ (G_STRFUNC);
	g_propagate_error (perror, EDB_ERROR (OTHER_ERROR));
}

static void
e_book_backend_google_remove (EBookBackendSync *backend, EDataBook *book, guint32 opid, GError **perror)
{
	__debug__ (G_STRFUNC);
}

static void
set_offline_mode (EBookBackend *backend, gboolean offline)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	__debug__ (G_STRFUNC);

	priv->offline = offline;

	if (offline) {
		/* Going offline, so we can free our service and proxy */
		if (priv->service)
			g_object_unref (priv->service);
		priv->service = NULL;

		if (priv->proxy)
			g_object_unref (priv->proxy);
		priv->proxy = NULL;
	} else {
		/* Going online, so we need to re-authenticate and re-create the service and proxy.
		 * This is done in e_book_backend_google_authenticate_user() when it gets the authentication data. */
		e_book_backend_notify_auth_required (backend);
	}
}

static void
e_book_backend_google_load_source (EBookBackend *backend, ESource *source, gboolean only_if_exists, GError **perror)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	const gchar *refresh_interval_str, *use_ssl_str, *use_cache_str;
	guint refresh_interval;
	gboolean use_ssl, use_cache;
	const gchar *username;

	__debug__ (G_STRFUNC);

	if (priv->username) {
		g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, "Source already loaded!"));
		return;
	}

	/* Parse the username property */
	username = e_source_get_property (source, "username");

	if (!username || username[0] == '\0') {
		g_propagate_error (perror, EDB_ERROR_EX (OTHER_ERROR, "No or empty username!"));
		return;
	}

	/* Parse various other properties */
	refresh_interval_str = e_source_get_property (source, "refresh-interval");
	use_ssl_str = e_source_get_property (source, "ssl");
	use_cache_str = e_source_get_property (source, "offline_sync");

	refresh_interval = 3600;
	if (refresh_interval_str && sscanf (refresh_interval_str, "%u", &refresh_interval) != 1) {
		g_warning ("Could not parse refresh-interval!");
		refresh_interval = 3600;
	}

	use_ssl = TRUE;
	if (use_ssl_str && (g_ascii_strcasecmp (use_ssl_str, "false") == 0 || strcmp (use_ssl_str, "0") == 0))
		use_ssl = FALSE;

	use_cache = TRUE;
	if (use_cache_str && (g_ascii_strcasecmp (use_cache_str, "false") == 0 || strcmp (use_cache_str, "0") == 0))
		use_cache = FALSE;

	/* Set up our object */
	priv->username = g_strdup (username);
	cache_init (backend, use_cache);
	priv->use_ssl = use_ssl;
	priv->refresh_interval = refresh_interval;

	/* Remove and re-add the timeout */
	if (priv->refresh_id != 0) {
		g_source_remove (priv->refresh_id);
		priv->refresh_id = g_timeout_add_seconds (priv->refresh_interval, (GSourceFunc) on_refresh_timeout, backend);
	}

	/* Set up ready to be interacted with */
	e_book_backend_set_is_loaded (backend, TRUE);
	e_book_backend_notify_connection_status (backend, TRUE);
	e_book_backend_set_is_writable (backend, FALSE);
	set_offline_mode (backend, (priv->mode == E_DATA_BOOK_MODE_LOCAL));
}

static gchar *
e_book_backend_google_get_static_capabilities (EBookBackend *backend)
{
	__debug__ (G_STRFUNC);
	return g_strdup ("net,do-initial-query,contact-lists");
}

static void
e_book_backend_google_cancel_operation (EBookBackend *backend, EDataBook *book, GError **perror)
{
	__debug__ (G_STRFUNC);
	g_propagate_error (perror, EDB_ERROR (COULD_NOT_CANCEL));
}

static void
e_book_backend_google_set_mode (EBookBackend *backend, EDataBookMode mode)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	gboolean online = (mode == E_DATA_BOOK_MODE_REMOTE);

	__debug__ (G_STRFUNC);

	if (mode == priv->mode)
		return;

	priv->mode = mode;

	set_offline_mode (backend, !online);
	e_book_backend_notify_connection_status (backend, online);

	/* Mark the book as unwriteable if we're going offline, but don't do the inverse when we go online;
	 * e_book_backend_google_authenticate_user() will mark us as writeable again once the user's authenticated again. */
	if (!online)
		e_book_backend_notify_writable (backend, FALSE);
}

static void
e_book_backend_google_dispose (GObject *object)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (object)->priv;

	__debug__ (G_STRFUNC);

	while (priv->bookviews) {
		e_data_book_view_unref (priv->bookviews->data);
		priv->bookviews = g_list_delete_link (priv->bookviews, priv->bookviews);
	}

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	if (priv->service)
		g_object_unref (priv->service);
	priv->service = NULL;

	if (priv->proxy)
		g_object_unref (priv->proxy);
	priv->proxy = NULL;

	cache_destroy (E_BOOK_BACKEND (object));

	G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
e_book_backend_google_finalize (GObject *object)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (object)->priv;

	__debug__ (G_STRFUNC);

	g_free (priv->username);

	G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *backend_class;
	EBookBackendSyncClass *sync_class;

	backend_class = E_BOOK_BACKEND_CLASS (klass);
	sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	g_type_class_add_private (klass, sizeof (EBookBackendGooglePrivate));

	/* Set the virtual methods. */
	backend_class->load_source                  = e_book_backend_google_load_source;
	backend_class->get_static_capabilities      = e_book_backend_google_get_static_capabilities;
	backend_class->start_book_view              = e_book_backend_google_start_book_view;
	backend_class->stop_book_view               = e_book_backend_google_stop_book_view;
	backend_class->cancel_operation             = e_book_backend_google_cancel_operation;
	backend_class->set_mode                     = e_book_backend_google_set_mode;
	sync_class->remove_sync                     = e_book_backend_google_remove;
	sync_class->create_contact_sync             = e_book_backend_google_create_contact;
	sync_class->remove_contacts_sync            = e_book_backend_google_remove_contacts;
	sync_class->modify_contact_sync             = e_book_backend_google_modify_contact;
	sync_class->get_contact_sync                = e_book_backend_google_get_contact;
	sync_class->get_contact_list_sync           = e_book_backend_google_get_contact_list;
	sync_class->get_changes_sync                = e_book_backend_google_get_changes;
	sync_class->authenticate_user_sync          = e_book_backend_google_authenticate_user;
	sync_class->get_supported_fields_sync       = e_book_backend_google_get_supported_fields;
	sync_class->get_required_fields_sync        = e_book_backend_google_get_required_fields;
	sync_class->get_supported_auth_methods_sync = e_book_backend_google_get_supported_auth_methods;

	object_class->dispose  = e_book_backend_google_dispose;
	object_class->finalize = e_book_backend_google_finalize;

	__e_book_backend_google_debug__ = g_getenv ("GOOGLE_BACKEND_DEBUG") ? TRUE : FALSE;
}

static void
e_book_backend_google_init (EBookBackendGoogle *backend)
{
	__debug__ (G_STRFUNC);
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (backend, E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGooglePrivate);
}

EBookBackend *
e_book_backend_google_new (void)
{
	EBookBackendGoogle *backend;

	__debug__ (G_STRFUNC);
	backend = g_object_new (E_TYPE_BOOK_BACKEND_GOOGLE, NULL);

	return E_BOOK_BACKEND (backend);
}

static void
data_book_error_from_gdata_error (GError **dest_err, GError *error)
{
	if (!error || !dest_err)
		return;

	/* only last error is used */
	g_clear_error (dest_err);

	if (error->domain == GDATA_AUTHENTICATION_ERROR) {
		/* Authentication errors */
		switch (error->code) {
		case GDATA_AUTHENTICATION_ERROR_BAD_AUTHENTICATION:
			g_propagate_error (dest_err, EDB_ERROR (AUTHENTICATION_FAILED));
			return;
		case GDATA_AUTHENTICATION_ERROR_NOT_VERIFIED:
		case GDATA_AUTHENTICATION_ERROR_TERMS_NOT_AGREED:
		case GDATA_AUTHENTICATION_ERROR_CAPTCHA_REQUIRED:
		case GDATA_AUTHENTICATION_ERROR_ACCOUNT_DELETED:
		case GDATA_AUTHENTICATION_ERROR_ACCOUNT_DISABLED:
			g_propagate_error (dest_err, EDB_ERROR (PERMISSION_DENIED));
			return;
		case GDATA_AUTHENTICATION_ERROR_SERVICE_DISABLED:
			g_propagate_error (dest_err, EDB_ERROR (REPOSITORY_OFFLINE));
			return;
		default:
			break;
		}
	} else if (error->domain == GDATA_SERVICE_ERROR) {
		/* General service errors */
		switch (error->code) {
		case GDATA_SERVICE_ERROR_UNAVAILABLE:
			g_propagate_error (dest_err, EDB_ERROR (REPOSITORY_OFFLINE));
			return;
		case GDATA_SERVICE_ERROR_PROTOCOL_ERROR:
			g_propagate_error (dest_err, EDB_ERROR (INVALID_QUERY));
			return;
		case GDATA_SERVICE_ERROR_ENTRY_ALREADY_INSERTED:
			g_propagate_error (dest_err, EDB_ERROR (CONTACTID_ALREADY_EXISTS));
			return;
		case GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED:
			g_propagate_error (dest_err, EDB_ERROR (AUTHENTICATION_REQUIRED));
			return;
		case GDATA_SERVICE_ERROR_NOT_FOUND:
			g_propagate_error (dest_err, EDB_ERROR (CONTACT_NOT_FOUND));
			return;
		case GDATA_SERVICE_ERROR_CONFLICT:
			g_propagate_error (dest_err, EDB_ERROR (CONTACTID_ALREADY_EXISTS));
			return;
		case GDATA_SERVICE_ERROR_FORBIDDEN:
			g_propagate_error (dest_err, EDB_ERROR (QUERY_REFUSED));
			return;
		case GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER:
			g_propagate_error (dest_err, EDB_ERROR (INVALID_QUERY));
			return;
		default:
			break;
		}
	}

	g_propagate_error (dest_err, e_data_book_create_error (E_DATA_BOOK_STATUS_OTHER_ERROR, error->message));
}


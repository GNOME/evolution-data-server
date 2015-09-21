/* e-book-backend-google.c - Google contact backendy.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010, 2011 Philip Withnall
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
 * Authors: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 *          Philip Withnall <philip@tecnocode.co.uk>
 */

#include <config.h>
#include <string.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <gdata/gdata.h>

#include "e-book-backend-google.h"
#include "e-book-google-utils.h"
#include "e-gdata-oauth2-authorizer.h"

#define E_BOOK_BACKEND_GOOGLE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGooglePrivate))

#define CLIENT_ID "evolution-client-0.1.0"

#define URI_GET_CONTACTS "https://www.google.com/m8/feeds/contacts/default/full"

/* This macro was introduced in libgdata 0.11,
 * but we currently only require libgdata 0.10. */
#ifndef GDATA_CHECK_VERSION
#define GDATA_CHECK_VERSION(major,minor,micro) 0
#endif

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_BACKEND)

struct _EBookBackendGooglePrivate {
	EBookBackendCache *cache;
	GMutex cache_lock;

	/* For all the group-related members */
	GRecMutex groups_lock;
	/* Mapping from group ID to (human readable) group name */
	GHashTable *groups_by_id;
	/* Mapping from (human readable) group name to group ID */
	GHashTable *groups_by_name;
	/* Mapping system_group_id to entry ID */
	GHashTable *system_groups_by_id;
	/* Mapping entry ID to system_group_id */
	GHashTable *system_groups_by_entry_id;
	/* Time when the groups were last queried */
	GTimeVal groups_last_update;

	GDataAuthorizer *authorizer;
	GDataService *service;

	guint refresh_id;

	/* Map of active opids to GCancellables */
	GHashTable *cancellables;

	/* Did the server-side groups change? If so, re-download the book */
	gboolean groups_changed;
};

static void
data_book_error_from_gdata_error (GError **error,
                                  const GError *gdata_error)
{
	gboolean use_fallback = FALSE;

	g_return_if_fail (gdata_error != NULL);

	/* Authentication errors */
	if (gdata_error->domain == GDATA_SERVICE_ERROR) {
		switch (gdata_error->code) {
		case GDATA_SERVICE_ERROR_UNAVAILABLE:
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_REPOSITORY_OFFLINE,
				e_client_error_to_string (
				E_CLIENT_ERROR_REPOSITORY_OFFLINE));
			break;
		case GDATA_SERVICE_ERROR_PROTOCOL_ERROR:
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_QUERY,
				gdata_error->message);
			break;
		case GDATA_SERVICE_ERROR_ENTRY_ALREADY_INSERTED:
			g_set_error_literal (
				error, E_BOOK_CLIENT_ERROR,
				E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS,
				e_book_client_error_to_string (
				E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS));
			break;
		case GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED:
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_AUTHENTICATION_REQUIRED,
				e_client_error_to_string (
				E_CLIENT_ERROR_AUTHENTICATION_REQUIRED));
			break;
		case GDATA_SERVICE_ERROR_NOT_FOUND:
			g_set_error_literal (
				error, E_BOOK_CLIENT_ERROR,
				E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
				e_book_client_error_to_string (
				E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
			break;
		case GDATA_SERVICE_ERROR_CONFLICT:
			g_set_error_literal (
				error, E_BOOK_CLIENT_ERROR,
				E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS,
				e_book_client_error_to_string (
				E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS));
			break;
		case GDATA_SERVICE_ERROR_FORBIDDEN:
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_QUERY_REFUSED,
				e_client_error_to_string (
				E_CLIENT_ERROR_QUERY_REFUSED));
			break;
		case GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER:
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_INVALID_QUERY,
				gdata_error->message);
			break;
		default:
			use_fallback = TRUE;
			break;
		}

	} else {
		use_fallback = TRUE;
	}

	/* Generic fallback */
	if (use_fallback)
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			gdata_error->message);
}

static void
migrate_cache (EBookBackendCache *cache)
{
	const gchar *version;
	const gchar *version_key = "book-cache-version";

	g_return_if_fail (cache != NULL);

	version = e_file_cache_get_object (E_FILE_CACHE (cache), version_key);
	if (!version || atoi (version) < 2) {
		/* not versioned yet or too old, dump the cache and reload it from the server */
		e_file_cache_clean (E_FILE_CACHE (cache));
		e_file_cache_add_object (E_FILE_CACHE (cache), version_key, "2");
	}
}

static void
cache_init (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	const gchar *cache_dir;
	gchar *filename;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);

	cache_dir = e_book_backend_get_cache_dir (backend);
	filename = g_build_filename (cache_dir, "cache.xml", NULL);
	priv->cache = e_book_backend_cache_new (filename);
	g_free (filename);

	migrate_cache (priv->cache);

	g_mutex_unlock (&priv->cache_lock);
}

static EContact *
cache_add_contact (EBookBackend *backend,
                   GDataEntry *entry)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_rec_mutex_lock (&priv->groups_lock);
	contact = e_contact_new_from_gdata_entry (entry, priv->groups_by_id, priv->system_groups_by_entry_id);
	g_rec_mutex_unlock (&priv->groups_lock);

	if (!contact)
		return NULL;

	e_contact_add_gdata_entry_xml (contact, entry);
	g_mutex_lock (&priv->cache_lock);
	e_book_backend_cache_add_contact (priv->cache, contact);
	g_mutex_unlock (&priv->cache_lock);
	e_contact_remove_gdata_entry_xml (contact);

	return contact;
}

static gboolean
cache_remove_contact (EBookBackend *backend,
                      const gchar *uid)
{
	EBookBackendGooglePrivate *priv;
	gboolean removed;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);
	removed = e_book_backend_cache_remove_contact (priv->cache, uid);
	g_mutex_unlock (&priv->cache_lock);

	return removed;
}

static gboolean
cache_has_contact (EBookBackend *backend,
                   const gchar *uid)
{
	EBookBackendGooglePrivate *priv;
	gboolean has_contact;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);
	has_contact = e_book_backend_cache_check_contact (priv->cache, uid);
	g_mutex_unlock (&priv->cache_lock);

	return has_contact;
}

static EContact *
cache_get_contact (EBookBackend *backend,
                   const gchar *uid,
                   GDataEntry **entry)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);
	contact = e_book_backend_cache_get_contact (priv->cache, uid);
	g_mutex_unlock (&priv->cache_lock);

	if (contact) {
		if (entry) {
			const gchar *entry_xml, *edit_uri = NULL;

			entry_xml = e_contact_get_gdata_entry_xml (contact, &edit_uri);
			*entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, entry_xml, -1, NULL));

			if (*entry) {
				GDataLink *edit_link = gdata_link_new (edit_uri, GDATA_LINK_EDIT);
				gdata_entry_add_link (*entry, edit_link);
				g_object_unref (edit_link);
			}
		}

		e_contact_remove_gdata_entry_xml (contact);
	}

	return contact;
}

static void
cache_get_contacts (EBookBackend *backend,
                    GQueue *out_contacts)
{
	EBookBackendGooglePrivate *priv;
	GList *list, *link;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);
	list = e_book_backend_cache_get_contacts (
		priv->cache, "(contains \"x-evolution-any-field\" \"\")");
	g_mutex_unlock (&priv->cache_lock);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EContact *contact = E_CONTACT (link->data);

		e_contact_remove_gdata_entry_xml (contact);
		g_queue_push_tail (out_contacts, g_object_ref (contact));
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
cache_freeze (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
}

static void
cache_thaw (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
}

static gchar *
cache_get_last_update (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	gchar *last_update;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_lock (&priv->cache_lock);
	last_update = e_book_backend_cache_get_time (priv->cache);
	g_mutex_unlock (&priv->cache_lock);

	return last_update;
}

static void
cache_set_last_update (EBookBackend *backend,
                       GTimeVal *tv)
{
	EBookBackendGooglePrivate *priv;
	gchar *_time;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	if (tv)
		_time = g_time_val_to_iso8601 (tv);
	else
		_time = NULL;

	g_mutex_lock (&priv->cache_lock);
	if (tv)
		e_book_backend_cache_set_time (priv->cache, _time);
	else
		e_file_cache_remove_object (E_FILE_CACHE (priv->cache), "last_update_time");
	g_mutex_unlock (&priv->cache_lock);
	g_free (_time);
}

/* returns whether group changed from the one stored in the cache;
 * returns FALSE, if the group was not in the cache yet;
 * also adds the group into the cache;
 * use group_name = NULL to remove it from the cache.
 */
static gboolean
cache_update_group (EBookBackend *backend,
                    const gchar *group_id,
                    const gchar *group_name)
{
	EBookBackendGooglePrivate *priv;
	EFileCache *file_cache;
	gboolean changed;
	gchar *key;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);
	key = g_strconcat ("google-group", ":", group_id, NULL);

	g_mutex_lock (&priv->cache_lock);

	file_cache = E_FILE_CACHE (priv->cache);

	if (group_name) {
		const gchar *old_value;

		old_value = e_file_cache_get_object (file_cache, key);
		changed = old_value && g_strcmp0 (old_value, group_name) != 0;

		if (!e_file_cache_replace_object (file_cache, key, group_name))
			e_file_cache_add_object (file_cache, key, group_name);

		/* Add the category to Evolution’s category list. */
		e_categories_add (group_name, NULL, NULL, TRUE);
	} else {
		const gchar *old_value;

		old_value = e_file_cache_get_object (file_cache, key);
		changed = e_file_cache_remove_object (file_cache, key);

		/* Remove the category from Evolution’s category list. */
		if (old_value != NULL) {
			e_categories_remove (old_value);
		}
	}

	g_mutex_unlock (&priv->cache_lock);

	g_free (key);

	return changed;
}

static gboolean
backend_is_authorized (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	if (priv->service == NULL)
		return FALSE;

	return gdata_service_is_authorized (priv->service);
}

static GCancellable *
start_operation (EBookBackend *backend,
                 guint32 opid,
                 GCancellable *cancellable,
                 const gchar *message)
{
	EBookBackendGooglePrivate *priv;
	GList *list, *link;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	/* Insert the operation into the set of active cancellable operations */
	if (cancellable)
		g_object_ref (cancellable);
	else
		cancellable = g_cancellable_new ();
	g_hash_table_insert (priv->cancellables, GUINT_TO_POINTER (opid), g_object_ref (cancellable));

	/* Send out a status message to each view */
	list = e_book_backend_list_views (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		EDataBookView *view = E_DATA_BOOK_VIEW (link->data);
		e_data_book_view_notify_progress (view, -1, message);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return cancellable;
}

static void
finish_operation (EBookBackend *backend,
                  guint32 opid,
                  const GError *gdata_error)
{
	EBookBackendGooglePrivate *priv;
	GError *book_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	if (gdata_error != NULL) {
		data_book_error_from_gdata_error (&book_error, gdata_error);
		g_debug ("Book view query failed: %s", book_error->message);
	}

	if (g_hash_table_remove (priv->cancellables, GUINT_TO_POINTER (opid))) {
		GList *list, *link;

		list = e_book_backend_list_views (backend);

		for (link = list; link != NULL; link = g_list_next (link)) {
			EDataBookView *view = E_DATA_BOOK_VIEW (link->data);
			e_data_book_view_notify_complete (view, book_error);
		}

		g_list_free_full (list, (GDestroyNotify) g_object_unref);
	}

	g_clear_error (&book_error);
}

static void
process_contact_finish (EBookBackend *backend,
                        GDataEntry *entry)
{
	EContact *new_contact;

	g_debug (G_STRFUNC);

	new_contact = cache_add_contact (backend, entry);

	if (!new_contact)
		return;

	e_book_backend_notify_update (backend, new_contact);

	g_object_unref (new_contact);
}

typedef struct {
	EBookBackend *backend;
	GCancellable *cancellable;
	GError *gdata_error;

	/* These two don't need locking; they're only accessed from the main thread. */
	gboolean update_complete;
	guint num_contacts_pending_photos;
} GetContactsData;

static void
check_get_new_contacts_finished (GetContactsData *data)
{
	g_debug (G_STRFUNC);

	/* Are we finished yet? */
	if (data->update_complete == FALSE || data->num_contacts_pending_photos > 0) {
		g_debug (
			"Bailing from check_get_new_contacts_finished(): update_complete: %u, num_contacts_pending_photos: %u, data: %p",
			data->update_complete, data->num_contacts_pending_photos, data);
		return;
	}

	g_debug ("Proceeding with check_get_new_contacts_finished() for data: %p.", data);

	finish_operation (data->backend, -1, data->gdata_error);

	/* Tidy up */
	g_object_unref (data->cancellable);
	g_object_unref (data->backend);
	g_clear_error (&data->gdata_error);

	g_slice_free (GetContactsData, data);
}

typedef struct {
	GetContactsData *parent_data;

	GCancellable *cancellable;
	gulong cancelled_handle;
} PhotoData;

static void
process_contact_photo_cancelled_cb (GCancellable *parent_cancellable,
                                    GCancellable *photo_cancellable)
{
	g_debug (G_STRFUNC);

	g_cancellable_cancel (photo_cancellable);
}

static void
process_contact_photo_cb (GDataContactsContact *gdata_contact,
                          GAsyncResult *async_result,
                          PhotoData *data)
{
	EBookBackend *backend = data->parent_data->backend;
	guint8 *photo_data = NULL;
	gsize photo_length;
	gchar *photo_content_type = NULL;
	GError *error = NULL;

	g_debug (G_STRFUNC);

	/* Finish downloading the photo */
	photo_data = gdata_contacts_contact_get_photo_finish (gdata_contact, async_result, &photo_length, &photo_content_type, &error);

	if (error == NULL) {
		EContactPhoto *photo;

		/* Success! Create an EContactPhoto and store it on the final GDataContactsContact object so it makes it into the cache. */
		photo = e_contact_photo_new ();
		photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
		photo->data.inlined.data = (guchar *) photo_data;
		photo->data.inlined.length = photo_length;
		photo->data.inlined.mime_type = photo_content_type;

		g_object_set_data_full (G_OBJECT (gdata_contact), "photo", photo, (GDestroyNotify) e_contact_photo_free);

		photo_data = NULL;
		photo_content_type = NULL;
	} else {
		/* Error. */
		g_debug ("Downloading contact photo for '%s' failed: %s", gdata_entry_get_id (GDATA_ENTRY (gdata_contact)), error->message);
		g_error_free (error);
	}

	process_contact_finish (backend, GDATA_ENTRY (gdata_contact));

	g_free (photo_data);
	g_free (photo_content_type);

	/* Disconnect from the cancellable. */
	g_cancellable_disconnect (data->parent_data->cancellable, data->cancelled_handle);
	g_object_unref (data->cancellable);

	data->parent_data->num_contacts_pending_photos--;
	check_get_new_contacts_finished (data->parent_data);

	g_slice_free (PhotoData, data);
}

static void
process_contact_cb (GDataEntry *entry,
                    guint entry_key,
                    guint entry_count,
                    GetContactsData *data)
{
	EBookBackendGooglePrivate *priv;
	EBookBackend *backend = data->backend;
	gboolean is_deleted, is_cached;
	const gchar *uid;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	uid = gdata_entry_get_id (entry);
	is_deleted = gdata_contacts_contact_is_deleted (GDATA_CONTACTS_CONTACT (entry));

	is_cached = cache_has_contact (backend, uid);
	if (is_deleted) {
		/* Do we have this item in our cache? */
		if (is_cached) {
			cache_remove_contact (backend, uid);
			e_book_backend_notify_remove (backend, uid);
		}
	} else {
		gchar *old_photo_etag = NULL;
		const gchar *new_photo_etag;

		/* Download the contact's photo first, if the contact's uncached or if the photo's been updated. */
		if (is_cached == TRUE) {
			EContact *old_contact;
			EContactPhoto *photo;
			EVCardAttribute *old_attr;

			old_contact = cache_get_contact (backend, uid, NULL);

			/* Get the old ETag. */
			old_attr = e_vcard_get_attribute (E_VCARD (old_contact), GDATA_PHOTO_ETAG_ATTR);
			old_photo_etag = (old_attr != NULL) ? e_vcard_attribute_get_value (old_attr) : NULL;

			/* Attach the old photo to the new contact. */
			photo = e_contact_get (old_contact, E_CONTACT_PHOTO);

			if (photo != NULL && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
				g_object_set_data_full (G_OBJECT (entry), "photo", photo, (GDestroyNotify) e_contact_photo_free);
			} else if (photo != NULL) {
				e_contact_photo_free (photo);
			}

			g_object_unref (old_contact);
		}

		new_photo_etag = gdata_contacts_contact_get_photo_etag (GDATA_CONTACTS_CONTACT (entry));

		if ((old_photo_etag == NULL && new_photo_etag != NULL) ||
		    (old_photo_etag != NULL && new_photo_etag != NULL && strcmp (old_photo_etag, new_photo_etag) != 0)) {
			GCancellable *cancellable;
			PhotoData *photo_data;

			photo_data = g_slice_new (PhotoData);
			photo_data->parent_data = data;

			/* Increment the count of contacts whose photos we're waiting for. */
			data->num_contacts_pending_photos++;

			/* Cancel downloading if the get_new_contacts() operation is cancelled. */
			cancellable = g_cancellable_new ();

			photo_data->cancellable = g_object_ref (cancellable);
			photo_data->cancelled_handle = g_cancellable_connect (
				data->cancellable, (GCallback) process_contact_photo_cancelled_cb,
				g_object_ref (cancellable), (GDestroyNotify) g_object_unref);

			/* Download the photo. */
			gdata_contacts_contact_get_photo_async (
				GDATA_CONTACTS_CONTACT (entry),
				GDATA_CONTACTS_SERVICE (priv->service), cancellable,
				(GAsyncReadyCallback) process_contact_photo_cb, photo_data);

			g_object_unref (cancellable);
			g_free (old_photo_etag);

			return;
		}

		g_free (old_photo_etag);

		/* Since we're not downloading a photo, add the contact to the cache now. */
		process_contact_finish (backend, entry);
	}
}

static void
get_new_contacts_cb (GDataService *service,
                     GAsyncResult *result,
                     GetContactsData *data)
{
	EBookBackend *backend = data->backend;
	GDataFeed *feed;
	GError *gdata_error = NULL;

	g_debug (G_STRFUNC);
	feed = gdata_service_query_finish (service, result, &gdata_error);
	if (feed != NULL) {
		GList *entries = gdata_feed_get_entries (feed);
		g_debug ("Feed has %d entries", g_list_length (entries));
	}

	if (feed != NULL)
		g_object_unref (feed);

	if (!gdata_error) {
		/* Finish updating the cache */
		GTimeVal current_time;
		g_get_current_time (&current_time);
		cache_set_last_update (backend, &current_time);

		e_backend_ensure_source_status_connected (E_BACKEND (backend));
	}

	/* Thaw the cache again */
	cache_thaw (backend);

	/* Note: The operation's only marked as finished when all the
	 * process_contact_photo_cb() callbacks have been called as well. */
	data->update_complete = TRUE;
	data->gdata_error = gdata_error;
	check_get_new_contacts_finished (data);
}

static void
get_new_contacts (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	gchar *last_updated;
	GTimeVal updated;
	GDataQuery *query;
	GCancellable *cancellable;
	GetContactsData *data;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	/* Sort out update times */
	last_updated = cache_get_last_update (backend);
	g_return_if_fail (last_updated == NULL || g_time_val_from_iso8601 (last_updated, &updated) == TRUE);
	g_free (last_updated);

	/* Prevent the cache writing each change to disk individually (thawed in get_new_contacts_cb()) */
	cache_freeze (backend);

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));
	if (last_updated) {
		gdata_query_set_updated_min (query, updated.tv_sec);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	/* Query for new contacts asynchronously */
	cancellable = start_operation (backend, -1, NULL, _("Querying for updated contacts…"));

	data = g_slice_new (GetContactsData);
	data->backend = g_object_ref (backend);
	data->cancellable = g_object_ref (cancellable);
	data->gdata_error = NULL;
	data->num_contacts_pending_photos = 0;
	data->update_complete = FALSE;

	gdata_contacts_service_query_contacts_async (
		GDATA_CONTACTS_SERVICE (priv->service),
		query,
		cancellable,
		(GDataQueryProgressCallback) process_contact_cb,
		data,
		(GDestroyNotify) NULL,
		(GAsyncReadyCallback) get_new_contacts_cb,
		data);

	g_object_unref (cancellable);
	g_object_unref (query);
}

static void
process_group (GDataEntry *entry,
               guint entry_key,
               guint entry_count,
               EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	const gchar *uid, *system_group_id;
	gchar *name;
	gboolean is_deleted;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	uid = gdata_entry_get_id (entry);
	name = e_contact_sanitise_google_group_name (entry);

	system_group_id = gdata_contacts_group_get_system_group_id (GDATA_CONTACTS_GROUP (entry));
	is_deleted = gdata_contacts_group_is_deleted (GDATA_CONTACTS_GROUP (entry));

	g_rec_mutex_lock (&priv->groups_lock);

	if (system_group_id) {
		g_debug ("Processing %ssystem group %s, %s", is_deleted ? "(deleted) " : "", system_group_id, uid);

		if (is_deleted) {
			gchar *entry_id = g_hash_table_lookup (priv->system_groups_by_id, system_group_id);
			g_hash_table_remove (priv->system_groups_by_entry_id, entry_id);
			g_hash_table_remove (priv->system_groups_by_id, system_group_id);
		} else {
			gchar *entry_id, *system_group_id_dup;

			entry_id = e_contact_sanitise_google_group_id (uid);
			system_group_id_dup = g_strdup (system_group_id);

			g_hash_table_replace (priv->system_groups_by_entry_id, entry_id, system_group_id_dup);
			g_hash_table_replace (priv->system_groups_by_id, system_group_id_dup, entry_id);
		}

		g_free (name);

		/* use evolution's names for google's system groups */
		name = g_strdup (e_contact_map_google_with_evo_group (system_group_id, TRUE));

		g_warn_if_fail (name != NULL);
		if (!name)
			name = g_strdup (system_group_id);
	}

	if (is_deleted) {
		g_debug ("Processing (deleting) group %s, %s", uid, name);
		g_hash_table_remove (priv->groups_by_id, uid);
		g_hash_table_remove (priv->groups_by_name, name);

		priv->groups_changed = cache_update_group (backend, uid, NULL) || priv->groups_changed;
	} else {
		g_debug ("Processing group %s, %s", uid, name);
		g_hash_table_replace (priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (name));
		g_hash_table_replace (priv->groups_by_name, g_strdup (name), e_contact_sanitise_google_group_id (uid));

		priv->groups_changed = cache_update_group (backend, uid, name) || priv->groups_changed;
	}

	g_rec_mutex_unlock (&priv->groups_lock);

	g_free (name);
}

static void
get_groups_cb (GDataService *service,
               GAsyncResult *result,
               EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GDataFeed *feed;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	feed = gdata_service_query_finish (service, result, &gdata_error);
	if (feed != NULL) {
		GList *entries = gdata_feed_get_entries (feed);
		g_debug ("Group feed has %d entries", g_list_length (entries));
	}

	if (feed != NULL)
		g_object_unref (feed);

	if (!gdata_error) {
		/* Update the update time */
		g_rec_mutex_lock (&priv->groups_lock);
		g_get_current_time (&(priv->groups_last_update));
		g_rec_mutex_unlock (&priv->groups_lock);

		e_backend_ensure_source_status_connected (E_BACKEND (backend));
	}

	finish_operation (backend, -2, gdata_error);

	g_rec_mutex_lock (&priv->groups_lock);

	if (priv->groups_changed) {
		priv->groups_changed = FALSE;

		g_rec_mutex_unlock (&priv->groups_lock);

		/* do the update for all contacts, like with an empty cache */
		cache_set_last_update (backend, NULL);
		get_new_contacts (backend);
	} else {
		g_rec_mutex_unlock (&priv->groups_lock);
	}

	g_object_unref (backend);

	g_clear_error (&gdata_error);
}

static void
get_groups_and_update_cache_cb (GDataService *service,
				GAsyncResult *result,
				EBookBackend *backend)
{
	g_object_ref (backend);

	get_groups_cb (service, result, backend);
	get_new_contacts (backend);

	g_object_unref (backend);
}

static void
get_groups (EBookBackend *backend,
	    gboolean and_update_cache)
{
	EBookBackendGooglePrivate *priv;
	GDataQuery *query;
	GCancellable *cancellable;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	g_rec_mutex_lock (&priv->groups_lock);

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));
	if (priv->groups_last_update.tv_sec != 0 || priv->groups_last_update.tv_usec != 0) {
		gdata_query_set_updated_min (query, priv->groups_last_update.tv_sec);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	priv->groups_changed = FALSE;

	g_rec_mutex_unlock (&priv->groups_lock);

	g_object_ref (backend);

	/* Run the query asynchronously */
	cancellable = start_operation (backend, -2, NULL, _("Querying for updated groups…"));
	gdata_contacts_service_query_groups_async (
		GDATA_CONTACTS_SERVICE (priv->service),
		query,
		cancellable,
		(GDataQueryProgressCallback) process_group,
		backend,
		(GDestroyNotify) NULL,
		(GAsyncReadyCallback) (and_update_cache ? get_groups_and_update_cache_cb : get_groups_cb),
		backend);

	g_object_unref (cancellable);
	g_object_unref (query);
}

static void
get_groups_sync (EBookBackend *backend,
                 GCancellable *cancellable,
		 GError **error)
{
	EBookBackendGooglePrivate *priv;
	GDataQuery *query;
	GDataFeed *feed;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	/* Build our query, always fetch all of them */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));

	/* Run the query synchronously */
	feed = gdata_contacts_service_query_groups (
		GDATA_CONTACTS_SERVICE (priv->service),
		query,
		cancellable,
		(GDataQueryProgressCallback) process_group,
		backend,
		error);

	if (feed)
		g_object_unref (feed);

	g_object_unref (query);
}

static gchar *
create_group (EBookBackend *backend,
              const gchar *category_name,
              GError **error)
{
	EBookBackendGooglePrivate *priv;
	GDataEntry *group, *new_group;
	gchar *uid;
	const gchar *system_group_id;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	system_group_id = e_contact_map_google_with_evo_group (category_name, FALSE);
	if (system_group_id) {
		gchar *group_entry_id;

		g_rec_mutex_lock (&priv->groups_lock);
		group_entry_id = g_strdup (g_hash_table_lookup (priv->system_groups_by_id, system_group_id));
		g_rec_mutex_unlock (&priv->groups_lock);

		g_return_val_if_fail (group_entry_id != NULL, NULL);

		return group_entry_id;
	}

	group = GDATA_ENTRY (gdata_contacts_group_new (NULL));

	gdata_entry_set_title (group, category_name);
	g_debug ("Creating group %s", category_name);

	/* Insert the new group */
	new_group = GDATA_ENTRY (
		gdata_contacts_service_insert_group (
			GDATA_CONTACTS_SERVICE (priv->service),
			GDATA_CONTACTS_GROUP (group),
			NULL, error));
	g_object_unref (group);

	if (new_group == NULL)
		return NULL;

	/* Add the new group to the group mappings */
	uid = g_strdup (gdata_entry_get_id (new_group));

	g_rec_mutex_lock (&priv->groups_lock);
	g_hash_table_replace (priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (category_name));
	g_hash_table_replace (priv->groups_by_name, g_strdup (category_name), e_contact_sanitise_google_group_id (uid));
	g_rec_mutex_unlock (&priv->groups_lock);

	g_object_unref (new_group);

	/* Update the cache. */
	cache_update_group (backend, uid, category_name);

	g_debug ("...got UID %s", uid);

	return uid;
}

static gchar *
_create_group (const gchar *category_name,
               gpointer user_data,
               GError **error)
{
	return create_group (E_BOOK_BACKEND (user_data), category_name, error);
}

static void
refresh_local_cache_cb (ESource *source,
                        gpointer user_data)
{
	EBookBackend *backend = user_data;

	g_debug ("Invoking cache refresh");

	/* The TRUE means the cache update will be run immediately
	   after groups are updated */
	get_groups (backend, TRUE);
}

static void
cache_refresh_if_needed (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	gboolean is_online;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);

	is_online = e_backend_get_online (E_BACKEND (backend));

	if (!is_online || !backend_is_authorized (backend)) {
		g_debug ("We are not connected to Google%s.", (!is_online) ? " (offline mode)" : "");
		return;
	}

	if (!priv->refresh_id) {
		/* Update the cache asynchronously */
		refresh_local_cache_cb (NULL, backend);

		priv->refresh_id = e_source_refresh_add_timeout (
			e_backend_get_source (E_BACKEND (backend)),
			NULL,
			refresh_local_cache_cb,
			backend,
			NULL);
	} else {
		g_rec_mutex_lock (&priv->groups_lock);
		if (g_hash_table_size (priv->system_groups_by_id) == 0) {
			g_rec_mutex_unlock (&priv->groups_lock);
			get_groups (backend, FALSE);
		} else {
			g_rec_mutex_unlock (&priv->groups_lock);
		}
	}

	return;
}

#if !GDATA_CHECK_VERSION(0,15,0)
static void
fallback_set_proxy_uri (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GProxyResolver *proxy_resolver;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	proxy_resolver = e_book_backend_ref_proxy_resolver (backend);

	if (proxy_resolver != NULL) {
		SoupURI *proxy_uri = NULL;
		gchar **proxies;

		/* Don't worry about errors since this is a
		 * fallback function.  It works if it works. */
		proxies = g_proxy_resolver_lookup (
			proxy_resolver, URI_GET_CONTACTS, NULL, NULL);

		if (proxies != NULL && strcmp (proxies[0], "direct://") != 0) {
			proxy_uri = soup_uri_new (proxies[0]);
			g_strfreev (proxies);
		}

		if (proxy_uri != NULL) {
			gdata_service_set_proxy_uri (priv->service, proxy_uri);
			soup_uri_free (proxy_uri);
		}

		g_object_unref (proxy_resolver);
	}
}
#endif

static gboolean
connect_without_password (EBookBackend *backend,
			  GCancellable *cancellable,
			  GError **error)
{
	ESource *source;
	ESourceAuthentication *extension;
	EGDataOAuth2Authorizer *authorizer;
	gchar *method;
	gboolean is_oauth2_method;
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	source = e_backend_get_source (E_BACKEND (backend));
	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	method = e_source_authentication_dup_method (extension);
	is_oauth2_method = g_strcmp0 (method, "OAuth2") == 0;
	g_free (method);

	/* Make sure we have the GDataService configured
	 * before requesting authorization. */

	if (priv->authorizer == NULL) {
		authorizer = e_gdata_oauth2_authorizer_new (source);
		priv->authorizer = GDATA_AUTHORIZER (authorizer);
	}

	if (priv->service == NULL) {
		GDataContactsService *contacts_service;

		contacts_service =
			gdata_contacts_service_new (priv->authorizer);
		priv->service = GDATA_SERVICE (contacts_service);

#if GDATA_CHECK_VERSION(0,15,0)
		/* proxy-resolver was added in 0.15.0.
		 * (https://bugzilla.gnome.org/709758) */
		e_binding_bind_property (
			backend, "proxy-resolver",
			priv->service, "proxy-resolver",
			G_BINDING_SYNC_CREATE);
#else
		/* XXX The fallback approach doesn't listen for proxy
		 *     setting changes, but really how often do proxy
		 *     settings change? */
		fallback_set_proxy_uri (backend);
#endif
	}

	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background. */
	if (is_oauth2_method)
		return TRUE;

	/* Otherwise it's up to us to obtain an OAuth 2 token. */
	return FALSE;
}

typedef enum {
	LEAVE_PHOTO,
	ADD_PHOTO,
	REMOVE_PHOTO,
	UPDATE_PHOTO,
} PhotoOperation;

static PhotoOperation
pick_photo_operation (EContact *old_contact,
                      EContact *new_contact)
{
	EContactPhoto *old_photo;
	EContactPhoto *new_photo;
	gboolean have_old_photo;
	gboolean have_new_photo;
	PhotoOperation photo_operation = LEAVE_PHOTO;

	old_photo = e_contact_get (old_contact, E_CONTACT_PHOTO);
	new_photo = e_contact_get (new_contact, E_CONTACT_PHOTO);

	have_old_photo =
		(old_photo != NULL) &&
		(old_photo->type == E_CONTACT_PHOTO_TYPE_INLINED);

	have_new_photo =
		(new_photo != NULL) &&
		(new_photo->type == E_CONTACT_PHOTO_TYPE_INLINED);

	if (!have_old_photo && have_new_photo)
		photo_operation = ADD_PHOTO;

	if (have_old_photo && !have_new_photo)
		photo_operation = REMOVE_PHOTO;

	if (have_old_photo && have_new_photo) {
		guchar *old_data;
		guchar *new_data;
		gsize old_length;
		gsize new_length;
		gboolean changed;

		old_data = old_photo->data.inlined.data;
		new_data = new_photo->data.inlined.data;

		old_length = old_photo->data.inlined.length;
		new_length = new_photo->data.inlined.length;

		changed =
			(old_length != new_length) ||
			(memcmp (old_data, new_data, old_length) != 0);

		if (changed)
			photo_operation = UPDATE_PHOTO;
	}

	if (old_photo != NULL)
		e_contact_photo_free (old_photo);

	if (new_photo != NULL)
		e_contact_photo_free (new_photo);

	return photo_operation;
}

static GDataEntry *
update_contact_photo (GDataContactsContact *contact,
                      GDataContactsService *service,
                      EContactPhoto *photo,
                      GCancellable *cancellable,
                      GError **error)
{
	GDataAuthorizationDomain *authorization_domain;
	GDataEntry *new_contact = NULL;
	const gchar *content_type;
	const guint8 *photo_data;
	gsize photo_length;
	gboolean success;

	authorization_domain =
		gdata_contacts_service_get_primary_authorization_domain ();

	if (photo != NULL) {
		photo_data = (guint8 *) photo->data.inlined.data;
		photo_length = photo->data.inlined.length;
		content_type = photo->data.inlined.mime_type;
	} else {
		photo_data = NULL;
		photo_length = 0;
		content_type = NULL;
	}

	success = gdata_contacts_contact_set_photo (
		contact, service,
		photo_data, photo_length,
		content_type,
		cancellable, error);

	if (success) {
		/* Setting the photo changes the contact's ETag,
		 * so query for the contact to obtain its new ETag. */
		new_contact = gdata_service_query_single_entry (
			GDATA_SERVICE (service),
			authorization_domain,
			gdata_entry_get_id (GDATA_ENTRY (contact)),
			NULL, GDATA_TYPE_CONTACTS_CONTACT,
			cancellable, error);
	}

	return new_contact;
}

static void
google_cancel_all_operations (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GHashTableIter iter;
	gpointer opid_ptr;
	GCancellable *cancellable;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);

	if (!priv->cancellables)
		return;

	/* Cancel all active operations */
	g_hash_table_iter_init (&iter, priv->cancellables);
	while (g_hash_table_iter_next (&iter, &opid_ptr, (gpointer *) &cancellable)) {
		g_cancellable_cancel (cancellable);
	}
}

static void
e_book_backend_google_notify_online_cb (EBookBackend *backend,
                                        GParamSpec *pspec)
{
	EBookBackendGooglePrivate *priv;
	ESource *source;
	gboolean is_online;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);

	is_online = e_backend_get_online (E_BACKEND (backend));
	source = e_backend_get_source (E_BACKEND (backend));

	if (is_online && e_book_backend_is_opened (backend)) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

		if (connect_without_password (backend, NULL, NULL)) {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);

			e_book_backend_set_writable (backend, TRUE);
			cache_refresh_if_needed (backend);
		} else {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

			e_backend_schedule_credentials_required (E_BACKEND (backend), E_SOURCE_CREDENTIALS_REASON_REQUIRED,
				NULL, 0, NULL, NULL, G_STRFUNC);
		}
	} else {
		/* Going offline, so cancel all running operations */
		google_cancel_all_operations (backend);

		/* Mark the book as unwriteable if we're going offline,
		 * but don't do the inverse when we go online;
		 * e_book_backend_google_authenticate_user() will mark us
		 * as writeable again once the user's authenticated again. */
		e_book_backend_set_writable (backend, FALSE);

		if (e_source_get_connection_status (source) != E_SOURCE_CONNECTION_STATUS_DISCONNECTED)
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

		/* We can free our service. */
		g_clear_object (&priv->service);
	}
}

static void
book_backend_google_dispose (GObject *object)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (object);

	g_debug (G_STRFUNC);

	/* Cancel all outstanding operations */
	google_cancel_all_operations (E_BOOK_BACKEND (object));

	if (priv->refresh_id > 0) {
		e_source_refresh_remove_timeout (
			e_backend_get_source (E_BACKEND (object)),
			priv->refresh_id);
		priv->refresh_id = 0;
	}

	g_clear_object (&priv->service);
	g_clear_object (&priv->authorizer);
	g_clear_object (&priv->cache);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
book_backend_google_finalize (GObject *object)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (object);

	g_debug (G_STRFUNC);

	if (priv->cancellables) {
		g_hash_table_destroy (priv->groups_by_id);
		g_hash_table_destroy (priv->groups_by_name);
		g_hash_table_destroy (priv->system_groups_by_entry_id);
		g_hash_table_destroy (priv->system_groups_by_id);
		g_hash_table_destroy (priv->cancellables);
	}

	g_mutex_clear (&priv->cache_lock);
	g_rec_mutex_clear (&priv->groups_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static gchar *
book_backend_google_get_backend_property (EBookBackend *backend,
                                            const gchar *prop_name)
{
	g_debug (G_STRFUNC);

	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strdup ("net,do-initial-query,contact-lists,refresh-supported");

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup ("");

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		return g_strjoin (
			",",
			e_contact_field_name (E_CONTACT_UID),
			e_contact_field_name (E_CONTACT_REV),
			e_contact_field_name (E_CONTACT_FULL_NAME),

			e_contact_field_name (E_CONTACT_EMAIL_1),
			e_contact_field_name (E_CONTACT_EMAIL_2),
			e_contact_field_name (E_CONTACT_EMAIL_3),
			e_contact_field_name (E_CONTACT_EMAIL_4),
			e_contact_field_name (E_CONTACT_EMAIL),

			e_contact_field_name (E_CONTACT_ADDRESS_LABEL_HOME),
			e_contact_field_name (E_CONTACT_ADDRESS_LABEL_WORK),
			e_contact_field_name (E_CONTACT_ADDRESS_LABEL_OTHER),

			e_contact_field_name (E_CONTACT_IM_AIM),
			e_contact_field_name (E_CONTACT_IM_JABBER),
			e_contact_field_name (E_CONTACT_IM_YAHOO),
			e_contact_field_name (E_CONTACT_IM_MSN),
			e_contact_field_name (E_CONTACT_IM_ICQ),
			e_contact_field_name (E_CONTACT_IM_SKYPE),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK),
			/* current implementation uses http://schemas.google.com/g/2005# namespace
			 * see google-utils:gdata_gd_im_address_from_attribute
			 *
			 * google namespace does not support:
			 * e_contact_field_name (E_CONTACT_IM_TWITTER),
			 * e_contact_field_name (E_CONTACT_IM_GADUGADU),
			 * e_contact_field_name (E_CONTACT_IM_GROUPWISE),
			 * see https://developers.google.com/gdata/docs/2.0/elements#gdIm
			 * see google-utils:is_known_google_im_protocol
			*/

			e_contact_field_name (E_CONTACT_ADDRESS),
			e_contact_field_name (E_CONTACT_ADDRESS_HOME),
			e_contact_field_name (E_CONTACT_ADDRESS_WORK),
			e_contact_field_name (E_CONTACT_ADDRESS_OTHER),
			e_contact_field_name (E_CONTACT_NAME),
			e_contact_field_name (E_CONTACT_GIVEN_NAME),
			e_contact_field_name (E_CONTACT_FAMILY_NAME),
			e_contact_field_name (E_CONTACT_PHONE_HOME),
			e_contact_field_name (E_CONTACT_PHONE_HOME_FAX),
			e_contact_field_name (E_CONTACT_PHONE_BUSINESS),
			e_contact_field_name (E_CONTACT_PHONE_BUSINESS_FAX),
			e_contact_field_name (E_CONTACT_PHONE_MOBILE),
			e_contact_field_name (E_CONTACT_PHONE_PAGER),
			e_contact_field_name (E_CONTACT_PHONE_ASSISTANT),
			e_contact_field_name (E_CONTACT_PHONE_BUSINESS_2),
			e_contact_field_name (E_CONTACT_PHONE_CALLBACK),
			e_contact_field_name (E_CONTACT_PHONE_CAR),
			e_contact_field_name (E_CONTACT_PHONE_COMPANY),
			e_contact_field_name (E_CONTACT_PHONE_HOME_2),
			e_contact_field_name (E_CONTACT_PHONE_ISDN),
			e_contact_field_name (E_CONTACT_PHONE_OTHER),
			e_contact_field_name (E_CONTACT_PHONE_OTHER_FAX),
			e_contact_field_name (E_CONTACT_PHONE_PRIMARY),
			e_contact_field_name (E_CONTACT_PHONE_RADIO),
			e_contact_field_name (E_CONTACT_PHONE_TELEX),
			e_contact_field_name (E_CONTACT_PHONE_TTYTDD),
			e_contact_field_name (E_CONTACT_TEL),

			e_contact_field_name (E_CONTACT_IM_AIM_HOME_1),
			e_contact_field_name (E_CONTACT_IM_AIM_HOME_2),
			e_contact_field_name (E_CONTACT_IM_AIM_HOME_3),
			e_contact_field_name (E_CONTACT_IM_AIM_WORK_1),
			e_contact_field_name (E_CONTACT_IM_AIM_WORK_2),
			e_contact_field_name (E_CONTACT_IM_AIM_WORK_3),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_HOME_1),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_HOME_2),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_HOME_3),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_WORK_1),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_WORK_2),
			e_contact_field_name (E_CONTACT_IM_GROUPWISE_WORK_3),
			e_contact_field_name (E_CONTACT_IM_JABBER_HOME_1),
			e_contact_field_name (E_CONTACT_IM_JABBER_HOME_2),
			e_contact_field_name (E_CONTACT_IM_JABBER_HOME_3),
			e_contact_field_name (E_CONTACT_IM_JABBER_WORK_1),
			e_contact_field_name (E_CONTACT_IM_JABBER_WORK_2),
			e_contact_field_name (E_CONTACT_IM_JABBER_WORK_3),
			e_contact_field_name (E_CONTACT_IM_YAHOO_HOME_1),
			e_contact_field_name (E_CONTACT_IM_YAHOO_HOME_2),
			e_contact_field_name (E_CONTACT_IM_YAHOO_HOME_3),
			e_contact_field_name (E_CONTACT_IM_YAHOO_WORK_1),
			e_contact_field_name (E_CONTACT_IM_YAHOO_WORK_2),
			e_contact_field_name (E_CONTACT_IM_YAHOO_WORK_3),
			e_contact_field_name (E_CONTACT_IM_MSN_HOME_1),
			e_contact_field_name (E_CONTACT_IM_MSN_HOME_2),
			e_contact_field_name (E_CONTACT_IM_MSN_HOME_3),
			e_contact_field_name (E_CONTACT_IM_MSN_WORK_1),
			e_contact_field_name (E_CONTACT_IM_MSN_WORK_2),
			e_contact_field_name (E_CONTACT_IM_MSN_WORK_3),
			e_contact_field_name (E_CONTACT_IM_ICQ_HOME_1),
			e_contact_field_name (E_CONTACT_IM_ICQ_HOME_2),
			e_contact_field_name (E_CONTACT_IM_ICQ_HOME_3),
			e_contact_field_name (E_CONTACT_IM_ICQ_WORK_1),
			e_contact_field_name (E_CONTACT_IM_ICQ_WORK_2),
			e_contact_field_name (E_CONTACT_IM_ICQ_WORK_3),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_HOME_1),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_HOME_2),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_HOME_3),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_WORK_1),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_WORK_2),
			e_contact_field_name (E_CONTACT_IM_GADUGADU_WORK_3),
			e_contact_field_name (E_CONTACT_IM_SKYPE_HOME_1),
			e_contact_field_name (E_CONTACT_IM_SKYPE_HOME_2),
			e_contact_field_name (E_CONTACT_IM_SKYPE_HOME_3),
			e_contact_field_name (E_CONTACT_IM_SKYPE_WORK_1),
			e_contact_field_name (E_CONTACT_IM_SKYPE_WORK_2),
			e_contact_field_name (E_CONTACT_IM_SKYPE_WORK_3),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_HOME_1),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_HOME_2),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_HOME_3),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_WORK_1),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_WORK_2),
			e_contact_field_name (E_CONTACT_IM_GOOGLE_TALK_WORK_3),

			e_contact_field_name (E_CONTACT_SIP),
			e_contact_field_name (E_CONTACT_ORG),
			e_contact_field_name (E_CONTACT_ORG_UNIT),
			e_contact_field_name (E_CONTACT_TITLE),
			e_contact_field_name (E_CONTACT_ROLE),
			e_contact_field_name (E_CONTACT_HOMEPAGE_URL),
			e_contact_field_name (E_CONTACT_BLOG_URL),
			e_contact_field_name (E_CONTACT_BIRTH_DATE),
			e_contact_field_name (E_CONTACT_ANNIVERSARY),
			e_contact_field_name (E_CONTACT_NOTE),
			e_contact_field_name (E_CONTACT_PHOTO),
			e_contact_field_name (E_CONTACT_CATEGORIES),
#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
			e_contact_field_name (E_CONTACT_CATEGORY_LIST),
			e_contact_field_name (E_CONTACT_FILE_AS),
#else
			e_contact_field_name (E_CONTACT_CATEGORY_LIST),
#endif
#else
			e_contact_field_name (E_CONTACT_CATEGORY_LIST),
#endif
			e_contact_field_name (E_CONTACT_NICKNAME),
			NULL);
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_google_parent_class)->
		get_backend_property (backend, prop_name);
}

static gboolean
book_backend_google_open_sync (EBookBackend *backend,
                               GCancellable *cancellable,
                               GError **error)
{
	EBookBackendGooglePrivate *priv;
	gboolean is_online;
	gboolean success = TRUE;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_debug (G_STRFUNC);

	if (priv->cancellables && backend_is_authorized (backend))
		return TRUE;

	/* Set up our object */
	if (priv->cancellables == NULL) {
		priv->groups_by_id = g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_free);
		priv->groups_by_name = g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_free);
		priv->system_groups_by_id = g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_free);
		/* shares keys and values with system_groups_by_id */
		priv->system_groups_by_entry_id = g_hash_table_new (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal);
		priv->cancellables = g_hash_table_new_full (
			(GHashFunc) g_direct_hash,
			(GEqualFunc) g_direct_equal,
			(GDestroyNotify) NULL,
			(GDestroyNotify) g_object_unref);
	}

	cache_init (backend);

	/* Set up ready to be interacted with */
	is_online = e_backend_get_online (E_BACKEND (backend));
	e_book_backend_set_writable (backend, FALSE);

	if (is_online) {
		ESource *source = e_backend_get_source (E_BACKEND (backend));

		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

		success = connect_without_password (backend, cancellable, error);
		if (success) {
			GError *local_error = NULL;

			/* Refresh the authorizer.  This may block. */
			success = gdata_authorizer_refresh_authorization (
				priv->authorizer, cancellable, &local_error);

			if (success) {
				e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
			} else {
				GError *local_error2 = NULL;

				e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

				if (local_error && !e_backend_credentials_required_sync (E_BACKEND (backend), E_SOURCE_CREDENTIALS_REASON_ERROR,
					NULL, 0, local_error, cancellable, &local_error2)) {
					g_warning ("%s: Failed to call credentials required: %s", G_STRFUNC, local_error2 ? local_error2->message : "Unknown error");
				}

				g_clear_error (&local_error2);

				if (local_error)
					g_propagate_error (error, local_error);
			}
		} else {
			GError *local_error = NULL;

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

			if (!e_backend_credentials_required_sync (E_BACKEND (backend), E_SOURCE_CREDENTIALS_REASON_REQUIRED,
				NULL, 0, NULL, cancellable, &local_error)) {
				g_warning ("%s: Failed to call credentials required: %s", G_STRFUNC, local_error ? local_error->message : "Unknown error");
			}

			g_clear_error (&local_error);
		}
	}

	if (is_online && backend_is_authorized (backend)) {
		e_book_backend_set_writable (backend, TRUE);
		cache_refresh_if_needed (backend);
	}

	return success;
}

static gboolean
book_backend_google_create_contacts_sync (EBookBackend *backend,
                                          const gchar * const *vcards,
                                          GQueue *out_contacts,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendGooglePrivate *priv;
	EContactPhoto *photo = NULL;
	EContact *contact;
	GDataEntry *entry;
	GDataContactsContact *new_contact;
	gchar *xml;
	gboolean success = TRUE;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	/* We make the assumption that the vCard list we're passed is always
	 * exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This simplifies the logic. */
	if (g_strv_length ((gchar **) vcards) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk additions"));
		return FALSE;
	}

	g_debug (G_STRFUNC);

	g_debug ("Creating: %s", vcards[0]);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE,
			e_client_error_to_string (
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE));
		return FALSE;
	}

	g_warn_if_fail (backend_is_authorized (backend));

	g_rec_mutex_lock (&priv->groups_lock);

	/* Ensure the system groups have been fetched. */
	if (g_hash_table_size (priv->system_groups_by_id) == 0)
		get_groups_sync (backend, cancellable, NULL);

	/* Build the GDataEntry from the vCard */
	contact = e_contact_new_from_vcard (vcards[0]);
	entry = gdata_entry_new_from_e_contact (
		contact,
		priv->groups_by_name,
		priv->system_groups_by_id,
		_create_group, backend);
	g_object_unref (contact);

	g_rec_mutex_unlock (&priv->groups_lock);

	/* Debug XML output */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	g_debug ("new entry with xml: %s", xml);
	g_free (xml);

	new_contact = gdata_contacts_service_insert_contact (
		GDATA_CONTACTS_SERVICE (priv->service),
		GDATA_CONTACTS_CONTACT (entry),
		cancellable, &gdata_error);

	if (new_contact == NULL) {
		success = FALSE;
		goto exit;
	}

	/* Add a photo for the new contact, if appropriate.  This has to
	 * be done before we finish the contact creation operation so we
	 * can update the EContact with the photo data and ETag. */
	photo = g_object_steal_data (G_OBJECT (entry), "photo");
	if (photo != NULL) {
		GDataEntry *updated_entry;
		gchar *xml;

		updated_entry = update_contact_photo (
			new_contact,
			GDATA_CONTACTS_SERVICE (priv->service),
			photo, cancellable, &gdata_error);

		/* Sanity check. */
		g_return_val_if_fail (
			((updated_entry != NULL) && (gdata_error == NULL)) ||
			((updated_entry == NULL) && (gdata_error != NULL)),
			FALSE);

		if (gdata_error != NULL) {
			g_debug (
				"Uploading contact photo "
				"for '%s' failed: %s",
				gdata_entry_get_id (GDATA_ENTRY (new_contact)),
				gdata_error->message);
			success = FALSE;
			goto exit;
		}

		/* Output debug XML */
		xml = gdata_parsable_get_xml (
			GDATA_PARSABLE (updated_entry));
		g_debug ("After re-querying:\n%s", xml);
		g_free (xml);

		g_object_unref (new_contact);
		new_contact = GDATA_CONTACTS_CONTACT (updated_entry);

		/* Store the photo on the final GDataContactsContact
		 * object so it makes it into the cache. */
		g_object_set_data_full (
			G_OBJECT (new_contact), "photo", photo,
			(GDestroyNotify) e_contact_photo_free);
		photo = NULL;
	}

	contact = cache_add_contact (backend, GDATA_ENTRY (new_contact));
	if (contact) {
		g_queue_push_tail (out_contacts, g_object_ref (contact));
		g_object_unref (contact);
	}

exit:
	g_clear_object (&entry);
	g_clear_object (&new_contact);

	if (photo != NULL)
		e_contact_photo_free (photo);

	if (gdata_error != NULL) {
		g_warn_if_fail (success == FALSE);
		data_book_error_from_gdata_error (error, gdata_error);
		g_error_free (gdata_error);
	} else {
		e_backend_ensure_source_status_connected (E_BACKEND (backend));
	}

	return success;
}

static gboolean
book_backend_google_modify_contacts_sync (EBookBackend *backend,
                                          const gchar * const *vcards,
                                          GQueue *out_contacts,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendGooglePrivate *priv;
	GDataAuthorizationDomain *authorization_domain;
	EContact *contact, *cached_contact;
	PhotoOperation photo_operation;
	EContactPhoto *photo;
	GDataEntry *entry = NULL;
	GDataEntry *new_contact;
	const gchar *uid;
	gboolean success = TRUE;
	gchar *xml;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	authorization_domain =
		gdata_contacts_service_get_primary_authorization_domain ();

	g_debug (G_STRFUNC);

	g_debug ("Updating: %s", vcards[0]);

	/* We make the assumption that the vCard list we're passed is
	 * always exactly one element long, since we haven't specified
	 * "bulk-modifies" in our static capability list.  This is because
	 * there is no clean way to roll back changes in case of an error. */
	if (g_strv_length ((gchar **) vcards) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk modifications"));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE,
			e_client_error_to_string (
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE));
		return FALSE;
	}

	g_warn_if_fail (backend_is_authorized (backend));

	/* Get the new contact and its UID. */
	contact = e_contact_new_from_vcard (vcards[0]);
	uid = e_contact_get (contact, E_CONTACT_UID);

	/* Get the old cached contact with the same UID,
	 * and its associated GDataEntry. */
	cached_contact = cache_get_contact (backend, uid, &entry);

	if (cached_contact == NULL) {
		g_debug (
			"Modifying contact failed: "
			"Contact with uid %s not found in cache.", uid);
		g_object_unref (contact);

		g_set_error_literal (
			error, E_BOOK_CLIENT_ERROR,
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
			e_book_client_error_to_string (
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
		return FALSE;
	}

	g_rec_mutex_lock (&priv->groups_lock);

	/* Ensure the system groups have been fetched. */
	if (g_hash_table_size (priv->system_groups_by_id) == 0)
		get_groups_sync (backend, cancellable, NULL);

	/* Update the old GDataEntry from the new contact. */
	gdata_entry_update_from_e_contact (
		entry, contact, FALSE,
		priv->groups_by_name,
		priv->system_groups_by_id,
		_create_group, backend);

	g_rec_mutex_unlock (&priv->groups_lock);

	/* Output debug XML */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	g_debug ("Before:\n%s", xml);
	g_free (xml);

	photo = g_object_steal_data (G_OBJECT (entry), "photo");

	/* Update the contact's photo. We can't rely on the ETags at this
	 * point, as the ETag in @contact may be out of sync with the photo
	 * in the EContact (since the photo may have been updated).
	 * Consequently, after updating @entry its ETag may also be out of
	 * sync with its attached photo data.  This means that we have to
	 * detect whether the photo has changed by comparing the photo data
	 * itself, which is guaranteed to be in sync between @contact and
	 * @entry. */
	photo_operation = pick_photo_operation (cached_contact, contact);

	/* Sanity check the photo operation. */
	switch (photo_operation) {
		case LEAVE_PHOTO:
			break;

		case ADD_PHOTO:
		case UPDATE_PHOTO:
			g_return_val_if_fail (photo != NULL, FALSE);
			break;

		case REMOVE_PHOTO:
			g_return_val_if_fail (photo == NULL, FALSE);
			break;

		default:
			g_return_val_if_reached (FALSE);
	}

	g_clear_object (&cached_contact);
	g_clear_object (&contact);

	new_contact = gdata_service_update_entry (
		priv->service,
		authorization_domain,
		entry,
		cancellable, &gdata_error);

	if (new_contact == NULL) {
		g_debug (
			"Modifying contact failed: %s",
			gdata_error->message);
		success = FALSE;
		goto exit;
	}

	/* Output debug XML */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (new_contact));
	g_debug ("After:\n%s", xml);
	g_free (xml);

	/* Add a photo for the new contact, if appropriate. This has to be
	 * done before we respond to the contact creation operation so that
	 * we can update the EContact with the photo data and ETag. */
	if (photo_operation != LEAVE_PHOTO) {
		GDataEntry *updated_entry;

		updated_entry = update_contact_photo (
			GDATA_CONTACTS_CONTACT (new_contact),
			GDATA_CONTACTS_SERVICE (priv->service),
			photo, cancellable, &gdata_error);

		/* Sanity check. */
		g_return_val_if_fail (
			((updated_entry != NULL) && (gdata_error == NULL)) ||
			((updated_entry == NULL) && (gdata_error != NULL)),
			FALSE);

		if (gdata_error != NULL) {
			g_debug (
				"Uploading contact photo "
				"for '%s' failed: %s",
				gdata_entry_get_id (new_contact),
				gdata_error->message);
			success = FALSE;
			goto exit;
		}

		/* Output debug XML */
		xml = gdata_parsable_get_xml (
			GDATA_PARSABLE (updated_entry));
		g_debug ("After re-querying:\n%s", xml);
		g_free (xml);

		g_object_unref (new_contact);
		new_contact = updated_entry;
	}

	/* Store the photo on the final GDataEntry
	 * object so it makes it to the cache. */
	if (photo != NULL) {
		g_object_set_data_full (
			G_OBJECT (new_contact), "photo", photo,
			(GDestroyNotify) e_contact_photo_free);
		photo = NULL;
	} else {
		g_object_set_data (
			G_OBJECT (new_contact), "photo", NULL);
	}

	contact = cache_add_contact (backend, new_contact);
	if (contact) {
		g_queue_push_tail (out_contacts, g_object_ref (contact));
		g_object_unref (contact);
	}

exit:
	g_clear_object (&entry);
	g_clear_object (&new_contact);

	if (photo != NULL)
		e_contact_photo_free (photo);

	if (gdata_error != NULL) {
		g_warn_if_fail (success == FALSE);
		data_book_error_from_gdata_error (error, gdata_error);
		g_error_free (gdata_error);
	} else {
		e_backend_ensure_source_status_connected (E_BACKEND (backend));
	}

	return success;
}

static gboolean
book_backend_google_remove_contacts_sync (EBookBackend *backend,
                                          const gchar *const *uids,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendGooglePrivate *priv;
	GDataAuthorizationDomain *authorization_domain;
	GDataEntry *entry = NULL;
	EContact *cached_contact;
	gboolean success;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	authorization_domain =
		gdata_contacts_service_get_primary_authorization_domain ();

	g_debug (G_STRFUNC);

	/* We make the assumption that the ID list we're passed is always
	 * exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list.  This simplifies the logic. */
	if (g_strv_length ((gchar **) uids) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk removals"));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE,
			e_client_error_to_string (
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE));
		return FALSE;
	}

	g_warn_if_fail (backend_is_authorized (backend));

	/* Get the contact and associated GDataEntry from the cache */
	cached_contact = cache_get_contact (backend, uids[0], &entry);

	if (cached_contact == NULL) {
		g_set_error_literal (
			error, E_BOOK_CLIENT_ERROR,
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
			e_book_client_error_to_string (
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
		return FALSE;
	}

	g_object_unref (cached_contact);

	/* Remove the contact from the cache */
	cache_remove_contact (backend, uids[0]);

	success = gdata_service_delete_entry (
		priv->service,
		authorization_domain, entry,
		cancellable, &gdata_error);

	g_object_unref (entry);

	if (gdata_error != NULL) {
		g_warn_if_fail (success == FALSE);
		data_book_error_from_gdata_error (error, gdata_error);
		g_error_free (gdata_error);
	} else {
		e_backend_ensure_source_status_connected (E_BACKEND (backend));
	}

	return success;
}

static EContact *
book_backend_google_get_contact_sync (EBookBackend *backend,
                                      const gchar *uid,
                                      GCancellable *cancellable,
                                      GError **error)
{
	EContact *contact;

	g_debug (G_STRFUNC);

	/* Get the contact */
	contact = cache_get_contact (backend, uid, NULL);
	if (contact == NULL) {
		g_set_error_literal (
			error, E_BOOK_CLIENT_ERROR,
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
			e_book_client_error_to_string (
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
	}

	return contact;
}

static gboolean
book_backend_google_get_contact_list_sync (EBookBackend *backend,
                                           const gchar *query,
                                           GQueue *out_contacts,
                                           GCancellable *cancellable,
                                           GError **error)
{
	EBookBackendSExp *sexp;
	GQueue queue = G_QUEUE_INIT;

	g_debug (G_STRFUNC);

	sexp = e_book_backend_sexp_new (query);

	/* Get all contacts */
	cache_get_contacts (backend, &queue);

	while (!g_queue_is_empty (&queue)) {
		EContact *contact;

		contact = g_queue_pop_head (&queue);

		/* If the search expression matches the contact,
		 * include it in the search results. */
		if (e_book_backend_sexp_match_contact (sexp, contact)) {
			g_object_ref (contact);
			g_queue_push_tail (out_contacts, contact);
		}

		g_object_unref (contact);
	}

	g_object_unref (sexp);

	return TRUE;
}

static void
book_backend_google_start_view (EBookBackend *backend,
                                EDataBookView *bookview)
{
	GQueue queue = G_QUEUE_INIT;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (bookview));

	g_debug (G_STRFUNC);

	e_data_book_view_notify_progress (bookview, -1, _("Loading…"));

	/* Ensure that we're ready to support a view */
	cache_refresh_if_needed (backend);

	/* Get the contacts */
	cache_get_contacts (backend, &queue);
	g_debug (
		"%d contacts found in cache",
		g_queue_get_length (&queue));

	/* Notify the view that all the contacts have changed (i.e. been added) */
	while (!g_queue_is_empty (&queue)) {
		EContact *contact;

		contact = g_queue_pop_head (&queue);
		e_data_book_view_notify_update (bookview, contact);
		g_object_unref (contact);
	}

	/* This function frees the GError passed to it. */
	e_data_book_view_notify_complete (bookview, error);
}

static void
book_backend_google_stop_view (EBookBackend *backend,
                               EDataBookView *bookview)
{
	g_debug (G_STRFUNC);
}

static gboolean
book_backend_google_refresh_sync (EBookBackend *backend,
				  GCancellable *cancellable,
				  GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend), FALSE);

	/* get only changes, it's not needed to redownload whole cache */
	get_new_contacts (backend);

	return TRUE;
}

static ESourceAuthenticationResult
book_backend_google_authenticate_sync (EBackend *backend,
				       const ENamedParameters *credentials,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors,
				       GCancellable *cancellable,
				       GError **error)
{
	EBookBackend *book_backend = E_BOOK_BACKEND (backend);
	EBookBackendGooglePrivate *priv;
	ESourceAuthenticationResult result;
	EGDataOAuth2Authorizer *authorizer;
	GError *local_error = NULL;

	g_debug (G_STRFUNC);

	/* We should not have gotten here if we're offline. */
	g_return_val_if_fail (e_backend_get_online (backend), E_SOURCE_AUTHENTICATION_ERROR);

	priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	g_return_val_if_fail (E_IS_GDATA_OAUTH2_AUTHORIZER (priv->authorizer), E_SOURCE_AUTHENTICATION_ERROR);

	authorizer = E_GDATA_OAUTH2_AUTHORIZER (priv->authorizer);
	e_gdata_oauth2_authorizer_set_credentials (authorizer, credentials);

	get_groups_sync (E_BOOK_BACKEND (backend), cancellable, &local_error);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		if (backend_is_authorized (book_backend)) {
			e_book_backend_set_writable (book_backend, TRUE);
			cache_refresh_if_needed (book_backend);
		}
	} else if (g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED)) {
		if (!e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD))
			result = E_SOURCE_AUTHENTICATION_REQUIRED;
		else
			result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_error (&local_error);
	} else {
		g_propagate_error (error, local_error);
		result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	return result;
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendGooglePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = book_backend_google_dispose;
	object_class->finalize = book_backend_google_finalize;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = book_backend_google_authenticate_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (class);
	book_backend_class->get_backend_property = book_backend_google_get_backend_property;
	book_backend_class->open_sync = book_backend_google_open_sync;
	book_backend_class->create_contacts_sync = book_backend_google_create_contacts_sync;
	book_backend_class->modify_contacts_sync = book_backend_google_modify_contacts_sync;
	book_backend_class->remove_contacts_sync = book_backend_google_remove_contacts_sync;
	book_backend_class->get_contact_sync = book_backend_google_get_contact_sync;
	book_backend_class->get_contact_list_sync = book_backend_google_get_contact_list_sync;
	book_backend_class->start_view = book_backend_google_start_view;
	book_backend_class->stop_view = book_backend_google_stop_view;
	book_backend_class->refresh_sync = book_backend_google_refresh_sync;
}

static void
e_book_backend_google_init (EBookBackendGoogle *backend)
{
	g_debug (G_STRFUNC);

	backend->priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_mutex_init (&backend->priv->cache_lock);
	g_rec_mutex_init (&backend->priv->groups_lock);

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_google_notify_online_cb), NULL);
}


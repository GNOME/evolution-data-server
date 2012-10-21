/* e-book-backend-google.c - Google contact backendy.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010, 2011 Philip Withnall
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
 *         Philip Withnall <philip@tecnocode.co.uk>
 */

#include <config.h>
#include <string.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <gdata/gdata.h>

#include "e-book-backend-google.h"
#include "e-book-google-utils.h"

#ifdef HAVE_GOA
#include "e-gdata-goa-authorizer.h"
#endif

#define E_BOOK_BACKEND_GOOGLE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGooglePrivate))

#define CLIENT_ID "evolution-client-0.1.0"

#define URI_GET_CONTACTS "https://www.google.com/m8/feeds/contacts/default/full"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

/* Forward Declarations */
static void	e_book_backend_google_source_authenticator_init
				(ESourceAuthenticatorInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EBookBackendGoogle,
	e_book_backend_google,
	E_TYPE_BOOK_BACKEND,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_book_backend_google_source_authenticator_init))

struct _EBookBackendGooglePrivate {
	GList *bookviews;

	EBookBackendCache *cache;

	/* Mapping from group ID to (human readable) group name */
	GHashTable *groups_by_id;
	/* Mapping from (human readable) group name to group ID */
	GHashTable *groups_by_name;
	/* Mapping system_group_id to entry ID */
	GHashTable *system_groups_by_id;
	/* Mapping entry ID to system_group_id */
	GHashTable *system_groups_by_entry_id;
	/* Time when the groups were last queried */
	GTimeVal last_groups_update;

	GDataAuthorizer *authorizer;
	GDataService *service;
	EProxy *proxy;

	guint refresh_id;

	/* Map of active opids to GCancellables */
	GHashTable *cancellables;
};

gboolean __e_book_backend_google_debug__;
#define __debug__(...) (__e_book_backend_google_debug__ ? g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__) : (void) 0)

static void data_book_error_from_gdata_error (GError **dest_err, const GError *error);

static void
migrate_cache (EBookBackendCache *cache)
{
	const gchar *version;
	const gchar *version_key = "book-cache-version";

	g_return_if_fail (cache != NULL);

	version = e_file_cache_get_object (E_FILE_CACHE (cache), version_key);
	if (!version || atoi (version) < 1) {
		/* not versioned yet, dump the cache and reload it from a server */
		e_file_cache_clean (E_FILE_CACHE (cache));
		e_file_cache_add_object (E_FILE_CACHE (cache), version_key, "1");
	}
}

static void
cache_init (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	const gchar *cache_dir;
	gchar *filename;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	cache_dir = e_book_backend_get_cache_dir (backend);
	filename = g_build_filename (cache_dir, "cache.xml", NULL);
	priv->cache = e_book_backend_cache_new (filename);
	g_free (filename);

	migrate_cache (priv->cache);
}

static EContact *
cache_add_contact (EBookBackend *backend,
                   GDataEntry *entry)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	contact = e_contact_new_from_gdata_entry (entry, priv->groups_by_id, priv->system_groups_by_entry_id);
	e_contact_add_gdata_entry_xml (contact, entry);
	e_book_backend_cache_add_contact (priv->cache, contact);
	e_contact_remove_gdata_entry_xml (contact);

	return contact;
}

static gboolean
cache_remove_contact (EBookBackend *backend,
                      const gchar *uid)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	return e_book_backend_cache_remove_contact (priv->cache, uid);
}

static gboolean
cache_has_contact (EBookBackend *backend,
                   const gchar *uid)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	return e_book_backend_cache_check_contact (priv->cache, uid);
}

static EContact *
cache_get_contact (EBookBackend *backend,
                   const gchar *uid,
                   GDataEntry **entry)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	contact = e_book_backend_cache_get_contact (priv->cache, uid);
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

static GList *
cache_get_contacts (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GList *contacts, *iter;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	contacts = e_book_backend_cache_get_contacts (priv->cache, "(contains \"x-evolution-any-field\" \"\")");
	for (iter = contacts; iter; iter = iter->next)
		e_contact_remove_gdata_entry_xml (iter->data);

	return contacts;
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

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	return e_book_backend_cache_get_time (priv->cache);
}

static void
cache_set_last_update (EBookBackend *backend,
                       GTimeVal *tv)
{
	EBookBackendGooglePrivate *priv;
	gchar *_time;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	_time = g_time_val_to_iso8601 (tv);
	e_book_backend_cache_set_time (priv->cache, _time);
	g_free (_time);
}

static gboolean
backend_is_authorized (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	if (priv->service == NULL)
		return FALSE;

#ifdef HAVE_GOA
	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background without
	 * bothering clients with "auth-required" signals. */
	if (E_IS_GDATA_GOA_AUTHORIZER (priv->authorizer))
		return TRUE;
#endif

	return gdata_service_is_authorized (priv->service);
}

static void
on_contact_added (EBookBackend *backend,
                  EContact *contact)
{
	EBookBackendGooglePrivate *priv;
	GList *iter;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), g_object_ref (contact));
}

static void
on_contact_removed (EBookBackend *backend,
                    const gchar *uid)
{
	EBookBackendGooglePrivate *priv;
	GList *iter;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_remove (E_DATA_BOOK_VIEW (iter->data), g_strdup (uid));
}

static void
on_contact_changed (EBookBackend *backend,
                    EContact *contact)
{
	EBookBackendGooglePrivate *priv;
	GList *iter;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), g_object_ref (contact));
}

static GCancellable *
start_operation (EBookBackend *backend,
                 guint32 opid,
                 GCancellable *cancellable,
                 const gchar *message)
{
	EBookBackendGooglePrivate *priv;
	GList *iter;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	/* Insert the operation into the set of active cancellable operations */
	if (cancellable)
		g_object_ref (cancellable);
	else
		cancellable = g_cancellable_new ();
	g_hash_table_insert (priv->cancellables, GUINT_TO_POINTER (opid), g_object_ref (cancellable));

	/* Send out a status message to each view */
	for (iter = priv->bookviews; iter; iter = iter->next)
		e_data_book_view_notify_progress (E_DATA_BOOK_VIEW (iter->data), -1, message);

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
		__debug__ ("Book view query failed: %s", book_error->message);
	}

	if (g_hash_table_remove (priv->cancellables, GUINT_TO_POINTER (opid))) {
		GList *iter;

		/* Send out a status message to each view */
		for (iter = priv->bookviews; iter; iter = iter->next)
			e_data_book_view_notify_complete (E_DATA_BOOK_VIEW (iter->data), book_error);
	}

	g_clear_error (&book_error);
}

static void
process_contact_finish (EBookBackend *backend,
                        GDataEntry *entry)
{
	EContact *new_contact;
	gboolean was_cached;

	__debug__ (G_STRFUNC);

	was_cached = cache_has_contact (backend, gdata_entry_get_id (entry));
	new_contact = cache_add_contact (backend, entry);

	if (was_cached == TRUE) {
		on_contact_changed (backend, new_contact);
	} else {
		on_contact_added (backend, new_contact);
	}

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
	__debug__ (G_STRFUNC);

	/* Are we finished yet? */
	if (data->update_complete == FALSE || data->num_contacts_pending_photos > 0) {
		__debug__ (
			"Bailing from check_get_new_contacts_finished(): update_complete: %u, num_contacts_pending_photos: %u, data: %p",
			data->update_complete, data->num_contacts_pending_photos, data);
		return;
	}

	__debug__ ("Proceeding with check_get_new_contacts_finished() for data: %p.", data);

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
	__debug__ (G_STRFUNC);

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

	__debug__ (G_STRFUNC);

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
		__debug__ ("Downloading contact photo for '%s' failed: %s", gdata_entry_get_id (GDATA_ENTRY (gdata_contact)), error->message);
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

	__debug__ (G_STRFUNC);
	feed = gdata_service_query_finish (service, result, &gdata_error);
	if (__e_book_backend_google_debug__ && feed) {
		GList *entries = gdata_feed_get_entries (feed);
		__debug__ ("Feed has %d entries", g_list_length (entries));
	}

	if (feed != NULL)
		g_object_unref (feed);

	if (!gdata_error) {
		/* Finish updating the cache */
		GTimeVal current_time;
		g_get_current_time (&current_time);
		cache_set_last_update (backend, &current_time);
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

	__debug__ (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	/* Sort out update times */
	last_updated = cache_get_last_update (backend);
	g_assert (last_updated == NULL || g_time_val_from_iso8601 (last_updated, &updated) == TRUE);
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

	__debug__ (G_STRFUNC);
	uid = gdata_entry_get_id (entry);
	name = e_contact_sanitise_google_group_name (entry);

	system_group_id = gdata_contacts_group_get_system_group_id (GDATA_CONTACTS_GROUP (entry));
	is_deleted = gdata_contacts_group_is_deleted (GDATA_CONTACTS_GROUP (entry));

	if (system_group_id) {
		__debug__ ("Processing %ssystem group %s, %s", is_deleted ? "(deleted) " : "", system_group_id, uid);

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
		__debug__ ("Processing (deleting) group %s, %s", uid, name);
		g_hash_table_remove (priv->groups_by_id, uid);
		g_hash_table_remove (priv->groups_by_name, name);
	} else {
		__debug__ ("Processing group %s, %s", uid, name);
		g_hash_table_replace (priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (name));
		g_hash_table_replace (priv->groups_by_name, g_strdup (name), e_contact_sanitise_google_group_id (uid));
	}

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

	__debug__ (G_STRFUNC);
	feed = gdata_service_query_finish (service, result, &gdata_error);
	if (__e_book_backend_google_debug__ && feed) {
		GList *entries = gdata_feed_get_entries (feed);
		__debug__ ("Group feed has %d entries", g_list_length (entries));
	}

	if (feed != NULL)
		g_object_unref (feed);

	if (!gdata_error) {
		/* Update the update time */
		g_get_current_time (&(priv->last_groups_update));
	}

	finish_operation (backend, -2, gdata_error);
	g_object_unref (backend);

	g_clear_error (&gdata_error);
}

static void
get_groups (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GDataQuery *query;
	GCancellable *cancellable;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));
	if (priv->last_groups_update.tv_sec != 0 || priv->last_groups_update.tv_usec != 0) {
		gdata_query_set_updated_min (query, priv->last_groups_update.tv_sec);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

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
		(GAsyncReadyCallback) get_groups_cb,
		backend);

	g_object_unref (cancellable);
	g_object_unref (query);
}

static void
get_groups_sync (EBookBackend *backend,
                 GCancellable *cancellable)
{
	EBookBackendGooglePrivate *priv;
	GDataQuery *query;
	GDataFeed *feed;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);
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
		NULL);

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
		const gchar *group_entry_id = g_hash_table_lookup (priv->system_groups_by_id, system_group_id);

		g_return_val_if_fail (group_entry_id != NULL, NULL);

		return g_strdup (group_entry_id);
	}

	group = GDATA_ENTRY (gdata_contacts_group_new (NULL));

	gdata_entry_set_title (group, category_name);
	__debug__ ("Creating group %s", category_name);

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
	g_hash_table_replace (priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (category_name));
	g_hash_table_replace (priv->groups_by_name, g_strdup (category_name), e_contact_sanitise_google_group_id (uid));
	g_object_unref (new_group);

	__debug__ ("...got UID %s", uid);

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

	__debug__ ("Invoking cache refresh");

	get_groups (backend);
	get_new_contacts (backend);
}

static void
cache_refresh_if_needed (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	gboolean is_online;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	is_online = e_backend_get_online (E_BACKEND (backend));

	if (!is_online || !backend_is_authorized (backend)) {
		__debug__ ("We are not connected to Google%s.", (!is_online) ? " (offline mode)" : "");
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
	} else if (g_hash_table_size (priv->system_groups_by_id) == 0) {
		get_groups (backend);
	}

	return;
}

static void
proxy_settings_changed (EProxy *proxy,
                        EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	SoupURI *proxy_uri = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	if (!priv || !priv->service)
		return;

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, URI_GET_CONTACTS))
		proxy_uri = e_proxy_peek_uri_for (proxy, URI_GET_CONTACTS);
	gdata_service_set_proxy_uri (priv->service, proxy_uri);
}

static gboolean
request_authorization (EBookBackend *backend,
                       GCancellable *cancellable,
                       GError **error)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	/* Make sure we have the GDataService configured
	 * before requesting authorization. */

#ifdef HAVE_GOA
	/* If this is associated with a GNOME Online Account,
	 * use OAuth authentication instead of ClientLogin. */
	if (priv->authorizer == NULL) {
		EGDataGoaAuthorizer *authorizer;
		GoaObject *goa_object;

		goa_object = g_object_get_data (
			G_OBJECT (backend), "GNOME Online Account");
		if (GOA_IS_OBJECT (goa_object)) {
			authorizer = e_gdata_goa_authorizer_new (goa_object);
			priv->authorizer = GDATA_AUTHORIZER (authorizer);
		}
	}
#endif

	if (priv->authorizer == NULL) {
		GDataClientLoginAuthorizer *authorizer;

		authorizer = gdata_client_login_authorizer_new (
			CLIENT_ID, GDATA_TYPE_CONTACTS_SERVICE);
		priv->authorizer = GDATA_AUTHORIZER (authorizer);
	}

	if (priv->service == NULL) {
		GDataContactsService *contacts_service;

		contacts_service =
			gdata_contacts_service_new (priv->authorizer);
		priv->service = GDATA_SERVICE (contacts_service);
		proxy_settings_changed (priv->proxy, backend);
	}

#ifdef HAVE_GOA
	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background. */
	if (E_IS_GDATA_GOA_AUTHORIZER (priv->authorizer))
		return TRUE;
#endif

	/* Otherwise it's up to us to obtain a login secret. */
	return e_backend_authenticate_sync (
		E_BACKEND (backend),
		E_SOURCE_AUTHENTICATOR (backend),
		cancellable, error);
}

typedef struct {
	EBookBackend *backend;
	EDataBook *book;
	guint32 opid;
	GCancellable *cancellable;
	GDataContactsContact *new_contact;
	EContactPhoto *photo;
} CreateContactData;

static void
create_contact_finish (CreateContactData *data,
                       GDataContactsContact *new_contact,
                       const GError *gdata_error)
{
	__debug__ (G_STRFUNC);

	if (gdata_error == NULL) {
		/* Add the new contact to the cache. If uploading the photo was successful, the photo's data is stored on the contact as the "photo"
		 * key, which the cache will pick up and store. */
		EContact *e_contact;
		GSList added_contacts = {NULL,};
		e_contact = cache_add_contact (data->backend, GDATA_ENTRY (new_contact));

		added_contacts.data = e_contact;
		e_data_book_respond_create_contacts (data->book, data->opid, NULL, &added_contacts);
		g_object_unref (e_contact);
	} else {
		GError *book_error = NULL;

		/* Report the error. */
		data_book_error_from_gdata_error (&book_error, gdata_error);
		e_data_book_respond_create_contacts (data->book, data->opid, book_error, NULL);
	}

	finish_operation (data->backend, data->opid, gdata_error);

	if (data->photo != NULL) {
		e_contact_photo_free (data->photo);
	}

	if (data->new_contact != NULL) {
		g_object_unref (data->new_contact);
	}

	g_object_unref (data->cancellable);
	g_object_unref (data->book);
	g_object_unref (data->backend);
	g_slice_free (CreateContactData, data);
}

static void
create_contact_photo_query_cb (GDataService *service,
                               GAsyncResult *async_result,
                               CreateContactData *data)
{
	GDataEntry *queried_contact;
	EContactPhoto *photo;
	GError *gdata_error = NULL;

	__debug__ (G_STRFUNC);

	queried_contact = gdata_service_query_single_entry_finish (service, async_result, &gdata_error);

	if (gdata_error != NULL) {
		__debug__ ("Querying for created contact failed: %s", gdata_error->message);
		goto finish;
	}

	/* Output debug XML */
	if (__e_book_backend_google_debug__) {
		gchar *xml = gdata_parsable_get_xml (GDATA_PARSABLE (queried_contact));
		__debug__ ("After re-querying:\n%s", xml);
		g_free (xml);
	}

	/* Copy the photo from the previous contact to the new one so that it makes it into the cache. */
	photo = g_object_steal_data (G_OBJECT (data->new_contact), "photo");

	if (photo != NULL) {
		g_object_set_data_full (G_OBJECT (queried_contact), "photo", photo, (GDestroyNotify) e_contact_photo_free);
	}

finish:
	create_contact_finish (data, GDATA_CONTACTS_CONTACT (queried_contact), gdata_error);

	g_clear_error (&gdata_error);

	if (queried_contact != NULL) {
		g_object_unref (queried_contact);
	}
}

static void
create_contact_photo_cb (GDataContactsContact *contact,
                         GAsyncResult *async_result,
                         CreateContactData *data)
{
	EBookBackendGooglePrivate *priv;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (data->backend);

	__debug__ (G_STRFUNC);

	gdata_contacts_contact_set_photo_finish (contact, async_result, &gdata_error);

	if (gdata_error == NULL) {
		/* Success! Store the photo on the final GDataContactsContact object so it makes it into the cache. */
		g_object_set_data_full (G_OBJECT (contact), "photo", data->photo, (GDestroyNotify) e_contact_photo_free);
		data->photo = NULL;

		/* We now have to re-query for the contact, since setting its photo changes the contact's ETag. */
		gdata_service_query_single_entry_async (
			priv->service,
			gdata_contacts_service_get_primary_authorization_domain (),
			gdata_entry_get_id (GDATA_ENTRY (contact)), NULL, GDATA_TYPE_CONTACTS_CONTACT,
			data->cancellable, (GAsyncReadyCallback) create_contact_photo_query_cb, data);
		return;
	} else {
		/* Error. */
		__debug__ ("Uploading initial contact photo for '%s' failed: %s", gdata_entry_get_id (GDATA_ENTRY (contact)), gdata_error->message);
	}

	/* Respond to the initial create contact operation. */
	create_contact_finish (data, contact, gdata_error);

	g_clear_error (&gdata_error);
}

static void
create_contact_cb (GDataService *service,
                   GAsyncResult *result,
                   CreateContactData *data)
{
	GError *gdata_error = NULL;
	GDataEntry *new_contact;

	__debug__ (G_STRFUNC);

	new_contact = gdata_service_insert_entry_finish (service, result, &gdata_error);

	if (!new_contact) {
		__debug__ ("Creating contact failed: %s", gdata_error->message);
		goto finish;
	}

	data->new_contact = g_object_ref (new_contact);

	/* Add a photo for the new contact, if appropriate. This has to be done before we respond to the contact creation operation so that
	 * we can update the EContact with the photo data and ETag. */
	if (data->photo != NULL) {
		gdata_contacts_contact_set_photo_async (
			GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
			(const guint8 *) data->photo->data.inlined.data, data->photo->data.inlined.length,
			data->photo->data.inlined.mime_type, data->cancellable,
			(GAsyncReadyCallback) create_contact_photo_cb, data);
		return;
	}

finish:
	create_contact_finish (data, GDATA_CONTACTS_CONTACT (new_contact), gdata_error);

	g_clear_error (&gdata_error);

	if (new_contact != NULL) {
		g_object_unref (new_contact);
	}
}

/*
 * Creating a contact happens in either one request or three, depending on whether the contact's photo needs to be set. If the photo doesn't
 * need to be set, a single request is made to insert the contact's other data, and finished and responded to in create_contact_cb().
 *
 * If the photo does need to be set, one request is made to insert the contact's other data, which is finished in create_contact_cb(). This then
 * makes another request to upload the photo, which is finished in create_contact_photo_cb(). This then makes another request to re-query
 * the contact so that we have the latest version of its ETag (which changes when the contact's photo is set); this is finished and the creation
 * operation responded to in create_contact_photo_query_cb().
 */
static void
e_book_backend_google_create_contacts (EBookBackend *backend,
                                       EDataBook *book,
                                       guint32 opid,
                                       GCancellable *cancellable,
                                       const GSList *vcards)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact;
	GDataEntry *entry;
	gchar *xml;
	CreateContactData *data;
	const gchar *vcard_str = (const gchar *) vcards->data;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (vcards->next != NULL) {
		e_data_book_respond_create_contacts (
			book, opid,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk additions")),
			NULL);
		return;
	}

	__debug__ (G_STRFUNC);

	__debug__ ("Creating: %s", vcard_str);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_create_contacts (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
		return;
	}

	g_return_if_fail (backend_is_authorized (backend));

	/* Ensure the system groups have been fetched. */
	if (g_hash_table_size (priv->system_groups_by_id) == 0) {
		get_groups_sync (backend, cancellable);
	}

	/* Build the GDataEntry from the vCard */
	contact = e_contact_new_from_vcard (vcard_str);
	entry = gdata_entry_new_from_e_contact (contact, priv->groups_by_name, priv->system_groups_by_id, _create_group, backend);
	g_object_unref (contact);

	/* Debug XML output */
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	__debug__ ("new entry with xml: %s", xml);
	g_free (xml);

	/* Insert the entry on the server asynchronously */
	cancellable = start_operation (backend, opid, cancellable, _("Creating new contact…"));

	data = g_slice_new (CreateContactData);
	data->backend = g_object_ref (backend);
	data->book = g_object_ref (book);
	data->opid = opid;
	data->cancellable = g_object_ref (cancellable);
	data->new_contact = NULL;
	data->photo = g_object_steal_data (G_OBJECT (entry), "photo");

	gdata_contacts_service_insert_contact_async (
		GDATA_CONTACTS_SERVICE (priv->service), GDATA_CONTACTS_CONTACT (entry), cancellable,
		(GAsyncReadyCallback) create_contact_cb, data);

	g_object_unref (cancellable);
	g_object_unref (entry);
}

typedef struct {
	EBookBackend *backend;
	EDataBook *book;
	guint32 opid;
	gchar *uid;
} RemoveContactData;

static void
remove_contact_cb (GDataService *service,
                   GAsyncResult *result,
                   RemoveContactData *data)
{
	GError *gdata_error = NULL;
	gboolean success;
	GSList *ids;

	__debug__ (G_STRFUNC);

	success = gdata_service_delete_entry_finish (service, result, &gdata_error);
	finish_operation (data->backend, data->opid, gdata_error);

	if (!success) {
		GError *book_error = NULL;
		data_book_error_from_gdata_error (&book_error, gdata_error);
		__debug__ ("Deleting contact %s failed: %s", data->uid, gdata_error->message);
		g_error_free (gdata_error);

		e_data_book_respond_remove_contacts (data->book, data->opid, book_error, NULL);
		goto finish;
	}

	/* List the entry's ID in the success list */
	ids = g_slist_prepend (NULL, data->uid);
	e_data_book_respond_remove_contacts (data->book, data->opid, NULL, ids);
	g_slist_free (ids);

finish:
	g_free (data->uid);
	g_object_unref (data->book);
	g_object_unref (data->backend);
	g_slice_free (RemoveContactData, data);
}

static void
e_book_backend_google_remove_contacts (EBookBackend *backend,
                                       EDataBook *book,
                                       guint32 opid,
                                       GCancellable *cancellable,
                                       const GSList *id_list)
{
	EBookBackendGooglePrivate *priv;
	const gchar *uid = id_list->data;
	GDataEntry *entry = NULL;
	EContact *cached_contact;
	RemoveContactData *data;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
		return;
	}

	g_return_if_fail (backend_is_authorized (backend));

	/* We make the assumption that the ID list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (id_list->next != NULL) {
		e_data_book_respond_remove_contacts (
			book, opid,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk removals")),
			NULL);
		return;
	}
	g_return_if_fail (!id_list->next);

	/* Get the contact and associated GDataEntry from the cache */
	cached_contact = cache_get_contact (backend, uid, &entry);

	if (!cached_contact) {
		__debug__ ("Deleting contact %s failed: Contact not found in cache.", uid);

		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
		return;
	}

	g_object_unref (cached_contact);

	/* Remove the contact from the cache */
	cache_remove_contact (backend, uid);

	/* Delete the contact from the server asynchronously */
	data = g_slice_new (RemoveContactData);
	data->backend = g_object_ref (backend);
	data->book = g_object_ref (book);
	data->opid = opid;
	data->uid = g_strdup (uid);

	cancellable = start_operation (backend, opid, cancellable, _("Deleting contact…"));
	gdata_service_delete_entry_async (
		GDATA_SERVICE (priv->service), gdata_contacts_service_get_primary_authorization_domain (),
		entry, cancellable, (GAsyncReadyCallback) remove_contact_cb, data);
	g_object_unref (cancellable);
	g_object_unref (entry);
}

typedef enum {
	LEAVE_PHOTO,
	ADD_PHOTO,
	REMOVE_PHOTO,
	UPDATE_PHOTO,
} PhotoOperation;

typedef struct {
	EBookBackend *backend;
	EDataBook *book;
	guint32 opid;
	GCancellable *cancellable;
	GDataContactsContact *new_contact;
	EContactPhoto *photo;
	PhotoOperation photo_operation;
} ModifyContactData;

static void
modify_contact_finish (ModifyContactData *data,
                       GDataContactsContact *new_contact,
                       const GError *gdata_error)
{
	EContact *e_contact;

	__debug__ (G_STRFUNC);

	if (gdata_error == NULL) {
		GSList modified_contacts = {NULL,};
		/* Add the new entry to the cache */
		e_contact = cache_add_contact (data->backend, GDATA_ENTRY (new_contact));
		modified_contacts.data = e_contact;
		e_data_book_respond_modify_contacts (data->book, data->opid, NULL, &modified_contacts);
		g_object_unref (e_contact);
	} else {
		GError *book_error = NULL;

		/* Report the error. */
		data_book_error_from_gdata_error (&book_error, gdata_error);
		e_data_book_respond_modify_contacts (data->book, data->opid, book_error, NULL);
	}

	finish_operation (data->backend, data->opid, gdata_error);

	if (data->photo != NULL) {
		e_contact_photo_free (data->photo);
	}

	if (data->new_contact != NULL) {
		g_object_unref (data->new_contact);
	}

	g_object_unref (data->cancellable);
	g_object_unref (data->book);
	g_object_unref (data->backend);
	g_slice_free (ModifyContactData, data);
}

static void
modify_contact_photo_query_cb (GDataService *service,
                               GAsyncResult *async_result,
                               ModifyContactData *data)
{
	GDataEntry *queried_contact;
	EContactPhoto *photo;
	GError *gdata_error = NULL;

	__debug__ (G_STRFUNC);

	queried_contact = gdata_service_query_single_entry_finish (service, async_result, &gdata_error);

	if (gdata_error != NULL) {
		__debug__ ("Querying for modified contact failed: %s", gdata_error->message);
		goto finish;
	}

	/* Output debug XML */
	if (__e_book_backend_google_debug__) {
		gchar *xml = gdata_parsable_get_xml (GDATA_PARSABLE (queried_contact));
		__debug__ ("After re-querying:\n%s", xml);
		g_free (xml);
	}

	/* Copy the photo from the previous contact to the new one so that it makes it into the cache. */
	photo = g_object_steal_data (G_OBJECT (data->new_contact), "photo");

	if (photo != NULL) {
		g_object_set_data_full (G_OBJECT (queried_contact), "photo", photo, (GDestroyNotify) e_contact_photo_free);
	}

finish:
	modify_contact_finish (data, GDATA_CONTACTS_CONTACT (queried_contact), gdata_error);

	g_clear_error (&gdata_error);

	if (queried_contact != NULL) {
		g_object_unref (queried_contact);
	}
}

static void
modify_contact_photo_cb (GDataContactsContact *contact,
                         GAsyncResult *async_result,
                         ModifyContactData *data)
{
	EBookBackendGooglePrivate *priv;
	GError *gdata_error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (data->backend);

	__debug__ (G_STRFUNC);

	gdata_contacts_contact_set_photo_finish (contact, async_result, &gdata_error);

	if (gdata_error == NULL) {
		/* Success! Store the photo on the final GDataContactsContact object so it makes it into the cache. */
		if (data->photo != NULL) {
			g_object_set_data_full (G_OBJECT (contact), "photo", data->photo, (GDestroyNotify) e_contact_photo_free);
			data->photo = NULL;
		} else {
			g_object_set_data (G_OBJECT (contact), "photo", NULL);
		}

		/* We now have to re-query for the contact, since setting its photo changes the contact's ETag. */
		gdata_service_query_single_entry_async (
			priv->service,
			gdata_contacts_service_get_primary_authorization_domain (),
			gdata_entry_get_id (GDATA_ENTRY (contact)), NULL, GDATA_TYPE_CONTACTS_CONTACT,
			data->cancellable, (GAsyncReadyCallback) modify_contact_photo_query_cb, data);
		return;
	} else {
		/* Error. */
		__debug__ ("Uploading modified contact photo for '%s' failed: %s", gdata_entry_get_id (GDATA_ENTRY (contact)), gdata_error->message);
	}

	/* Respond to the initial modify contact operation. */
	modify_contact_finish (data, contact, gdata_error);

	g_clear_error (&gdata_error);
}

static void
modify_contact_cb (GDataService *service,
                   GAsyncResult *result,
                   ModifyContactData *data)
{
	GDataEntry *new_contact;
	GError *gdata_error = NULL;

	__debug__ (G_STRFUNC);

	new_contact = gdata_service_update_entry_finish (service, result, &gdata_error);

	if (!new_contact) {
		__debug__ ("Modifying contact failed: %s", gdata_error->message);
		goto finish;
	}

	/* Output debug XML */
	if (__e_book_backend_google_debug__) {
		gchar *xml = gdata_parsable_get_xml (GDATA_PARSABLE (new_contact));
		__debug__ ("After:\n%s", xml);
		g_free (xml);
	}

	data->new_contact = g_object_ref (new_contact);

	/* Add a photo for the new contact, if appropriate. This has to be done before we respond to the contact creation operation so that
	 * we can update the EContact with the photo data and ETag. */
	switch (data->photo_operation) {
		case LEAVE_PHOTO:
			/* Do nothing apart from copy the photo stolen from the old GDataContactsContact to the updated one we've just received from
			 * Google. */
			g_object_set_data_full (G_OBJECT (new_contact), "photo", data->photo, (GDestroyNotify) e_contact_photo_free);
			data->photo = NULL;
			break;
		case ADD_PHOTO:
		case UPDATE_PHOTO:
			/* Set the photo. */
			g_return_if_fail (data->photo != NULL);
			gdata_contacts_contact_set_photo_async (
				GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
				(const guint8 *) data->photo->data.inlined.data, data->photo->data.inlined.length,
				data->photo->data.inlined.mime_type, data->cancellable,
				(GAsyncReadyCallback) modify_contact_photo_cb, data);
			return;
		case REMOVE_PHOTO:
			/* Unset the photo. */
			g_return_if_fail (data->photo == NULL);
			gdata_contacts_contact_set_photo_async (
				GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
				NULL, 0, NULL, data->cancellable, (GAsyncReadyCallback) modify_contact_photo_cb, data);
			return;
		default:
			g_assert_not_reached ();
	}

finish:
	modify_contact_finish (data, GDATA_CONTACTS_CONTACT (new_contact), gdata_error);

	g_clear_error (&gdata_error);

	if (new_contact != NULL) {
		g_object_unref (new_contact);
	}
}

/*
 * Modifying a contact happens in either one request or three, depending on whether the contact's photo needs to be updated. If the photo doesn't
 * need to be updated, a single request is made to update the contact's other data, and finished and responded to in modify_contact_cb().
 *
 * If the photo does need to be updated, one request is made to update the contact's other data, which is finished in modify_contact_cb(). This then
 * makes another request to upload the updated photo, which is finished in modify_contact_photo_cb(). This then makes another request to re-query
 * the contact so that we have the latest version of its ETag (which changes when the contact's photo is set); this is finished and the modification
 * operation responded to in modify_contact_photo_query_cb().
 */
static void
e_book_backend_google_modify_contacts (EBookBackend *backend,
                                      EDataBook *book,
                                      guint32 opid,
                                      GCancellable *cancellable,
                                      const GSList *vcards)
{
	EBookBackendGooglePrivate *priv;
	EContact *contact, *cached_contact;
	EContactPhoto *old_photo, *new_photo;
	GDataEntry *entry = NULL;
	const gchar *uid;
	ModifyContactData *data;
	const gchar *vcard_str = vcards->data;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	__debug__ ("Updating: %s", vcard_str);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
		return;
	}

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. This is because there is no clean way to roll back changes in case of an error. */
	if (vcards->next != NULL) {
		e_data_book_respond_modify_contacts (book, opid,
						     EDB_ERROR_EX (NOT_SUPPORTED,
						     _("The backend does not support bulk modifications")),
						     NULL);
		return;
	}

	g_return_if_fail (backend_is_authorized (backend));

	/* Get the new contact and its UID */
	contact = e_contact_new_from_vcard (vcard_str);
	uid = e_contact_get (contact, E_CONTACT_UID);

	/* Get the old cached contact with the same UID and its associated GDataEntry */
	cached_contact = cache_get_contact (backend, uid, &entry);

	if (!cached_contact) {
		__debug__ ("Modifying contact failed: Contact with uid %s not found in cache.", uid);
		g_object_unref (contact);

		e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
		return;
	}

	/* Ensure the system groups have been fetched. */
	if (g_hash_table_size (priv->system_groups_by_id) == 0) {
		get_groups_sync (backend, cancellable);
	}

	/* Update the old GDataEntry from the new contact */
	gdata_entry_update_from_e_contact (entry, contact, FALSE, priv->groups_by_name, priv->system_groups_by_id, _create_group, backend);

	/* Output debug XML */
	if (__e_book_backend_google_debug__) {
		gchar *xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
		__debug__ ("Before:\n%s", xml);
		g_free (xml);
	}

	/* Update the contact on the server asynchronously */
	cancellable = start_operation (backend, opid, cancellable, _("Modifying contact…"));

	data = g_slice_new (ModifyContactData);
	data->backend = g_object_ref (backend);
	data->book = g_object_ref (book);
	data->opid = opid;

	data->cancellable = g_object_ref (cancellable);
	data->new_contact = NULL;
	data->photo = g_object_steal_data (G_OBJECT (entry), "photo");

	/* Update the contact's photo. We can't rely on the ETags at this point, as the ETag in @ontact may be out of sync with the photo in the
	 * EContact (since the photo may have been updated). Consequently, after updating @entry its ETag may also be out of sync with its attached
	 * photo data. This means that we have to detect whether the photo has changed by comparing the photo data itself, which is guaranteed to
	 * be in sync between @contact and @entry. */
	old_photo = e_contact_get (cached_contact, E_CONTACT_PHOTO);
	new_photo = e_contact_get (contact, E_CONTACT_PHOTO);

	if ((old_photo == NULL || old_photo->type != E_CONTACT_PHOTO_TYPE_INLINED) && new_photo != NULL) {
		/* Adding a photo */
		data->photo_operation = ADD_PHOTO;
	} else if (old_photo != NULL && (new_photo == NULL || new_photo->type != E_CONTACT_PHOTO_TYPE_INLINED)) {
		/* Removing a photo */
		data->photo_operation = REMOVE_PHOTO;
	} else if (old_photo != NULL && new_photo != NULL &&
		   (old_photo->data.inlined.length != new_photo->data.inlined.length ||
		    memcmp (old_photo->data.inlined.data, new_photo->data.inlined.data, old_photo->data.inlined.length) != 0)) {
		/* Modifying the photo */
		data->photo_operation = UPDATE_PHOTO;
	} else {
		/* Do nothing. */
		data->photo_operation = LEAVE_PHOTO;
	}

	if (new_photo != NULL) {
		e_contact_photo_free (new_photo);
	}

	if (old_photo != NULL) {
		e_contact_photo_free (old_photo);
	}

	gdata_service_update_entry_async (
		GDATA_SERVICE (priv->service), gdata_contacts_service_get_primary_authorization_domain (),
		entry, cancellable, (GAsyncReadyCallback) modify_contact_cb, data);
	g_object_unref (cancellable);

	g_object_unref (cached_contact);
	g_object_unref (contact);
	g_object_unref (entry);
}

static void
e_book_backend_google_get_contact (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *uid)
{
	EContact *contact;
	gchar *vcard_str;

	__debug__ (G_STRFUNC);

	/* Get the contact */
	contact = cache_get_contact (backend, uid, NULL);
	if (!contact) {
		__debug__ ("Getting contact with uid %s failed: Contact not found in cache.", uid);

		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
		return;
	}

	/* Success! Build and return a vCard of the contacts */
	vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	e_data_book_respond_get_contact (book, opid, NULL, vcard_str);
	g_free (vcard_str);
	g_object_unref (contact);
}

static void
e_book_backend_google_get_contact_list (EBookBackend *backend,
                                        EDataBook *book,
                                        guint32 opid,
                                        GCancellable *cancellable,
                                        const gchar *query)
{
	EBookBackendSExp *sexp;
	GList *all_contacts;
	GSList *filtered_contacts = NULL;

	__debug__ (G_STRFUNC);

	/* Get all contacts */
	sexp = e_book_backend_sexp_new (query);
	all_contacts = cache_get_contacts (backend);

	for (; all_contacts; all_contacts = g_list_delete_link (all_contacts, all_contacts)) {
		EContact *contact = all_contacts->data;

		/* If the search expression matches the contact, include it in the search results */
		if (e_book_backend_sexp_match_contact (sexp, contact)) {
			gchar *vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			filtered_contacts = g_slist_append (filtered_contacts, vcard_str);
		}

		g_object_unref (contact);
	}

	g_object_unref (sexp);

	e_data_book_respond_get_contact_list (book, opid, NULL, filtered_contacts);

	g_slist_foreach (filtered_contacts, (GFunc) g_free, NULL);
	g_slist_free (filtered_contacts);
}

static void
e_book_backend_google_get_contact_list_uids (EBookBackend *backend,
                                             EDataBook *book,
                                             guint32 opid,
                                             GCancellable *cancellable,
                                             const gchar *query)
{
	EBookBackendSExp *sexp;
	GList *all_contacts;
	GSList *filtered_uids = NULL;

	__debug__ (G_STRFUNC);

	/* Get all contacts */
	sexp = e_book_backend_sexp_new (query);
	all_contacts = cache_get_contacts (backend);

	for (; all_contacts; all_contacts = g_list_delete_link (all_contacts, all_contacts)) {
		EContact *contact = all_contacts->data;

		/* If the search expression matches the contact, include it in the search results */
		if (e_book_backend_sexp_match_contact (sexp, contact)) {
			filtered_uids = g_slist_append (filtered_uids, e_contact_get (contact, E_CONTACT_UID));
		}

		g_object_unref (contact);
	}

	g_object_unref (sexp);

	e_data_book_respond_get_contact_list_uids (book, opid, NULL, filtered_uids);

	g_slist_foreach (filtered_uids, (GFunc) g_free, NULL);
	g_slist_free (filtered_uids);
}

static void
e_book_backend_google_start_book_view (EBookBackend *backend,
                                       EDataBookView *bookview)
{
	EBookBackendGooglePrivate *priv;
	GList *cached_contacts;
	GError *error = NULL;

	g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend));
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (bookview));

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	priv->bookviews = g_list_append (priv->bookviews, bookview);

	e_data_book_view_ref (bookview);
	e_data_book_view_notify_progress (bookview, -1, _("Loading…"));

	/* Ensure that we're ready to support a view */
	cache_refresh_if_needed (backend);

	/* Get the contacts */
	cached_contacts = cache_get_contacts (backend);
	__debug__ ("%d contacts found in cache", g_list_length (cached_contacts));

	/* Notify the view that all the contacts have changed (i.e. been added) */
	for (; cached_contacts; cached_contacts = g_list_delete_link (cached_contacts, cached_contacts)) {
		EContact *contact = cached_contacts->data;
		e_data_book_view_notify_update (bookview, contact);
		g_object_unref (contact);
	}

	/* This function frees the GError passed to it. */
	e_data_book_view_notify_complete (bookview, error);
}

static void
e_book_backend_google_stop_book_view (EBookBackend *backend,
                                      EDataBookView *bookview)
{
	EBookBackendGooglePrivate *priv;
	GList *view;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	/* Remove the view from the list of active views */
	if ((view = g_list_find (priv->bookviews, bookview)) != NULL) {
		priv->bookviews = g_list_delete_link (priv->bookviews, view);
		e_data_book_view_unref (bookview);
	}
}

static void
e_book_backend_google_open (EBookBackend *backend,
                            EDataBook *book,
                            guint opid,
                            GCancellable *cancellable,
                            gboolean only_if_exists)
{
	EBookBackendGooglePrivate *priv;
	gboolean is_online;
	GError *error = NULL;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	if (priv->cancellables && backend_is_authorized (backend)) {
		e_book_backend_respond_opened (backend, book, opid, NULL);
		return;
	}

	/* Set up our object */
	if (!priv->cancellables) {
		priv->groups_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		priv->groups_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		priv->system_groups_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		priv->system_groups_by_entry_id = g_hash_table_new (g_str_hash, g_str_equal); /* shares keys and values with system_groups_by_id */
		priv->cancellables = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
	}

	cache_init (backend);

	/* Set up ready to be interacted with */
	is_online = e_backend_get_online (E_BACKEND (backend));
	e_book_backend_notify_online (backend, is_online);
	e_book_backend_notify_readonly (backend, TRUE);

	if (is_online) {
		if (request_authorization (backend, cancellable, &error)) {
			/* Refresh the authorizer.  This may block. */
			gdata_authorizer_refresh_authorization (
				priv->authorizer, cancellable, &error);
		}
	}

	if (!is_online || backend_is_authorized (backend)) {
		if (is_online) {
			e_book_backend_notify_readonly (backend, FALSE);
			cache_refresh_if_needed (backend);
		}

		e_book_backend_notify_opened (backend, NULL /* Success */);
	}

	/* This function frees the GError passed to it. */
	e_data_book_respond_open (book, opid, error);
}

static void
e_book_backend_google_get_backend_property (EBookBackend *backend,
                                            EDataBook *book,
                                            guint32 opid,
                                            GCancellable *cancellable,
                                            const gchar *prop_name)
{
	__debug__ (G_STRFUNC);

	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "net,do-initial-query,contact-lists");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GSList *fields = NULL;
		gchar *fields_str;
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
			E_CONTACT_IM_GOOGLE_TALK,
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
			E_CONTACT_IM_GOOGLE_TALK_HOME_1,
			E_CONTACT_IM_GOOGLE_TALK_HOME_2,
			E_CONTACT_IM_GOOGLE_TALK_HOME_3,
			E_CONTACT_IM_GOOGLE_TALK_WORK_1,
			E_CONTACT_IM_GOOGLE_TALK_WORK_2,
			E_CONTACT_IM_GOOGLE_TALK_WORK_3,
			E_CONTACT_SIP,
			E_CONTACT_ORG,
			E_CONTACT_ORG_UNIT,
			E_CONTACT_TITLE,
			E_CONTACT_ROLE,
			E_CONTACT_HOMEPAGE_URL,
			E_CONTACT_BLOG_URL,
			E_CONTACT_BIRTH_DATE,
			E_CONTACT_ANNIVERSARY,
			E_CONTACT_NOTE,
			E_CONTACT_PHOTO,
			E_CONTACT_CATEGORIES,
#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
			E_CONTACT_CATEGORY_LIST,
			E_CONTACT_FILE_AS
#else
			E_CONTACT_CATEGORY_LIST
#endif
#else
			E_CONTACT_CATEGORY_LIST
#endif
		};

		/* Add all the fields above to the list */
		for (i = 0; i < G_N_ELEMENTS (supported_fields); i++) {
			const gchar *field_name = e_contact_field_name (supported_fields[i]);
			fields = g_slist_prepend (fields, (gpointer) field_name);
		}

		fields_str = e_data_book_string_slist_to_comma_string (fields);

		e_data_book_respond_get_backend_property (book, opid, NULL, fields_str);

		g_slist_free (fields);
		g_free (fields_str);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "plain/password");
	} else {
		E_BOOK_BACKEND_CLASS (e_book_backend_google_parent_class)->get_backend_property (backend, book, opid, cancellable, prop_name);
	}
}

static void
google_cancel_all_operations (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	GHashTableIter iter;
	gpointer opid_ptr;
	GCancellable *cancellable;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

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
	gboolean is_online;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	__debug__ (G_STRFUNC);

	is_online = e_backend_get_online (E_BACKEND (backend));
	e_book_backend_notify_online (backend, is_online);

	if (is_online && e_book_backend_is_opened (backend)) {
		request_authorization (backend, NULL, NULL);
		if (backend_is_authorized (backend))
			e_book_backend_notify_readonly (backend, FALSE);
	} else {
		/* Going offline, so cancel all running operations */
		google_cancel_all_operations (backend);

		/* Mark the book as unwriteable if we're going offline, but don't do the inverse when we go online;
		 * e_book_backend_google_authenticate_user() will mark us as writeable again once the user's authenticated again. */
		e_book_backend_notify_readonly (backend, TRUE);

		/* We can free our service. */
		if (priv->service)
			g_object_unref (priv->service);
		priv->service = NULL;
	}
}

static void
e_book_backend_google_dispose (GObject *object)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (object);

	__debug__ (G_STRFUNC);

	/* Cancel all outstanding operations */
	google_cancel_all_operations (E_BOOK_BACKEND (object));

	while (priv->bookviews) {
		e_data_book_view_unref (priv->bookviews->data);
		priv->bookviews = g_list_delete_link (priv->bookviews, priv->bookviews);
	}

	if (priv->refresh_id) {
		e_source_refresh_remove_timeout (
			e_backend_get_source (E_BACKEND (object)),
			priv->refresh_id);
		priv->refresh_id = 0;
	}

	if (priv->service)
		g_object_unref (priv->service);
	priv->service = NULL;

	if (priv->authorizer != NULL)
		g_object_unref (priv->authorizer);
	priv->authorizer = NULL;

	if (priv->proxy)
		g_object_unref (priv->proxy);
	priv->proxy = NULL;

	g_clear_object (&priv->cache);

	G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
e_book_backend_google_finalize (GObject *object)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (object);

	__debug__ (G_STRFUNC);

	if (priv->cancellables) {
		g_hash_table_destroy (priv->groups_by_id);
		g_hash_table_destroy (priv->groups_by_name);
		g_hash_table_destroy (priv->system_groups_by_entry_id);
		g_hash_table_destroy (priv->system_groups_by_id);
		g_hash_table_destroy (priv->cancellables);
	}

	G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static ESourceAuthenticationResult
book_backend_google_try_password_sync (ESourceAuthenticator *authenticator,
                                       const GString *password,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EBookBackendGooglePrivate *priv;
	ESourceAuthentication *auth_extension;
	ESourceAuthenticationResult result;
	ESource *source;
	const gchar *extension_name;
	gchar *user;
	GError *local_error = NULL;

	__debug__ (G_STRFUNC);

	/* We should not have gotten here if we're offline. */
	g_return_val_if_fail (
		e_backend_get_online (E_BACKEND (authenticator)),
		E_SOURCE_AUTHENTICATION_ERROR);

	/* Nor should we have gotten here if we're already authorized. */
	g_return_val_if_fail (
		!backend_is_authorized (E_BOOK_BACKEND (authenticator)),
		E_SOURCE_AUTHENTICATION_ERROR);

	priv = E_BOOK_BACKEND_GOOGLE (authenticator)->priv;

	source = e_backend_get_source (E_BACKEND (authenticator));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);
	user = e_source_authentication_dup_user (auth_extension);

	gdata_client_login_authorizer_authenticate (
		GDATA_CLIENT_LOGIN_AUTHORIZER (priv->authorizer),
		user, password->str, cancellable, &local_error);

	g_free (user);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	} else if (g_error_matches (
		local_error, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR,
		GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_BAD_AUTHENTICATION)) {

		g_clear_error (&local_error);
		result = E_SOURCE_AUTHENTICATION_REJECTED;

	} else {
		g_propagate_error (error, local_error);
		result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	return result;
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (class);

	g_type_class_add_private (class, sizeof (EBookBackendGooglePrivate));

	/* Set the virtual methods. */
	backend_class->open			= e_book_backend_google_open;
	backend_class->get_backend_property	= e_book_backend_google_get_backend_property;
	backend_class->start_book_view		= e_book_backend_google_start_book_view;
	backend_class->stop_book_view		= e_book_backend_google_stop_book_view;
	backend_class->create_contacts		= e_book_backend_google_create_contacts;
	backend_class->remove_contacts		= e_book_backend_google_remove_contacts;
	backend_class->modify_contacts		= e_book_backend_google_modify_contacts;
	backend_class->get_contact		= e_book_backend_google_get_contact;
	backend_class->get_contact_list		= e_book_backend_google_get_contact_list;
	backend_class->get_contact_list_uids	= e_book_backend_google_get_contact_list_uids;

	object_class->dispose  = e_book_backend_google_dispose;
	object_class->finalize = e_book_backend_google_finalize;

	__e_book_backend_google_debug__ = g_getenv ("GOOGLE_BACKEND_DEBUG") ? TRUE : FALSE;
}

static void
e_book_backend_google_source_authenticator_init (ESourceAuthenticatorInterface *interface)
{
	interface->try_password_sync = book_backend_google_try_password_sync;
}

static void
e_book_backend_google_init (EBookBackendGoogle *backend)
{
	__debug__ (G_STRFUNC);

	backend->priv = E_BOOK_BACKEND_GOOGLE_GET_PRIVATE (backend);

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_google_notify_online_cb), NULL);

	/* Set up our EProxy. */
	backend->priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (backend->priv->proxy);

	g_signal_connect (
		backend->priv->proxy, "changed",
		G_CALLBACK (proxy_settings_changed), backend);
}

static void
data_book_error_from_gdata_error (GError **dest_err,
                                  const GError *error)
{
	if (!error || !dest_err)
		return;

	/* only last error is used */
	g_clear_error (dest_err);

	if (error->domain == GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR) {
		/* Authentication errors */
		switch (error->code) {
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_BAD_AUTHENTICATION:
			g_propagate_error (dest_err, EDB_ERROR (AUTHENTICATION_FAILED));
			return;
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_NOT_VERIFIED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_TERMS_NOT_AGREED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_CAPTCHA_REQUIRED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DELETED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DISABLED:
			g_propagate_error (dest_err, EDB_ERROR (PERMISSION_DENIED));
			return;
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_SERVICE_DISABLED:
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
			g_propagate_error (dest_err, e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, error->message));
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
			g_propagate_error (dest_err, e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_QUERY, error->message));
			return;
		default:
			break;
		}
	}

	g_propagate_error (dest_err, e_data_book_create_error (E_DATA_BOOK_STATUS_OTHER_ERROR, error->message));
}

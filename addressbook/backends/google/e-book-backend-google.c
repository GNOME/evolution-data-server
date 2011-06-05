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
 *         Philip Withnall <philip@tecnocode.co.uk>
 */

#include <config.h>
#include <string.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <libedataserver/e-proxy.h>
#include <libebook/e-vcard.h>
#include <libebook/e-contact.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend-cache.h>
#include <gdata/gdata.h>

#include "e-book-backend-google.h"

#ifdef HAVE_GOA
#include "e-gdata-goa-authorizer.h"
#endif

#define CLIENT_ID "evolution-client-0.1.0"

#define URI_GET_CONTACTS "://www.google.com/m8/feeds/contacts/default/full"
#define GDATA_PHOTO_ETAG_ATTR "X-GDATA-PHOTO-ETAG"

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_BACKEND)

typedef enum {
	NO_CACHE,
	ON_DISK_CACHE,
	IN_MEMORY_CACHE
} CacheType;

struct _EBookBackendGooglePrivate {
	gboolean is_online;
	GList *bookviews;

	CacheType cache_type;
	union {
		EBookBackendCache *on_disk;
		struct {
			GHashTable *contacts;
			GHashTable *gdata_entries;
			GTimeVal last_updated;
		} in_memory;
	} cache;

	/* Mapping from group ID to (human readable) group name */
	GHashTable *groups_by_id;
	/* Mapping from (human readable) group name to group ID */
	GHashTable *groups_by_name;
	/* Time when the groups were last queried */
	GTimeVal last_groups_update;

	#ifdef HAVE_LIBGDATA_0_9
	GDataAuthorizer *authorizer;
	#endif
	GDataService *service;
	EProxy *proxy;
	guint refresh_interval;
	gboolean use_ssl;

	/* Whether the backend is being used by a view at the moment; if it isn't, we don't need to do updates or send out notifications */
	gboolean live_mode;

	/* In live mode we will send out signals in an idle_handler */
	guint idle_id;

	guint refresh_id;

	/* Map of active opids to GCancellables */
	GHashTable *cancellables;
};

gboolean __e_book_backend_google_debug__;
#define __debug__(...) (__e_book_backend_google_debug__ ? g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__) : (void) 0)

static void data_book_error_from_gdata_error (GError **dest_err, const GError *error);

static GDataEntry *_gdata_entry_new_from_e_contact (EBookBackend *backend, EContact *contact);
static gboolean _gdata_entry_update_from_e_contact (EBookBackend *backend, GDataEntry *entry, EContact *contact);

static EContact *_e_contact_new_from_gdata_entry (EBookBackend *backend, GDataEntry *entry);
static void _e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry);
static void _e_contact_remove_gdata_entry_xml (EContact *contact);
static const gchar *_e_contact_get_gdata_entry_xml (EContact *contact, const gchar **edit_uri);

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
		contact = _e_contact_new_from_gdata_entry (backend, entry);
		_e_contact_add_gdata_entry_xml (contact, entry);
		e_book_backend_cache_add_contact (priv->cache.on_disk, contact);
		_e_contact_remove_gdata_entry_xml (contact);
		return contact;
	case IN_MEMORY_CACHE:
		contact = _e_contact_new_from_gdata_entry (backend, entry);
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
				const gchar *entry_xml, *edit_uri = NULL;

				entry_xml = _e_contact_get_gdata_entry_xml (contact, &edit_uri);
				*entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, entry_xml, -1, NULL));

				if (*entry) {
					GDataLink *edit_link = gdata_link_new (edit_uri, GDATA_LINK_EDIT);
					gdata_entry_add_link (*entry, edit_link);
					g_object_unref (edit_link);
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
	if (!priv->is_online)
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

static gboolean
backend_is_authorized (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

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

#ifdef HAVE_LIBGDATA_0_9
	return gdata_service_is_authorized (priv->service);
#else
	return gdata_service_is_authenticated (priv->service);
#endif
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

static GCancellable *
start_operation (EBookBackend *backend, guint32 opid, GCancellable *cancellable, const gchar *message)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *iter;

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
finish_operation (EBookBackend *backend, guint32 opid)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	g_hash_table_remove (priv->cancellables, GUINT_TO_POINTER (opid));
}

static void
process_contact_finish (EBookBackend *backend, GDataEntry *entry)
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

#ifdef HAVE_LIBGDATA_0_9
static void
process_contact_photo_cb (GDataContactsContact *gdata_contact, GAsyncResult *async_result, EBookBackend *backend)
{
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
}
#endif /* HAVE_LIBGDATA_0_9 */

static void
process_contact_cb (GDataEntry *entry, guint entry_key, guint entry_count, EBookBackend *backend)
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
#ifdef HAVE_LIBGDATA_0_9
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
			/* Download the photo. */
			gdata_contacts_contact_get_photo_async (GDATA_CONTACTS_CONTACT (entry),
			                                        GDATA_CONTACTS_SERVICE (E_BOOK_BACKEND_GOOGLE (backend)->priv->service), NULL,
			                                        (GAsyncReadyCallback) process_contact_photo_cb, backend);
			g_free (old_photo_etag);

			return;
		}

		g_free (old_photo_etag);
#endif /* HAVE_LIBGDATA_0_9 */

		/* Since we're not downloading a photo, add the contact to the cache now. */
		process_contact_finish (backend, entry);
	}
}

static void
get_new_contacts_cb (GDataService *service, GAsyncResult *result, EBookBackend *backend)
{
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

	finish_operation (backend, 0);
	on_sequence_complete (backend, gdata_error);

	/* Thaw the cache again */
	cache_thaw (backend);

	g_clear_error (&gdata_error);
}

static void
get_new_contacts (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	gchar *last_updated;
	GTimeVal updated;
	GDataQuery *query;
	GCancellable *cancellable;

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
	cancellable = start_operation (backend, 0, NULL, _("Querying for updated contacts…"));
	gdata_contacts_service_query_contacts_async (
		GDATA_CONTACTS_SERVICE (priv->service),
		query,
		cancellable,
		(GDataQueryProgressCallback) process_contact_cb,
		backend,
#ifdef HAVE_LIBGDATA_0_9
		(GDestroyNotify) NULL,
#endif
		(GAsyncReadyCallback) get_new_contacts_cb,
		backend);

	g_object_unref (cancellable);
	g_object_unref (query);
}

static gchar *
sanitise_group_id (const gchar *group_id)
{
	gchar *id, *base;

	id = g_strdup (group_id);

	/* Fix the ID to refer to the full projection, rather than the base projection, because Google think that returning different IDs for the
	 * same object is somehow a good idea. */
	if (id != NULL) {
		base = strstr (id, "/base/");
		if (base != NULL)
			memcpy (base, "/full/", 6);
	}

	return id;
}

static gchar *
sanitise_group_name (GDataEntry *group)
{
	const gchar *system_group_id = gdata_contacts_group_get_system_group_id (GDATA_CONTACTS_GROUP (group));

	if (system_group_id == NULL) {
		return g_strdup (gdata_entry_get_title (group)); /* Non-system group */
	} else if (strcmp (system_group_id, GDATA_CONTACTS_GROUP_CONTACTS) == 0) {
		return g_strdup (_("Personal")); /* System Group: My Contacts */
	} else if (strcmp (system_group_id, GDATA_CONTACTS_GROUP_FRIENDS) == 0) {
		return g_strdup (_("Friends")); /* System Group: Friends */
	} else if (strcmp (system_group_id, GDATA_CONTACTS_GROUP_FAMILY) == 0) {
		return g_strdup (_("Family")); /* System Group: Family */
	} else if (strcmp (system_group_id, GDATA_CONTACTS_GROUP_COWORKERS) == 0) {
		return g_strdup (_("Coworkers")); /* System Group: Coworkers */
	} else {
		g_warning ("Unknown system group '%s' for group with ID '%s'.", system_group_id, gdata_entry_get_id (group));
		return g_strdup (gdata_entry_get_title (group));
	}
}

static void
process_group (GDataEntry *entry, guint entry_key, guint entry_count, EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	const gchar *uid;
	gchar *name;
	gboolean is_deleted;

	__debug__ (G_STRFUNC);
	uid = gdata_entry_get_id (entry);
	name = sanitise_group_name (entry);

	is_deleted = gdata_contacts_group_is_deleted (GDATA_CONTACTS_GROUP (entry));

	if (is_deleted) {
		__debug__ ("Processing (deleting) group %s, %s", uid, name);
		g_hash_table_remove (priv->groups_by_id, uid);
		g_hash_table_remove (priv->groups_by_name, name);
	} else {
		__debug__ ("Processing group %s, %s", uid, name);
		g_hash_table_replace (priv->groups_by_id, sanitise_group_id (uid), g_strdup (name));
		g_hash_table_replace (priv->groups_by_name, g_strdup (name), sanitise_group_id (uid));
	}

	g_free (name);
}

static void
get_groups_cb (GDataService *service, GAsyncResult *result, EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GDataFeed *feed;
	GError *gdata_error = NULL;

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

	finish_operation (backend, 1);

	g_clear_error (&gdata_error);
}

static void
get_groups (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GDataQuery *query;
	GCancellable *cancellable;

	__debug__ (G_STRFUNC);
	g_return_if_fail (backend_is_authorized (backend));

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));
	if (priv->last_groups_update.tv_sec != 0 || priv->last_groups_update.tv_usec != 0) {
		gdata_query_set_updated_min (query, priv->last_groups_update.tv_sec);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	/* Run the query asynchronously */
	cancellable = start_operation (backend, 1, NULL, _("Querying for updated groups…"));
	gdata_contacts_service_query_groups_async (
		GDATA_CONTACTS_SERVICE (priv->service),
		query,
		cancellable,
		(GDataQueryProgressCallback) process_group,
		backend,
#ifdef HAVE_LIBGDATA_0_9
		(GDestroyNotify) NULL,
#endif
		(GAsyncReadyCallback) get_groups_cb,
		backend);

	g_object_unref (cancellable);
	g_object_unref (query);
}

static gchar *
create_group (EBookBackend *backend, const gchar *category_name, GError **error)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GDataEntry *group, *new_group;
	gchar *uid;

	group = GDATA_ENTRY (gdata_contacts_group_new (NULL));

	gdata_entry_set_title (group, category_name);
	__debug__ ("Creating group %s", category_name);

	/* Insert the new group */
	new_group = GDATA_ENTRY (gdata_contacts_service_insert_group (GDATA_CONTACTS_SERVICE (priv->service), GDATA_CONTACTS_GROUP (group),
								      NULL, error));
	g_object_unref (group);

	if (new_group == NULL)
		return NULL;

	/* Add the new group to the group mappings */
	uid = g_strdup (gdata_entry_get_id (new_group));
	g_hash_table_replace (priv->groups_by_id, sanitise_group_id (uid), g_strdup (category_name));
	g_hash_table_replace (priv->groups_by_name, g_strdup (category_name), sanitise_group_id (uid));
	g_object_unref (new_group);

	__debug__ ("...got UID %s", uid);

	return uid;
}

static gboolean cache_refresh_if_needed (EBookBackend *backend);

static gboolean
on_refresh_timeout (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	__debug__ (G_STRFUNC);

	priv->refresh_id = 0;
	if (priv->live_mode)
		cache_refresh_if_needed (backend);

	return FALSE;
}

static gboolean
cache_refresh_if_needed (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	guint remaining_secs;
	gboolean install_timeout;

	__debug__ (G_STRFUNC);

	if (!priv->is_online || !backend_is_authorized (backend)) {
		__debug__ ("We are not connected to Google%s.", (!priv->is_online) ? " (offline mode)" : "");
		return TRUE;
	}

	install_timeout = (priv->live_mode && priv->refresh_interval > 0 && 0 == priv->refresh_id);

	if (cache_needs_update (backend, &remaining_secs)) {
		/* Update the cache asynchronously and schedule a new timeout */
		get_groups (backend);
		get_new_contacts (backend);
		remaining_secs = priv->refresh_interval;
	}

	if (install_timeout) {
		__debug__ ("Installing timeout with %d seconds", remaining_secs);
		priv->refresh_id = g_timeout_add_seconds (remaining_secs, (GSourceFunc) on_refresh_timeout, backend);
	}

	return TRUE;
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
proxy_settings_changed (EProxy *proxy,
                        EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;
	SoupURI *proxy_uri = NULL;
	gchar *uri;

	priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	if (!priv || !priv->service)
		return;

	/* Build the URI which libgdata would use to query contacts */
	uri = g_strconcat (
		priv->use_ssl ? "https" : "http",
		URI_GET_CONTACTS, NULL);

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, uri))
		proxy_uri = e_proxy_peek_uri_for (proxy, uri);
	gdata_service_set_proxy_uri (priv->service, proxy_uri);

	g_free (uri);
}

static void
request_authorization (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv;

	priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

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

#ifdef HAVE_LIBGDATA_0_9
	if (priv->authorizer == NULL) {
		GDataClientLoginAuthorizer *authorizer;

		authorizer = gdata_client_login_authorizer_new (
			CLIENT_ID, GDATA_TYPE_CONTACTS_SERVICE);
		priv->authorizer = GDATA_AUTHORIZER (authorizer);
	}
#endif

	if (priv->service == NULL) {
		GDataContactsService *contacts_service;

#ifdef HAVE_LIBGDATA_0_9
		contacts_service =
			gdata_contacts_service_new (priv->authorizer);
#else
		contacts_service = gdata_contacts_service_new (CLIENT_ID);
#endif
		priv->service = GDATA_SERVICE (contacts_service);
		proxy_settings_changed (priv->proxy, backend);
	}

#ifdef HAVE_GOA
	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background without
	 * bothering clients with "auth-required" signals. */
	if (E_IS_GDATA_GOA_AUTHORIZER (priv->authorizer))
		return;
#endif

	e_book_backend_notify_auth_required (backend, TRUE, NULL);
}

typedef struct {
	EBookBackend *backend;
	EDataBook *book;
	guint32 opid;
#ifdef HAVE_LIBGDATA_0_9
	GCancellable *cancellable;
	GDataContactsContact *new_contact;
	EContactPhoto *photo;
#endif /* HAVE_LIBGDATA_0_9 */
} CreateContactData;

static void
create_contact_finish (CreateContactData *data, GDataContactsContact *new_contact, const GError *gdata_error)
{
	EContact *e_contact;

	__debug__ (G_STRFUNC);

	if (gdata_error == NULL) {
		/* Add the new contact to the cache. If uploading the photo was successful, the photo's data is stored on the contact as the "photo"
		 * key, which the cache will pick up and store. */
		e_contact = cache_add_contact (data->backend, GDATA_ENTRY (new_contact));
		e_data_book_respond_create (data->book, data->opid, NULL, e_contact);
		g_object_unref (e_contact);
	} else {
		GError *book_error = NULL;

		/* Report the error. */
		data_book_error_from_gdata_error (&book_error, gdata_error);
		e_data_book_respond_create (data->book, data->opid, book_error, NULL);
	}

	finish_operation (data->backend, data->opid);

#ifdef HAVE_LIBGDATA_0_9
	if (data->photo != NULL) {
		e_contact_photo_free (data->photo);
	}

	if (data->new_contact != NULL) {
		g_object_unref (data->new_contact);
	}

	g_object_unref (data->cancellable);
#endif /* HAVE_LIBGDATA_0_9 */
	g_object_unref (data->book);
	g_object_unref (data->backend);
	g_slice_free (CreateContactData, data);
}

#ifdef HAVE_LIBGDATA_0_9
static void
create_contact_photo_query_cb (GDataService *service, GAsyncResult *async_result, CreateContactData *data)
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
create_contact_photo_cb (GDataContactsContact *contact, GAsyncResult *async_result, CreateContactData *data)
{
	GError *gdata_error = NULL;

	__debug__ (G_STRFUNC);

	gdata_contacts_contact_set_photo_finish (contact, async_result, &gdata_error);

	if (gdata_error == NULL) {
		/* Success! Store the photo on the final GDataContactsContact object so it makes it into the cache. */
		g_object_set_data_full (G_OBJECT (contact), "photo", data->photo, (GDestroyNotify) e_contact_photo_free);
		data->photo = NULL;

		/* We now have to re-query for the contact, since setting its photo changes the contact's ETag. */
		gdata_service_query_single_entry_async (E_BOOK_BACKEND_GOOGLE (data->backend)->priv->service,
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
#endif /* HAVE_LIBGDATA_0_9 */

static void
create_contact_cb (GDataService *service, GAsyncResult *result, CreateContactData *data)
{
	GError *gdata_error = NULL;
	GDataEntry *new_contact;

	__debug__ (G_STRFUNC);

	new_contact = gdata_service_insert_entry_finish (service, result, &gdata_error);

	if (!new_contact) {
		__debug__ ("Creating contact failed: %s", gdata_error->message);
		goto finish;
	}

#ifdef HAVE_LIBGDATA_0_9
	data->new_contact = g_object_ref (new_contact);

	/* Add a photo for the new contact, if appropriate. This has to be done before we respond to the contact creation operation so that
	 * we can update the EContact with the photo data and ETag. */
	if (data->photo != NULL) {
		gdata_contacts_contact_set_photo_async (GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
		                                        (const guint8 *) data->photo->data.inlined.data, data->photo->data.inlined.length,
		                                        data->photo->data.inlined.mime_type, data->cancellable,
		                                        (GAsyncReadyCallback) create_contact_photo_cb, data);
		return;
	}
#endif /* HAVE_LIBGDATA_0_9 */

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
e_book_backend_google_create_contact (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard_str)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact;
	GDataEntry *entry;
	gchar *xml;
	CreateContactData *data;

	__debug__ (G_STRFUNC);

	__debug__ ("Creating: %s", vcard_str);

	if (!priv->is_online) {
		e_data_book_respond_create (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
		return;
	}

	g_return_if_fail (backend_is_authorized (backend));

	/* Build the GDataEntry from the vCard */
	contact = e_contact_new_from_vcard (vcard_str);
	entry = _gdata_entry_new_from_e_contact (backend, contact);
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
#ifdef HAVE_LIBGDATA_0_9
	data->cancellable = g_object_ref (cancellable);
	data->new_contact = NULL;
	data->photo = g_object_steal_data (G_OBJECT (entry), "photo");
#endif /* HAVE_LIBGDATA_0_9 */

	gdata_contacts_service_insert_contact_async (GDATA_CONTACTS_SERVICE (priv->service), GDATA_CONTACTS_CONTACT (entry), cancellable,
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
remove_contact_cb (GDataService *service, GAsyncResult *result, RemoveContactData *data)
{
	GError *gdata_error = NULL;
	gboolean success;
	GSList *ids;

	__debug__ (G_STRFUNC);

	success = gdata_service_delete_entry_finish (service, result, &gdata_error);
	finish_operation (data->backend, data->opid);

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
e_book_backend_google_remove_contacts (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const GSList *id_list)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	const gchar *uid = id_list->data;
	GDataEntry *entry = NULL;
	EContact *cached_contact;
	RemoveContactData *data;

	__debug__ (G_STRFUNC);

	if (!priv->is_online) {
		e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
		return;
	}

	g_return_if_fail (backend_is_authorized (backend));

	/* We make the assumption that the ID list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
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
	#ifdef HAVE_LIBGDATA_0_9
	gdata_service_delete_entry_async (GDATA_SERVICE (priv->service), gdata_contacts_service_get_primary_authorization_domain (),
					  entry, cancellable, (GAsyncReadyCallback) remove_contact_cb, data);
	#else
	gdata_service_delete_entry_async (GDATA_SERVICE (priv->service), entry, cancellable, (GAsyncReadyCallback) remove_contact_cb, data);
	#endif
	g_object_unref (cancellable);
	g_object_unref (entry);
}

#ifdef HAVE_LIBGDATA_0_9
typedef enum {
	LEAVE_PHOTO,
	ADD_PHOTO,
	REMOVE_PHOTO,
	UPDATE_PHOTO,
} PhotoOperation;
#endif /* HAVE_LIBGDATA_0_9 */

typedef struct {
	EBookBackend *backend;
	EDataBook *book;
	guint32 opid;
#ifdef HAVE_LIBGDATA_0_9
	GCancellable *cancellable;
	GDataContactsContact *new_contact;
	EContactPhoto *photo;
	PhotoOperation photo_operation;
#endif /* HAVE_LIBGDATA_0_9 */
} ModifyContactData;

static void
modify_contact_finish (ModifyContactData *data, GDataContactsContact *new_contact, const GError *gdata_error)
{
	EContact *e_contact;

	__debug__ (G_STRFUNC);

	if (gdata_error == NULL) {
		/* Add the new entry to the cache */
		e_contact = cache_add_contact (data->backend, GDATA_ENTRY (new_contact));
		e_data_book_respond_modify (data->book, data->opid, NULL, e_contact);
		g_object_unref (e_contact);
	} else {
		GError *book_error = NULL;

		/* Report the error. */
		data_book_error_from_gdata_error (&book_error, gdata_error);
		e_data_book_respond_modify (data->book, data->opid, book_error, NULL);
	}

	finish_operation (data->backend, data->opid);

#ifdef HAVE_LIBGDATA_0_9
	if (data->photo != NULL) {
		e_contact_photo_free (data->photo);
	}

	if (data->new_contact != NULL) {
		g_object_unref (data->new_contact);
	}

	g_object_unref (data->cancellable);
#endif /* HAVE_LIBGDATA_0_9 */
	g_object_unref (data->book);
	g_object_unref (data->backend);
	g_slice_free (ModifyContactData, data);
}

#ifdef HAVE_LIBGDATA_0_9
static void
modify_contact_photo_query_cb (GDataService *service, GAsyncResult *async_result, ModifyContactData *data)
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
modify_contact_photo_cb (GDataContactsContact *contact, GAsyncResult *async_result, ModifyContactData *data)
{
	GError *gdata_error = NULL;

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
		gdata_service_query_single_entry_async (E_BOOK_BACKEND_GOOGLE (data->backend)->priv->service,
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
#endif /* HAVE_LIBGDATA_0_9 */

static void
modify_contact_cb (GDataService *service, GAsyncResult *result, ModifyContactData *data)
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

#ifdef HAVE_LIBGDATA_0_9
	data->new_contact = g_object_ref (new_contact);

	/* Add a photo for the new contact, if appropriate. This has to be done before we respond to the contact creation operation so that
	 * we can update the EContact with the photo data and ETag. */
	switch (data->photo_operation) {
		case LEAVE_PHOTO:
			/* Do nothing. */
			break;
		case ADD_PHOTO:
		case UPDATE_PHOTO:
			/* Set the photo. */
			g_return_if_fail (data->photo != NULL);
			gdata_contacts_contact_set_photo_async (GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
			                                        (const guint8 *) data->photo->data.inlined.data, data->photo->data.inlined.length,
			                                        data->photo->data.inlined.mime_type, data->cancellable,
			                                        (GAsyncReadyCallback) modify_contact_photo_cb, data);
			return;
		case REMOVE_PHOTO:
			/* Unset the photo. */
			g_return_if_fail (data->photo == NULL);
			gdata_contacts_contact_set_photo_async (GDATA_CONTACTS_CONTACT (new_contact), GDATA_CONTACTS_SERVICE (service),
			                                        NULL, 0, NULL, data->cancellable, (GAsyncReadyCallback) modify_contact_photo_cb, data);
			return;
		default:
			g_assert_not_reached ();
	}
#endif /* HAVE_LIBGDATA_0_9 */

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
e_book_backend_google_modify_contact (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *vcard_str)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EContact *contact, *cached_contact;
#ifdef HAVE_LIBGDATA_0_9
	EContactPhoto *old_photo, *new_photo;
#endif /* HAVE_LIBGDATA_0_9 */
	GDataEntry *entry = NULL;
	const gchar *uid;
	ModifyContactData *data;

	__debug__ (G_STRFUNC);

	__debug__ ("Updating: %s", vcard_str);

	if (!priv->is_online) {
		e_data_book_respond_modify (book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE), NULL);
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

		e_data_book_respond_modify (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
		return;
	}

	/* Update the old GDataEntry from the new contact */
	_gdata_entry_update_from_e_contact (backend, entry, contact);

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

#ifdef HAVE_LIBGDATA_0_9
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
	} else if (old_photo->data.inlined.length != new_photo->data.inlined.length ||
	           memcmp (old_photo->data.inlined.data, new_photo->data.inlined.data, old_photo->data.inlined.length) != 0) {
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
#endif /* HAVE_LIBGDATA_0_9 */

	#ifdef HAVE_LIBGDATA_0_9
	gdata_service_update_entry_async (GDATA_SERVICE (priv->service), gdata_contacts_service_get_primary_authorization_domain (),
					  entry, cancellable, (GAsyncReadyCallback) modify_contact_cb, data);
	#else
	gdata_service_update_entry_async (GDATA_SERVICE (priv->service), entry, cancellable, (GAsyncReadyCallback) modify_contact_cb, data);
	#endif
	g_object_unref (cancellable);

	g_object_unref (cached_contact);
	g_object_unref (contact);
	g_object_unref (entry);
}

static void
e_book_backend_google_get_contact (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *uid)
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
e_book_backend_google_get_contact_list (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *query)
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
e_book_backend_google_get_contact_list_uids (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *query)
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

static gboolean
on_refresh_idle (EBookBackend *backend)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	priv->idle_id = 0;
	cache_refresh_if_needed (backend);

	return FALSE;
}

static void
set_live_mode (EBookBackend *backend, gboolean live_mode)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

	__debug__ (G_STRFUNC);

	if (priv->live_mode == live_mode)
		return;

	priv->live_mode = live_mode;

	if (live_mode) {
		/* Entering live mode, we need to refresh */
		cache_refresh_if_needed (backend);
	} else if (priv->refresh_id > 0) {
		/* Leaving live mode, we can stop periodically refreshing */
		g_source_remove (priv->refresh_id);
		priv->refresh_id = 0;
	}
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
	e_data_book_view_notify_progress (bookview, -1, _("Loading…"));

	/* Ensure that we're ready to support a view */
	set_live_mode (backend, TRUE);

	/* Update the cache if necessary */
	if (cache_needs_update (backend, NULL)) {
		if (!backend_is_authorized (backend)) {
			/* We need authorization first */
			request_authorization (backend);
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
	GList *view;

	__debug__ (G_STRFUNC);

	/* Remove the view from the list of active views */
	if ((view = g_list_find (priv->bookviews, bookview)) != NULL) {
		priv->bookviews = g_list_delete_link (priv->bookviews, view);
		e_data_book_view_unref (bookview);
	}

	/* If there are no book views left, we can stop doing certain things, like refreshes */
	if (!priv->bookviews)
		set_live_mode (backend, FALSE);
}

typedef struct {
	EBookBackend *backend;
	guint32 opid;
} AuthenticateUserData;

#ifdef HAVE_LIBGDATA_0_9
static void
authenticate_client_login_cb (GDataClientLoginAuthorizer *authorizer,
                              GAsyncResult *result,
                              AuthenticateUserData *data)
{
	GError *gdata_error = NULL;
	GError *book_error = NULL;

	__debug__ (G_STRFUNC);

	/* Finish authenticating */
	gdata_client_login_authorizer_authenticate_finish (
		authorizer, result, &gdata_error);

	if (gdata_error != NULL) {
		data_book_error_from_gdata_error (&book_error, gdata_error);
		__debug__ ("Authentication failed: %s", gdata_error->message);
		g_error_free (gdata_error);
	}

	finish_operation (data->backend, data->opid);
	e_book_backend_notify_readonly (data->backend, gdata_error != NULL);
	e_book_backend_notify_opened (data->backend, book_error);

	g_object_unref (data->backend);
	g_slice_free (AuthenticateUserData, data);
}
#else
static void
authenticate_client_login_cb (GDataService *service,
                              GAsyncResult *result,
                              AuthenticateUserData *data)
{
	GError *gdata_error = NULL;
	GError *book_error = NULL;

	__debug__ (G_STRFUNC);

	/* Finish authenticating */
	gdata_service_authenticate_finish (service, result, &gdata_error);

	if (gdata_error != NULL) {
		data_book_error_from_gdata_error (&book_error, gdata_error);
		__debug__ ("Authentication failed: %s", gdata_error->message);
		g_error_free (gdata_error);
	}

	finish_operation (data->backend, data->opid);
	e_book_backend_notify_readonly (data->backend, gdata_error != NULL);
	e_book_backend_notify_opened (data->backend, book_error);

	g_object_unref (data->backend);
	g_slice_free (AuthenticateUserData, data);
}
#endif

#ifdef HAVE_LIBGDATA_0_9
#endif

static void
e_book_backend_google_authenticate_user (EBookBackend *backend, GCancellable *cancellable, ECredentials *credentials)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	AuthenticateUserData *data;
	guint32 opid;

	__debug__ (G_STRFUNC);

	if (!priv->is_online) {
		e_book_backend_notify_readonly (backend, TRUE);
		e_book_backend_notify_online (backend, FALSE);
		e_book_backend_notify_opened (backend, EDB_ERROR (SUCCESS));
		return;
	}

	if (backend_is_authorized (backend)) {
		g_warning ("Connection to Google already established.");
		e_book_backend_notify_readonly (backend, FALSE);
		e_book_backend_notify_opened (backend, NULL);
		return;
	}

	if (!credentials || !e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME) || !e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PASSWORD)) {
		e_book_backend_notify_opened (backend, EDB_ERROR (AUTHENTICATION_REQUIRED));
		return;
	}

	opid = -1;
	while (g_hash_table_lookup (priv->cancellables, GUINT_TO_POINTER (opid)))
		opid--;

	/* Authenticate with the server asynchronously */
	data = g_slice_new (AuthenticateUserData);
	data->backend = g_object_ref (backend);
	data->opid = opid;

	cancellable = start_operation (
		backend, opid, cancellable,
		_("Authenticating with the server…"));

#ifdef HAVE_LIBGDATA_0_9
	gdata_client_login_authorizer_authenticate_async (
		GDATA_CLIENT_LOGIN_AUTHORIZER (priv->authorizer),
		e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME),
		e_credentials_peek (credentials, E_CREDENTIALS_KEY_PASSWORD),
		cancellable,
		(GAsyncReadyCallback) authenticate_client_login_cb,
		data);
#else
	gdata_service_authenticate_async (
		priv->service,
		e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME),
		e_credentials_peek (credentials, E_CREDENTIALS_KEY_PASSWORD),
		cancellable,
		(GAsyncReadyCallback) authenticate_client_login_cb,
		data);
#endif

	g_object_unref (cancellable);
}

static void
e_book_backend_google_remove (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable)
{
	__debug__ (G_STRFUNC);
	e_data_book_respond_remove (book, opid, NULL);
}

static void
e_book_backend_google_open (EBookBackend *backend, EDataBook *book, guint opid, GCancellable *cancellable, gboolean only_if_exists)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	const gchar *refresh_interval_str, *use_ssl_str, *use_cache_str;
	guint refresh_interval;
	gboolean use_ssl, use_cache;
	ESource *source = e_book_backend_get_source (backend);

	__debug__ (G_STRFUNC);

	if (priv->cancellables) {
		e_book_backend_respond_opened (backend, book, opid, EDB_ERROR_EX (OTHER_ERROR, "Source already loaded!"));
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
	priv->groups_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->groups_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->cancellables = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
	cache_init (backend, use_cache);
	priv->use_ssl = use_ssl;
	priv->refresh_interval = refresh_interval;

	/* Remove and re-add the timeout */
	if (priv->refresh_id != 0) {
		g_source_remove (priv->refresh_id);
		priv->refresh_id = g_timeout_add_seconds (priv->refresh_interval, (GSourceFunc) on_refresh_timeout, backend);
	}

	/* Set up ready to be interacted with */
	e_book_backend_notify_online (backend, priv->is_online);
	e_book_backend_notify_readonly (backend, TRUE);

	if (priv->is_online) {
		request_authorization (backend);

#ifdef HAVE_LIBGDATA_0_9
		/* Refresh the authorizer.  This may block. */
		gdata_authorizer_refresh_authorization (
			priv->authorizer, cancellable, NULL);
#endif
	}

	if (!priv->is_online || backend_is_authorized (backend))
		e_book_backend_notify_opened (backend, NULL /* Success */);

	e_data_book_respond_open (book, opid, NULL /* Success */);
}

static void
e_book_backend_google_get_backend_property (EBookBackend *backend, EDataBook *book, guint32 opid, GCancellable *cancellable, const gchar *prop_name)
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
			E_CONTACT_HOMEPAGE_URL,
			E_CONTACT_BLOG_URL,
			E_CONTACT_BIRTH_DATE,
			E_CONTACT_ANNIVERSARY,
			E_CONTACT_NOTE,
#ifdef HAVE_LIBGDATA_0_9
			E_CONTACT_PHOTO,
#endif /* HAVE_LIBGDATA_0_9 */
			E_CONTACT_CATEGORIES,
			E_CONTACT_CATEGORY_LIST
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
	GHashTableIter iter;
	gpointer opid_ptr;
	GCancellable *cancellable;
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;

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
e_book_backend_google_set_online (EBookBackend *backend, gboolean is_online)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	__debug__ (G_STRFUNC);

	if (is_online == priv->is_online)
		return;

	priv->is_online = is_online;

	e_book_backend_notify_online (backend, is_online);

	if (is_online && e_book_backend_is_opened (backend)) {
		request_authorization (backend);
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
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (object)->priv;

	__debug__ (G_STRFUNC);

	/* Cancel all outstanding operations */
	google_cancel_all_operations (E_BOOK_BACKEND (object));

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

#ifdef HAVE_LIBGDATA_0_9
	if (priv->authorizer != NULL)
		g_object_unref (priv->authorizer);
	priv->authorizer = NULL;
#endif

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

	if (priv->cancellables) {
		g_hash_table_destroy (priv->groups_by_id);
		g_hash_table_destroy (priv->groups_by_name);
		g_hash_table_destroy (priv->cancellables);
	}

	G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (klass);

	g_type_class_add_private (klass, sizeof (EBookBackendGooglePrivate));

	/* Set the virtual methods. */
	backend_class->open			= e_book_backend_google_open;
	backend_class->get_backend_property	= e_book_backend_google_get_backend_property;
	backend_class->start_book_view		= e_book_backend_google_start_book_view;
	backend_class->stop_book_view		= e_book_backend_google_stop_book_view;
	backend_class->set_online		= e_book_backend_google_set_online;
	backend_class->remove			= e_book_backend_google_remove;
	backend_class->create_contact		= e_book_backend_google_create_contact;
	backend_class->remove_contacts		= e_book_backend_google_remove_contacts;
	backend_class->modify_contact		= e_book_backend_google_modify_contact;
	backend_class->get_contact		= e_book_backend_google_get_contact;
	backend_class->get_contact_list		= e_book_backend_google_get_contact_list;
	backend_class->get_contact_list_uids	= e_book_backend_google_get_contact_list_uids;
	backend_class->authenticate_user	= e_book_backend_google_authenticate_user;

	object_class->dispose  = e_book_backend_google_dispose;
	object_class->finalize = e_book_backend_google_finalize;

	__e_book_backend_google_debug__ = g_getenv ("GOOGLE_BACKEND_DEBUG") ? TRUE : FALSE;
}

static void
e_book_backend_google_init (EBookBackendGoogle *backend)
{
	__debug__ (G_STRFUNC);
	backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		backend, E_TYPE_BOOK_BACKEND_GOOGLE,
		EBookBackendGooglePrivate);

	/* Set up our EProxy. */
	backend->priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (backend->priv->proxy);

	g_signal_connect (
		backend->priv->proxy, "changed",
		G_CALLBACK (proxy_settings_changed), backend);
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
data_book_error_from_gdata_error (GError **dest_err, const GError *error)
{
	if (!error || !dest_err)
		return;

	/* only last error is used */
	g_clear_error (dest_err);

	#ifdef HAVE_LIBGDATA_0_9
	if (error->domain == GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR) {
	#else
	if (error->domain == GDATA_AUTHENTICATION_ERROR) {
	#endif
		/* Authentication errors */
		switch (error->code) {
		#ifdef HAVE_LIBGDATA_0_9
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_BAD_AUTHENTICATION:
		#else
		case GDATA_AUTHENTICATION_ERROR_BAD_AUTHENTICATION:
		#endif
			g_propagate_error (dest_err, EDB_ERROR (AUTHENTICATION_FAILED));
			return;
		#ifdef HAVE_LIBGDATA_0_9
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_NOT_VERIFIED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_TERMS_NOT_AGREED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_CAPTCHA_REQUIRED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DELETED:
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DISABLED:
		#else
		case GDATA_AUTHENTICATION_ERROR_NOT_VERIFIED:
		case GDATA_AUTHENTICATION_ERROR_TERMS_NOT_AGREED:
		case GDATA_AUTHENTICATION_ERROR_CAPTCHA_REQUIRED:
		case GDATA_AUTHENTICATION_ERROR_ACCOUNT_DELETED:
		case GDATA_AUTHENTICATION_ERROR_ACCOUNT_DISABLED:
		#endif
			g_propagate_error (dest_err, EDB_ERROR (PERMISSION_DENIED));
			return;
		#ifdef HAVE_LIBGDATA_0_9
		case GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_SERVICE_DISABLED:
		#else
		case GDATA_AUTHENTICATION_ERROR_SERVICE_DISABLED:
		#endif
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

#define GOOGLE_PRIMARY_PARAM "X-EVOLUTION-UI-SLOT"
#define GOOGLE_LABEL_PARAM "X-GOOGLE-LABEL"
#define GDATA_ENTRY_XML_ATTR "X-GDATA-ENTRY-XML"
#define GDATA_ENTRY_LINK_ATTR "X-GDATA-ENTRY-LINK"

static void add_attribute_from_gdata_gd_email_address (EVCard *vcard, GDataGDEmailAddress *email);
static void add_attribute_from_gdata_gd_im_address (EVCard *vcard, GDataGDIMAddress *im);
static void add_attribute_from_gdata_gd_phone_number (EVCard *vcard, GDataGDPhoneNumber *number);
static void add_attribute_from_gdata_gd_postal_address (EVCard *vcard, GDataGDPostalAddress *address);
static void add_attribute_from_gdata_gd_organization (EVCard *vcard, GDataGDOrganization *org);

static GDataGDEmailAddress *gdata_gd_email_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDIMAddress *gdata_gd_im_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPhoneNumber *gdata_gd_phone_number_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPostalAddress *gdata_gd_postal_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDOrganization *gdata_gd_organization_from_attribute (EVCardAttribute *attr, gboolean *primary);

static gboolean is_known_google_im_protocol (const gchar *protocol);

static GDataEntry *
_gdata_entry_new_from_e_contact (EBookBackend *backend, EContact *contact)
{
	GDataEntry *entry = GDATA_ENTRY (gdata_contacts_contact_new (NULL));

	if (_gdata_entry_update_from_e_contact (backend, entry, contact))
		return entry;

	g_object_unref (entry);

	return NULL;
}

static void
remove_anniversary (GDataContactsContact *contact)
{
	GList *events, *itr;

	events = gdata_contacts_contact_get_events (contact);
	if (!events)
		return;

	events = g_list_copy (events);
	g_list_foreach (events, (GFunc) g_object_ref, NULL);

	gdata_contacts_contact_remove_all_events (contact);
	for (itr = events; itr; itr = itr->next) {
		GDataGContactEvent *event = itr->data;

		if (g_strcmp0 (gdata_gcontact_event_get_relation_type (event), GDATA_GCONTACT_EVENT_ANNIVERSARY) != 0)
			gdata_contacts_contact_add_event (contact, event);
	}

	g_list_foreach (events, (GFunc) g_object_unref, NULL);
	g_list_free (events);
}

static void
remove_websites (GDataContactsContact *contact)
{
	GList *websites, *itr;

	websites = gdata_contacts_contact_get_websites (contact);
	if (!websites)
		return;

	websites = g_list_copy (websites);
	g_list_foreach (websites, (GFunc) g_object_ref, NULL);

	gdata_contacts_contact_remove_all_websites (contact);
	for (itr = websites; itr; itr = itr->next) {
		GDataGContactWebsite *website = itr->data;

		if (g_strcmp0 (gdata_gcontact_website_get_relation_type (website), GDATA_GCONTACT_WEBSITE_HOME_PAGE) != 0 &&
		    g_strcmp0 (gdata_gcontact_website_get_relation_type (website), GDATA_GCONTACT_WEBSITE_BLOG) != 0)
			gdata_contacts_contact_add_website (contact, website);
	}

	g_list_foreach (websites, (GFunc) g_object_unref, NULL);
	g_list_free (websites);
}

static gboolean
_gdata_entry_update_from_e_contact (EBookBackend *backend, GDataEntry *entry, EContact *contact)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	GList *attributes, *iter, *category_names;
	EContactName *name_struct = NULL;
#ifdef HAVE_LIBGDATA_0_9
	EContactPhoto *photo;
#endif /* HAVE_LIBGDATA_0_9 */
	gboolean have_email_primary = FALSE;
	gboolean have_im_primary = FALSE;
	gboolean have_phone_primary = FALSE;
	gboolean have_postal_primary = FALSE;
	gboolean have_org_primary = FALSE;
	const gchar *title, *role, *note;
	EContactDate *bdate;
	const gchar *url;

	attributes = e_vcard_get_attributes (E_VCARD (contact));

	/* N and FN */
	name_struct = e_contact_get (contact, E_CONTACT_NAME);
	if (name_struct) {
		GDataGDName *name;
		const gchar *given = NULL, *family = NULL;

		if (name_struct->given && *(name_struct->given) != '\0')
			given = name_struct->given;
		if (name_struct->family && *(name_struct->family) != '\0')
			family = name_struct->family;

		name = gdata_gd_name_new (given, family);
		if (name_struct->additional && *(name_struct->additional) != '\0')
			gdata_gd_name_set_additional_name (name, name_struct->additional);
		if (name_struct->prefixes && *(name_struct->prefixes) != '\0')
			gdata_gd_name_set_prefix (name, name_struct->prefixes);
		if (name_struct->suffixes && *(name_struct->suffixes) != '\0')
			gdata_gd_name_set_suffix (name, name_struct->suffixes);
		gdata_gd_name_set_full_name (name, e_contact_get (contact, E_CONTACT_FULL_NAME));

		gdata_contacts_contact_set_name (GDATA_CONTACTS_CONTACT (entry), name);
		g_object_unref (name);
	}

	/* NOTE */
	note = e_contact_get (contact, E_CONTACT_NOTE);
	if (note)
		gdata_entry_set_content (entry, note);
	else
		gdata_entry_set_content (entry, NULL);

	/* Clear out all the old attributes */
	gdata_contacts_contact_remove_all_email_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_phone_numbers (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_postal_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_im_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_organizations (GDATA_CONTACTS_CONTACT (entry));

	category_names = gdata_contacts_contact_get_groups (GDATA_CONTACTS_CONTACT (entry));
	for (iter = category_names; iter != NULL; iter = g_list_delete_link (iter, iter))
		gdata_contacts_contact_remove_group (GDATA_CONTACTS_CONTACT (entry), iter->data);

	/* We walk them in reverse order, so we can find
	 * the correct primaries */
	iter = g_list_last (attributes);
	for (; iter; iter = iter->prev) {
		EVCardAttribute *attr;
		const gchar *name;

		attr = iter->data;
		name = e_vcard_attribute_get_name (attr);

		if (0 == g_ascii_strcasecmp (name, EVC_UID) ||
		    0 == g_ascii_strcasecmp (name, EVC_N) ||
		    0 == g_ascii_strcasecmp (name, EVC_FN) ||
		    0 == g_ascii_strcasecmp (name, EVC_LABEL) ||
		    0 == g_ascii_strcasecmp (name, EVC_VERSION) ||
		    0 == g_ascii_strcasecmp (name, EVC_X_FILE_AS) ||
		    0 == g_ascii_strcasecmp (name, EVC_TITLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_ROLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_NOTE) ||
		    0 == g_ascii_strcasecmp (name, EVC_CATEGORIES) ||
		    0 == g_ascii_strcasecmp (name, EVC_PHOTO)) {
			/* Ignore UID, VERSION, X-EVOLUTION-FILE-AS, N, FN, LABEL, TITLE, ROLE, NOTE, CATEGORIES, PHOTO */
		} else if (0 == g_ascii_strcasecmp (name, EVC_EMAIL)) {
			/* EMAIL */
			GDataGDEmailAddress *email;

			email = gdata_gd_email_address_from_attribute (attr, &have_email_primary);
			if (email) {
				gdata_contacts_contact_add_email_address (GDATA_CONTACTS_CONTACT (entry), email);
				g_object_unref (email);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_TEL)) {
			/* TEL */
			GDataGDPhoneNumber *number;

			number = gdata_gd_phone_number_from_attribute (attr, &have_phone_primary);
			if (number) {
				gdata_contacts_contact_add_phone_number (GDATA_CONTACTS_CONTACT (entry), number);
				g_object_unref (number);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_ADR)) {
			/* ADR (we ignore LABEL, since it should be the same as ADR, and ADR is more structured) */
			GDataGDPostalAddress *address;

			address = gdata_gd_postal_address_from_attribute (attr, &have_postal_primary);
			if (address) {
				gdata_contacts_contact_add_postal_address (GDATA_CONTACTS_CONTACT (entry), address);
				g_object_unref (address);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_ORG)) {
			/* ORG */
			GDataGDOrganization *org;

			org = gdata_gd_organization_from_attribute (attr, &have_org_primary);
			if (org) {
				gdata_contacts_contact_add_organization (GDATA_CONTACTS_CONTACT (entry), org);
				g_object_unref (org);
			}
		} else if (0 == g_ascii_strncasecmp (name, "X-", 2) && is_known_google_im_protocol (name + 2)) {
			/* X-IM */
			GDataGDIMAddress *im;

			im = gdata_gd_im_address_from_attribute (attr, &have_im_primary);
			if (im) {
				gdata_contacts_contact_add_im_address (GDATA_CONTACTS_CONTACT (entry), im);
				g_object_unref (im);
			}
		} else if (e_vcard_attribute_is_single_valued (attr)) {
			gchar *value;

			/* Add the attribute as an extended property */
			value = e_vcard_attribute_get_value (attr);
			gdata_contacts_contact_set_extended_property (GDATA_CONTACTS_CONTACT (entry), name, value);
			g_free (value);
		} else {
			GList *values;

			values = e_vcard_attribute_get_values (attr);
			if (values && values->data && ((gchar *) values->data)[0])
				__debug__ ("unsupported vcard field: %s: %s", name, (gchar *)values->data);
		}
	}

	/* TITLE and ROLE */
	title = e_contact_get (contact, E_CONTACT_TITLE);
	role = e_contact_get (contact, E_CONTACT_ROLE);
	if (title || role) {
		GDataGDOrganization *org = NULL;

		/* Find an appropriate org: try to add them to the primary organization, but fall back to the first listed organization if none
		 * are marked as primary. */
		if (have_org_primary) {
			org = gdata_contacts_contact_get_primary_organization (GDATA_CONTACTS_CONTACT (entry));
		} else {
			GList *orgs = gdata_contacts_contact_get_organizations (GDATA_CONTACTS_CONTACT (entry));
			if (orgs)
				org = orgs->data;
		}

		/* Set the title and role */
		if (org && title)
			gdata_gd_organization_set_title (org, title);
		if (org && role)
			gdata_gd_organization_set_job_description (org, role);
	}

	remove_websites (GDATA_CONTACTS_CONTACT (entry));

	url = e_contact_get_const (contact, E_CONTACT_HOMEPAGE_URL);
	if (url && *url) {
		GDataGContactWebsite *website = gdata_gcontact_website_new (url, GDATA_GCONTACT_WEBSITE_HOME_PAGE, NULL, FALSE);
		if (website) {
			gdata_contacts_contact_add_website (GDATA_CONTACTS_CONTACT (entry), website);
			g_object_unref (website);
		}
	}

	url = e_contact_get_const (contact, E_CONTACT_BLOG_URL);
	if (url && *url) {
		GDataGContactWebsite *website = gdata_gcontact_website_new (url, GDATA_GCONTACT_WEBSITE_BLOG, NULL, FALSE);
		if (website) {
			gdata_contacts_contact_add_website (GDATA_CONTACTS_CONTACT (entry), website);
			g_object_unref (website);
		}
	}

	gdata_contacts_contact_set_birthday (GDATA_CONTACTS_CONTACT (entry), NULL, TRUE);
	bdate = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (bdate) {
		GDate *gdate = g_date_new_dmy (bdate->day, bdate->month, bdate->year);

		if (gdate) {
			gdata_contacts_contact_set_birthday (GDATA_CONTACTS_CONTACT (entry), gdate, TRUE);
			g_date_free (gdate);
		}
		e_contact_date_free (bdate);
	}

	remove_anniversary (GDATA_CONTACTS_CONTACT (entry));
	bdate = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	if (bdate) {
		GDate *gdate = g_date_new_dmy (bdate->day, bdate->month, bdate->year);

		if (gdate) {
			GDataGContactEvent *anni = gdata_gcontact_event_new (gdate, GDATA_GCONTACT_EVENT_ANNIVERSARY, NULL);

			if (anni) {
				gdata_contacts_contact_add_event (GDATA_CONTACTS_CONTACT (entry), anni);
				g_object_unref (anni);
			}

			g_date_free (gdate);
		}
		e_contact_date_free (bdate);
	}

	/* CATEGORIES */
	for (category_names = e_contact_get (contact, E_CONTACT_CATEGORY_LIST); category_names != NULL; category_names = category_names->next) {
		gchar *category_id;
		const gchar *category_name = category_names->data;

		if (category_name == NULL || *category_name == '\0')
			continue;

		category_id = g_strdup (g_hash_table_lookup (priv->groups_by_name, category_name));
		if (category_id == NULL) {
			GError *error = NULL;

			category_id = create_group (backend, category_name, &error);
			if (category_id == NULL) {
				g_warning ("Error creating group '%s': %s", category_name, error->message);
				g_error_free (error);
				continue;
			}
		}

		gdata_contacts_contact_add_group (GDATA_CONTACTS_CONTACT (entry), category_id);
		g_free (category_id);
	}

#ifdef HAVE_LIBGDATA_0_9
	/* PHOTO */
	photo = e_contact_get (contact, E_CONTACT_PHOTO);

	if (photo != NULL && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		g_object_set_data_full (G_OBJECT (entry), "photo", photo, (GDestroyNotify) e_contact_photo_free);
	} else {
		g_object_set_data (G_OBJECT (entry), "photo", NULL);

		if (photo != NULL) {
			e_contact_photo_free (photo);
		}
	}
#endif /* HAVE_LIBGDATA_0_9 */

	return TRUE;
}

static void
foreach_extended_props_cb (const gchar *name, const gchar *value, EVCard *vcard)
{
	EVCardAttribute *attr;

	attr = e_vcard_attribute_new (NULL, name);
	e_vcard_add_attribute_with_value (vcard, attr, value);
}

static EContact *
_e_contact_new_from_gdata_entry (EBookBackend *backend, GDataEntry *entry)
{
	EBookBackendGooglePrivate *priv = E_BOOK_BACKEND_GOOGLE (backend)->priv;
	EVCard *vcard;
	EVCardAttribute *attr;
#ifdef HAVE_LIBGDATA_0_9
	EContactPhoto *photo;
	const gchar *photo_etag;
#endif /* HAVE_LIBGDATA_0_9 */
	GList *email_addresses, *im_addresses, *phone_numbers, *postal_addresses, *orgs, *category_names, *category_ids;
	const gchar *uid, *note;
	GList *itr;
	GDataGDName *name;
	GDataGDEmailAddress *email;
	GDataGDIMAddress *im;
	GDataGDPhoneNumber *phone_number;
	GDataGDPostalAddress *postal_address;
	GDataGDOrganization *org;
	GHashTable *extended_props;
	GList *websites, *events;
	GDate bdate;
	gboolean bdate_has_year;

	uid = gdata_entry_get_id (entry);
	if (NULL == uid)
		return NULL;

	vcard = E_VCARD (e_contact_new ());

	/* UID */
	attr = e_vcard_attribute_new (NULL, EVC_UID);
	e_vcard_add_attribute_with_value (vcard, attr, uid);

	/* FN, N */
	name = gdata_contacts_contact_get_name (GDATA_CONTACTS_CONTACT (entry));
	if (name) {
		EContactName name_struct;

		/* Set the full name */
		e_contact_set (E_CONTACT (vcard), E_CONTACT_FULL_NAME, gdata_gd_name_get_full_name (name));

		/* We just need to set the E_CONTACT_NAME field, and all the other name attribute values
		 * in the EContact will be populated automatically from that */
		name_struct.family = (gchar *) gdata_gd_name_get_family_name (name);
		name_struct.given = (gchar *) gdata_gd_name_get_given_name (name);
		name_struct.additional = (gchar *) gdata_gd_name_get_additional_name (name);
		name_struct.prefixes = (gchar *) gdata_gd_name_get_prefix (name);
		name_struct.suffixes = (gchar *) gdata_gd_name_get_suffix (name);

		e_contact_set (E_CONTACT (vcard), E_CONTACT_NAME, &name_struct);
	}

	/* NOTE */
	note = gdata_entry_get_content (entry);
	if (note)
		e_contact_set (E_CONTACT (vcard), E_CONTACT_NOTE, note);

	/* EMAIL - primary first */
	email = gdata_contacts_contact_get_primary_email_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_email_address (vcard, email);

	email_addresses = gdata_contacts_contact_get_email_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = email_addresses; itr; itr = itr->next) {
		email = itr->data;
		if (gdata_gd_email_address_is_primary (email) == TRUE)
			continue;
		add_attribute_from_gdata_gd_email_address (vcard, email);
	}

	/* X-IM - primary first */
	im = gdata_contacts_contact_get_primary_im_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_im_address (vcard, im);

	im_addresses = gdata_contacts_contact_get_im_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = im_addresses; itr; itr = itr->next) {
		im = itr->data;
		if (gdata_gd_im_address_is_primary (im) == TRUE)
			continue;
		add_attribute_from_gdata_gd_im_address (vcard, im);
	}

	/* TEL - primary first */
	phone_number = gdata_contacts_contact_get_primary_phone_number (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_phone_number (vcard, phone_number);

	phone_numbers = gdata_contacts_contact_get_phone_numbers (GDATA_CONTACTS_CONTACT (entry));
	for (itr = phone_numbers; itr; itr = itr->next) {
		phone_number = itr->data;
		if (gdata_gd_phone_number_is_primary (phone_number) == TRUE)
			continue;
		add_attribute_from_gdata_gd_phone_number (vcard, phone_number);
	}

	/* LABEL and ADR - primary first */
	postal_address = gdata_contacts_contact_get_primary_postal_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_postal_address (vcard, postal_address);

	postal_addresses = gdata_contacts_contact_get_postal_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = postal_addresses; itr; itr = itr->next) {
		postal_address = itr->data;
		if (gdata_gd_postal_address_is_primary (postal_address) == TRUE)
			continue;
		add_attribute_from_gdata_gd_postal_address (vcard, postal_address);
	}

	/* ORG - primary first */
	org = gdata_contacts_contact_get_primary_organization (GDATA_CONTACTS_CONTACT (entry));
	orgs = gdata_contacts_contact_get_organizations (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_organization (vcard, org);

	if (org || orgs) {
		if (!org)
			org = orgs->data;

		/* EVC_TITLE and EVC_ROLE from the primary organization (or the first organization in the list if there isn't a primary org) */
		attr = e_vcard_attribute_new (NULL, EVC_TITLE);
		e_vcard_add_attribute_with_value (vcard, attr, gdata_gd_organization_get_title (org));

		attr = e_vcard_attribute_new (NULL, EVC_ROLE);
		e_vcard_add_attribute_with_value (vcard, attr, gdata_gd_organization_get_job_description (org));
	}

	for (itr = orgs; itr; itr = itr->next) {
		org = itr->data;
		if (gdata_gd_organization_is_primary (org) == TRUE)
			continue;
		add_attribute_from_gdata_gd_organization (vcard, org);
	}

	/* CATEGORIES */
	category_ids = gdata_contacts_contact_get_groups (GDATA_CONTACTS_CONTACT (entry));
	category_names = NULL;
	for (itr = category_ids; itr != NULL; itr = g_list_delete_link (itr, itr)) {
		gchar *category_id, *category_name;

		category_id = sanitise_group_id (itr->data);
		category_name = g_hash_table_lookup (priv->groups_by_id, category_id);
		g_free (category_id);

		if (category_name != NULL)
			category_names = g_list_prepend (category_names, category_name);
		else
			g_warning ("Couldn't find name for category with ID '%s'.", category_id);
	}

	e_contact_set (E_CONTACT (vcard), E_CONTACT_CATEGORY_LIST, category_names);
	g_list_free (category_names);

	/* Extended properties */
	extended_props = gdata_contacts_contact_get_extended_properties (GDATA_CONTACTS_CONTACT (entry));
	g_hash_table_foreach (extended_props, (GHFunc) foreach_extended_props_cb, vcard);

	websites = gdata_contacts_contact_get_websites (GDATA_CONTACTS_CONTACT (entry));
	for (itr = websites; itr != NULL; itr = itr->next) {
		GDataGContactWebsite *website = itr->data;
		const gchar *uri, *reltype;

		if (!website)
			continue;

		uri = gdata_gcontact_website_get_uri (website);
		reltype = gdata_gcontact_website_get_relation_type (website);

		if (!uri || !*uri || !reltype)
			continue;

		if (g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_HOME_PAGE))
			e_contact_set (E_CONTACT (vcard), E_CONTACT_HOMEPAGE_URL, uri);
		else if (g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_BLOG))
			e_contact_set (E_CONTACT (vcard), E_CONTACT_BLOG_URL, uri);
	}

	g_date_clear (&bdate, 1);
	bdate_has_year = gdata_contacts_contact_get_birthday (GDATA_CONTACTS_CONTACT (entry), &bdate);
	if (!bdate_has_year) {
		GTimeVal curr_time = { 0 };
		GDate tmp_date;

		g_get_current_time (&curr_time);
		g_date_clear (&tmp_date, 1);
		g_date_set_time_val (&tmp_date, &curr_time);

		g_date_set_year (&bdate, g_date_get_year (&tmp_date));
	}

	if (g_date_valid (&bdate)) {
		EContactDate *date = e_contact_date_new ();

		if (date) {
			date->day = g_date_get_day (&bdate);
			date->month = g_date_get_month (&bdate);
			date->year = g_date_get_year (&bdate);

			e_contact_set (E_CONTACT (vcard), E_CONTACT_BIRTH_DATE, date);
			e_contact_date_free (date);
		}
	}

	events = gdata_contacts_contact_get_events (GDATA_CONTACTS_CONTACT (entry));
	for (itr = events; itr; itr = itr->next) {
		GDataGContactEvent *event = itr->data;

		if (!event)
			continue;

		if (!gdata_gcontact_event_get_relation_type (event) ||
		    !g_str_equal (gdata_gcontact_event_get_relation_type (event), GDATA_GCONTACT_EVENT_ANNIVERSARY))
			continue;

		g_date_clear (&bdate, 1);
		gdata_gcontact_event_get_date (event, &bdate);

		if (g_date_valid (&bdate)) {
			EContactDate *date = e_contact_date_new ();

			if (date) {
				date->day = g_date_get_day (&bdate);
				date->month = g_date_get_month (&bdate);
				date->year = g_date_get_year (&bdate);

				e_contact_set (E_CONTACT (vcard), E_CONTACT_ANNIVERSARY, date);
				e_contact_date_free (date);
			}
		}

		break;
	}

#ifdef HAVE_LIBGDATA_0_9
	/* PHOTO */
	photo = g_object_get_data (G_OBJECT (entry), "photo");
	photo_etag = gdata_contacts_contact_get_photo_etag (GDATA_CONTACTS_CONTACT (entry));

	g_return_val_if_fail ((photo == NULL) == (photo_etag == NULL), NULL);

	if (photo != NULL) {
		/* Photo */
		e_contact_set (E_CONTACT (vcard), E_CONTACT_PHOTO, photo);

		/* ETag */
		attr = e_vcard_attribute_new ("", GDATA_PHOTO_ETAG_ATTR);
		e_vcard_attribute_add_value (attr, photo_etag);
		e_vcard_add_attribute (vcard, attr);
	}
#endif /* HAVE_LIBGDATA_0_9 */

	return E_CONTACT (vcard);
}

static void
_e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry)
{
	EVCardAttribute *attr;
	gchar *entry_xml;
	GDataLink *edit_link;

	/* Cache the XML representing the entry */
	entry_xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	attr = e_vcard_attribute_new ("", GDATA_ENTRY_XML_ATTR);
	e_vcard_attribute_add_value (attr, entry_xml);
	e_vcard_add_attribute (E_VCARD (contact), attr);
	g_free (entry_xml);

	/* Also add the update URI for the entry, since that's not serialised by gdata_parsable_get_xml */
	edit_link = gdata_entry_look_up_link (entry, GDATA_LINK_EDIT);
	if (edit_link != NULL) {
		attr = e_vcard_attribute_new ("", GDATA_ENTRY_LINK_ATTR);
		e_vcard_attribute_add_value (attr, gdata_link_get_uri (edit_link));
		e_vcard_add_attribute (E_VCARD (contact), attr);
	}
}

static void
_e_contact_remove_gdata_entry_xml (EContact *contact)
{
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_XML_ATTR);
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_LINK_ATTR);
}

static const gchar *
_e_contact_get_gdata_entry_xml (EContact *contact, const gchar **edit_uri)
{
	EVCardAttribute *attr;
	GList *values = NULL;

	/* Return the edit URI if asked */
	if (edit_uri != NULL) {
		attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_LINK_ATTR);
		if (attr != NULL)
			values = e_vcard_attribute_get_values (attr);
		if (values != NULL)
			*edit_uri = values->data;
	}

	/* Return the entry's XML */
	attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_XML_ATTR);
	values = e_vcard_attribute_get_values (attr);

	return values ? values->data : NULL;
}

struct RelTypeMap {
	const gchar *rel;
	const gchar *types[2];
};

/* NOTE: These maps must be kept ordered with the one-to-many types first */
static const struct RelTypeMap rel_type_map_phone[] = {
	{ "home", { "HOME", "VOICE" }},
	{ "home_fax", { "HOME", "FAX" }},
	{ "work", { "WORK", "VOICE" }},
	{ "work_fax", { "WORK", "FAX" }},
	{ "work_mobile", { "WORK", "CELL" }},
	{ "work_pager", { "WORK", "PAGER" }},
	{ "assistant", { EVC_X_ASSISTANT, NULL }},
	{ "callback", { EVC_X_CALLBACK, NULL }},
	{ "car", { "CAR", NULL }},
	{ "company_main", {EVC_X_COMPANY, NULL }},
	{ "fax", { "FAX", NULL }},
	{ "isdn", { "ISDN", NULL }},
	{ "main", { "PREF", NULL }},
	{ "mobile", { "CELL", NULL }},
	{ "other", { "VOICE", NULL }},
	{ "other_fax", { "FAX", NULL }},
	{ "pager", { "PAGER", NULL }},
	{ "radio", { EVC_X_RADIO, NULL }},
	{ "telex", { EVC_X_TELEX, NULL }},
	{ "tty_tdd", { EVC_X_TTYTDD, NULL }}
};

static const struct RelTypeMap rel_type_map_im[] = {
	{ "home", { "HOME", NULL }},
	{ "netmeeting", { "NETMEETING", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static const struct RelTypeMap rel_type_map_others[] = {
	{ "home", { "HOME", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static gboolean
_add_type_param_from_google_rel (EVCardAttribute *attr, const struct RelTypeMap rel_type_map[], guint map_len, const gchar *rel)
{
	const gchar * field;
	guint i;

	field = strstr (rel ? rel : "", "#");
	if (NULL == field)
		return FALSE;

	field++;
	for (i = 0; i < map_len; i++) {
		if (0 == g_ascii_strcasecmp (rel_type_map[i].rel, field)) {
			EVCardAttributeParam *param;
			param = e_vcard_attribute_param_new ("TYPE");
			e_vcard_attribute_param_add_value (param, rel_type_map[i].types[0]);
			if (rel_type_map[i].types[1])
				e_vcard_attribute_param_add_value (param, rel_type_map[i].types[1]);
			e_vcard_attribute_add_param (attr, param);
			return TRUE;
		}
	}
	g_warning ("Unknown relationship '%s'", rel);

	return TRUE;
}

static gboolean
add_type_param_from_google_rel_phone (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone), rel);
}

static gboolean
add_type_param_from_google_rel_im (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_im, G_N_ELEMENTS (rel_type_map_im), rel);
}

static gboolean
add_type_param_from_google_rel (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others), rel);
}

static void
add_label_param (EVCardAttribute *attr, const gchar *label)
{
	if (label && label[0] != '\0') {
		EVCardAttributeParam *param;
		param = e_vcard_attribute_param_new (GOOGLE_LABEL_PARAM);
		e_vcard_attribute_add_param_with_value (attr, param, label);
	}
}

static gchar *
_google_rel_from_types (GList *types, const struct RelTypeMap rel_type_map[], guint map_len)
{
	const gchar format[] = "http://schemas.google.com/g/2005#%s";
	guint i;

	/* For each of the entries in the map... */
	for (i = 0; i < map_len; i++) {
		GList *cur;
		gboolean first_matched = FALSE, second_matched = rel_type_map[i].types[1] ? FALSE : TRUE;

		/* ...iterate through all the vCard's types and see if two of them match the types in the current map entry. */
		for (cur = types; cur != NULL; cur = cur->next) {
			if (0 == g_ascii_strcasecmp (rel_type_map[i].types[0], cur->data))
				first_matched = TRUE;
			else if (!rel_type_map[i].types[1] || 0 == g_ascii_strcasecmp (rel_type_map[i].types[1], cur->data))
				second_matched = TRUE;

			/* If they do, return the rel value from that entry... */
			if (first_matched && second_matched)
				return g_strdup_printf (format, rel_type_map[i].rel);
		}
	}

	/* ...otherwise return an "other" result. */
	return g_strdup_printf (format, "other");
}

static gchar *
google_rel_from_types (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others));
}

static gchar *
google_rel_from_types_phone (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone));
}

static gboolean
is_known_google_im_protocol (const gchar *protocol)
{
	const gchar *known_protocols[] = {
		"AIM", "MSN", "YAHOO", "SKYPE", "QQ",
		"GOOGLE_TALK", "ICQ", "JABBER"
	};
	guint i;

	if (NULL == protocol)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS (known_protocols); i++) {
		if (0 == g_ascii_strcasecmp (known_protocols[i], protocol))
			return TRUE;
	}

	return FALSE;
}

static gchar *
field_name_from_google_im_protocol (const gchar *google_protocol)
{
	gchar *protocol;
	if (!google_protocol)
		return NULL;

	protocol = g_strrstr (google_protocol, "#");
	if (!protocol)
		return NULL;

	return g_strdup_printf ("X-%s", protocol + 1);
}

static gchar *
google_im_protocol_from_field_name (const gchar *field_name)
{
	const gchar format[] = "http://schemas.google.com/g/2005#%s";

	if (!field_name || strlen (field_name) < 3)
		return NULL;

	return g_strdup_printf (format, field_name + 2);
}

static void
add_primary_param (EVCardAttribute *attr, gboolean has_type)
{
	EVCardAttributeParam *param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
	e_vcard_attribute_add_param_with_value (attr, param, "1");

	if (!has_type) {
		param = e_vcard_attribute_param_new ("TYPE");
		e_vcard_attribute_add_param_with_value (attr, param, "PREF");
	}
}

static GList *
get_google_primary_type_label (EVCardAttribute *attr, gboolean *primary, const gchar **label)
{
	GList *params;
	GList *types = NULL;

	*primary = FALSE;
	*label = NULL;
	params = e_vcard_attribute_get_params (attr);

	while (params) {
		const gchar *name;

		name = e_vcard_attribute_param_get_name (params->data);
		if (g_ascii_strcasecmp (name, GOOGLE_PRIMARY_PARAM) == 0) {
			GList *values;

			values = e_vcard_attribute_param_get_values (params->data);
			if (values && values->data &&
				(((const gchar *) values->data)[0] == '1' ||
				 0 == g_ascii_strcasecmp (values->data, "yes"))) {
				*primary = TRUE;
			}
		}

		if (g_ascii_strcasecmp (name, GOOGLE_LABEL_PARAM) == 0) {
			GList *values;

			values = e_vcard_attribute_param_get_values (params->data);
			*label = values ? values->data : NULL;
		}

		if (g_ascii_strcasecmp (name, "TYPE") == 0)
			types = e_vcard_attribute_param_get_values (params->data);
		params = params->next;
	}

	return types;
}

static void
add_attribute_from_gdata_gd_email_address (EVCard *vcard, GDataGDEmailAddress *email)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!email || !gdata_gd_email_address_get_address (email))
		return;

	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_email_address_get_relation_type (email));
	if (gdata_gd_email_address_is_primary (email))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_email_address_get_label (email));

	e_vcard_attribute_add_value (attr, gdata_gd_email_address_get_address (email));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_im_address (EVCard *vcard, GDataGDIMAddress *im)
{
	EVCardAttribute *attr;
	gboolean has_type;
	gchar *field_name;

	if (!im || !gdata_gd_im_address_get_address (im))
		return;

	field_name = field_name_from_google_im_protocol (gdata_gd_im_address_get_protocol (im));
	if (!field_name)
		return;

	attr = e_vcard_attribute_new (NULL, field_name);
	has_type = add_type_param_from_google_rel_im (attr, gdata_gd_im_address_get_relation_type (im));
	if (gdata_gd_im_address_is_primary (im))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_im_address_get_label (im));

	e_vcard_attribute_add_value (attr, gdata_gd_im_address_get_address (im));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_phone_number (EVCard *vcard, GDataGDPhoneNumber *number)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!number || !gdata_gd_phone_number_get_number (number))
		return;

	attr = e_vcard_attribute_new (NULL, EVC_TEL);
	has_type = add_type_param_from_google_rel_phone (attr, gdata_gd_phone_number_get_relation_type (number));
	if (gdata_gd_phone_number_is_primary (number))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_phone_number_get_label (number));

	e_vcard_attribute_add_value (attr, gdata_gd_phone_number_get_number (number));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_postal_address (EVCard *vcard, GDataGDPostalAddress *address)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!address || !gdata_gd_postal_address_get_address (address))
		return;

	/* Add the LABEL */
	attr = e_vcard_attribute_new (NULL, EVC_LABEL);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_postal_address_get_relation_type (address));
	if (gdata_gd_postal_address_is_primary (address))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_postal_address_get_label (address));

	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_address (address));

	if (attr)
		e_vcard_add_attribute (vcard, attr);

	/* Add the ADR */
	attr = e_vcard_attribute_new (NULL, EVC_ADR);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_postal_address_get_relation_type (address));
	if (gdata_gd_postal_address_is_primary (address))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_postal_address_get_label (address));

	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_po_box (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_house_name (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_street (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_city (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_region (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_postcode (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_country (address));

	/* The following bits of data provided by the Google Contacts API can't be fitted into the vCard format:
	 *   gdata_gd_postal_address_get_mail_class
	 *   gdata_gd_postal_address_get_usage
	 *   gdata_gd_postal_address_get_agent
	 *   gdata_gd_postal_address_get_neighborhood
	 *   gdata_gd_postal_address_get_subregion
	 *   gdata_gd_postal_address_get_country_code */

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_organization (EVCard *vcard, GDataGDOrganization *org)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!org || !gdata_gd_organization_get_name (org))
		return;

	/* Add the LABEL */
	attr = e_vcard_attribute_new (NULL, EVC_ORG);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_organization_get_relation_type (org));
	if (gdata_gd_organization_is_primary (org))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_organization_get_label (org));

	e_vcard_attribute_add_value (attr, gdata_gd_organization_get_name (org));
	e_vcard_attribute_add_value (attr, gdata_gd_organization_get_department (org));

	/* The following bits of data provided by the Google Contacts API can't be fitted into the vCard format:
	 *   gdata_gd_organization_get_title
	 *   gdata_gd_organization_get_job_description
	 *   gdata_gd_organization_get_symbol
	 *   gdata_gd_organization_get_location */

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static GDataGDEmailAddress *
gdata_gd_email_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDEmailAddress *email = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gchar *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		email = gdata_gd_email_address_new (values->data, rel, label, primary);
		g_free (rel);

		__debug__ ("New %semail entry %s (%s/%s)",
			   gdata_gd_email_address_is_primary (email) ? "primary " : "",
			   gdata_gd_email_address_get_address (email),
			   gdata_gd_email_address_get_relation_type (email),
			   gdata_gd_email_address_get_label (email));
	}

	return email;
}

static GDataGDIMAddress *
gdata_gd_im_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDIMAddress *im = NULL;
	GList *values;
	const gchar *name;

	name = e_vcard_attribute_get_name (attr);

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gchar *protocol, *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		protocol = google_im_protocol_from_field_name (name);
		im = gdata_gd_im_address_new (values->data, protocol, rel, label, primary);
		g_free (rel);
		g_free (protocol);

		__debug__ ("New %s%s entry %s (%s/%s)",
			   gdata_gd_im_address_is_primary (im) ? "primary " : "",
			   gdata_gd_im_address_get_protocol (im),
			   gdata_gd_im_address_get_address (im),
			   gdata_gd_im_address_get_relation_type (im),
			   gdata_gd_im_address_get_label (im));
	}

	return im;
}

static GDataGDPhoneNumber *
gdata_gd_phone_number_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDPhoneNumber *number = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types_phone (types);
		number = gdata_gd_phone_number_new (values->data, rel, label, NULL, primary);
		g_free (rel);

		__debug__ ("New %sphone-number entry %s (%s/%s)",
			   gdata_gd_phone_number_is_primary (number) ? "primary " : "",
			   gdata_gd_phone_number_get_number (number),
			   gdata_gd_phone_number_get_relation_type (number),
			   gdata_gd_phone_number_get_label (number));
	}

	return number;
}

static GDataGDPostalAddress *
gdata_gd_postal_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDPostalAddress *address = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values && values->data && *((gchar *) values->data) != '\0') {
		GList *types, *value;
		gchar *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		address = gdata_gd_postal_address_new (rel, label, primary);
		g_free (rel);

		__debug__ ("New %spostal address entry %s (%s/%s)",
			   gdata_gd_postal_address_is_primary (address) ? "primary " : "",
			   gdata_gd_postal_address_get_address (address),
			   gdata_gd_postal_address_get_relation_type (address),
			   gdata_gd_postal_address_get_label (address));

		/* Set the components of the address from the vCard's attribute values */
		value = values;
		gdata_gd_postal_address_set_po_box (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_house_name (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_street (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_city (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_region (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_postcode (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_country (address, (*((gchar *) value->data) != '\0') ? value->data : NULL, NULL);
	}

	return address;
}

static GDataGDOrganization *
gdata_gd_organization_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDOrganization *org = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		org = gdata_gd_organization_new (values->data, NULL, rel, label, primary);
		if (values->next)
			gdata_gd_organization_set_department (org, values->next->data);
		g_free (rel);

		/* TITLE and ROLE are dealt with separately in _gdata_entry_update_from_e_contact() */

		__debug__ ("New %sorganization entry %s (%s/%s)",
			   gdata_gd_organization_is_primary (org) ? "primary " : "",
			   gdata_gd_organization_get_name (org),
			   gdata_gd_organization_get_relation_type (org),
			   gdata_gd_organization_get_label (org));
	}

	return org;
}

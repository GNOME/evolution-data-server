/* e-book-backend-google.c - Google contact backendy.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010, 2011 Philip Withnall
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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

#include "evolution-data-server-config.h"

#include <string.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <gdata/gdata.h>

#include "libedataserver/libedataserver.h"

#include "e-book-backend-google.h"
#include "e-book-google-utils.h"

#define URI_GET_CONTACTS "https://www.google.com/m8/feeds/contacts/default/full"

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_META_BACKEND)

struct _EBookBackendGooglePrivate {
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
	/* Did the server-side groups change? If so, re-download the book */
	gboolean groups_changed;

	GDataAuthorizer *authorizer;
	GDataService *service;
	GHashTable *preloaded; /* gchar *uid ~> EContact * */
};

static void
ebb_google_data_book_error_from_gdata_error (GError **error,
					     const GError *gdata_error)
{
	gboolean use_fallback = FALSE;

	g_return_if_fail (gdata_error != NULL);

	if (!error)
		return;

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

static gboolean
ebb_google_is_authorized (EBookBackendGoogle *bbgoogle)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (bbgoogle), FALSE);

	if (!bbgoogle->priv->service)
		return FALSE;

	return gdata_service_is_authorized (GDATA_SERVICE (bbgoogle->priv->service));
}

static gboolean
ebb_google_request_authorization (EBookBackendGoogle *bbgoogle,
				  const ENamedParameters *credentials,
				  GCancellable *cancellable,
				  GError **error)
{
	/* Make sure we have the GDataService configured
	 * before requesting authorization. */

	if (!bbgoogle->priv->authorizer) {
		ESource *source;
		EGDataOAuth2Authorizer *authorizer;

		source = e_backend_get_source (E_BACKEND (bbgoogle));

		authorizer = e_gdata_oauth2_authorizer_new (source, GDATA_TYPE_CONTACTS_SERVICE);
		bbgoogle->priv->authorizer = GDATA_AUTHORIZER (authorizer);
	}

	if (E_IS_GDATA_OAUTH2_AUTHORIZER (bbgoogle->priv->authorizer)) {
		e_gdata_oauth2_authorizer_set_credentials (E_GDATA_OAUTH2_AUTHORIZER (bbgoogle->priv->authorizer), credentials);
	}

	if (!bbgoogle->priv->service) {
		GDataContactsService *contacts_service;

		contacts_service = gdata_contacts_service_new (bbgoogle->priv->authorizer);
		bbgoogle->priv->service = GDATA_SERVICE (contacts_service);

		e_binding_bind_property (
			bbgoogle, "proxy-resolver",
			bbgoogle->priv->service, "proxy-resolver",
			G_BINDING_SYNC_CREATE);
	}

	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background. */
	if (!GDATA_IS_CLIENT_LOGIN_AUTHORIZER (bbgoogle->priv->authorizer))
		return TRUE;

	/* Otherwise it's up to us to obtain a login secret, but
	   there is currently no way to do it, thus simply fail. */
	return FALSE;
}

/* returns whether group changed from the one stored in the cache;
 * returns FALSE, if the group was not in the cache yet;
 * also adds the group into the cache;
 * use group_name = NULL to remove it from the cache.
 */
static gboolean
ebb_google_cache_update_group (EBookBackendGoogle *bbgoogle,
			       const gchar *group_id,
			       const gchar *group_name)
{
	EBookCache *book_cache;
	gboolean changed;
	gchar *key, *old_value;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (bbgoogle), FALSE);
	g_return_val_if_fail (group_id != NULL, FALSE);

	book_cache = e_book_meta_backend_ref_cache (E_BOOK_META_BACKEND (bbgoogle));
	g_return_val_if_fail (book_cache != NULL, FALSE);

	key = g_strconcat ("google-group", ":", group_id, NULL);
	old_value = e_cache_dup_key (E_CACHE (book_cache), key, NULL);

	if (group_name) {
		changed = old_value && g_strcmp0 (old_value, group_name) != 0;

		e_cache_set_key (E_CACHE (book_cache), key, group_name, NULL);

		/* Add the category to Evolution’s category list. */
		e_categories_add (group_name, NULL, NULL, TRUE);
	} else {
		changed = old_value != NULL;

		e_cache_set_key (E_CACHE (book_cache), key, NULL, NULL);

		/* Remove the category from Evolution’s category list. */
		if (changed)
			e_categories_remove (old_value);
	}

	g_object_unref (book_cache);
	g_free (old_value);
	g_free (key);

	return changed;
}

static void
ebb_google_process_group (GDataEntry *entry,
			  guint entry_key,
			  guint entry_count,
			  gpointer user_data)
{
	EBookBackendGoogle *bbgoogle = user_data;
	const gchar *uid, *system_group_id;
	gchar *name;
	gboolean is_deleted;

	g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (bbgoogle));

	uid = gdata_entry_get_id (entry);
	name = e_contact_sanitise_google_group_name (entry);

	system_group_id = gdata_contacts_group_get_system_group_id (GDATA_CONTACTS_GROUP (entry));
	is_deleted = gdata_contacts_group_is_deleted (GDATA_CONTACTS_GROUP (entry));

	g_rec_mutex_lock (&bbgoogle->priv->groups_lock);

	if (system_group_id) {
		if (is_deleted) {
			gchar *entry_id = g_hash_table_lookup (bbgoogle->priv->system_groups_by_id, system_group_id);
			g_hash_table_remove (bbgoogle->priv->system_groups_by_entry_id, entry_id);
			g_hash_table_remove (bbgoogle->priv->system_groups_by_id, system_group_id);
		} else {
			gchar *entry_id, *system_group_id_dup;

			entry_id = e_contact_sanitise_google_group_id (uid);
			system_group_id_dup = g_strdup (system_group_id);

			g_hash_table_replace (bbgoogle->priv->system_groups_by_entry_id, entry_id, system_group_id_dup);
			g_hash_table_replace (bbgoogle->priv->system_groups_by_id, system_group_id_dup, entry_id);
		}

		g_free (name);

		/* use evolution's names for google's system groups */
		name = g_strdup (e_contact_map_google_with_evo_group (system_group_id, TRUE));

		g_warn_if_fail (name != NULL);
		if (!name)
			name = g_strdup (system_group_id);
	}

	if (is_deleted) {
		g_hash_table_remove (bbgoogle->priv->groups_by_id, uid);
		g_hash_table_remove (bbgoogle->priv->groups_by_name, name);

		bbgoogle->priv->groups_changed = ebb_google_cache_update_group (bbgoogle, uid, NULL) || bbgoogle->priv->groups_changed;
	} else {
		g_hash_table_replace (bbgoogle->priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (name));
		g_hash_table_replace (bbgoogle->priv->groups_by_name, g_strdup (name), e_contact_sanitise_google_group_id (uid));

		bbgoogle->priv->groups_changed = ebb_google_cache_update_group (bbgoogle, uid, name) || bbgoogle->priv->groups_changed;
	}

	g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

	g_free (name);
}

static gboolean
ebb_google_get_groups_sync (EBookBackendGoogle *bbgoogle,
			    gboolean with_time_constraint,
			    GCancellable *cancellable,
			    GError **error)
{
	GDataQuery *query;
	GDataFeed *feed;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (bbgoogle), FALSE);
	g_return_val_if_fail (ebb_google_is_authorized (bbgoogle), FALSE);

	g_rec_mutex_lock (&bbgoogle->priv->groups_lock);

	/* Build our query, always fetch all of them */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT));
	if (with_time_constraint && bbgoogle->priv->groups_last_update.tv_sec != 0) {
		gdata_query_set_updated_min (query, bbgoogle->priv->groups_last_update.tv_sec);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	bbgoogle->priv->groups_changed = FALSE;

	/* Run the query synchronously */
	feed = gdata_contacts_service_query_groups (
		GDATA_CONTACTS_SERVICE (bbgoogle->priv->service),
		query, cancellable, ebb_google_process_group, bbgoogle, &local_error);

	if (with_time_constraint && bbgoogle->priv->groups_last_update.tv_sec != 0 && (
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER) ||
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR))) {
		g_clear_error (&local_error);

		gdata_query_set_updated_min (query, -1);

		feed = gdata_contacts_service_query_groups (
			GDATA_CONTACTS_SERVICE (bbgoogle->priv->service),
			query, cancellable, ebb_google_process_group, bbgoogle, error);
	} else if (local_error) {
		g_propagate_error (error, local_error);
	}

	success = feed != NULL;

	if (success)
		g_get_current_time (&bbgoogle->priv->groups_last_update);

	g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

	g_clear_object (&feed);
	g_object_unref (query);

	return success;
}

static gboolean
ebb_google_connect_sync (EBookMetaBackend *meta_backend,
			 const ENamedParameters *credentials,
			 ESourceAuthenticationResult *out_auth_result,
			 gchar **out_certificate_pem,
			 GTlsCertificateFlags *out_certificate_errors,
			 GCancellable *cancellable,
			 GError **error)
{
	EBookBackendGoogle *bbgoogle;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	if (ebb_google_is_authorized (bbgoogle))
		return TRUE;

	success = ebb_google_request_authorization (bbgoogle, credentials, cancellable, &local_error);
	if (success)
		success = gdata_authorizer_refresh_authorization (bbgoogle->priv->authorizer, cancellable, &local_error);

	if (success)
		success = ebb_google_get_groups_sync (bbgoogle, FALSE, cancellable, &local_error);

	if (!success) {
		if (g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED)) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
		} else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
			   g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
			g_propagate_error (error, local_error);
			local_error = NULL;
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			ebb_google_data_book_error_from_gdata_error (error, local_error);
		}

		g_clear_error (&local_error);
	}

	return success;
}

static gboolean
ebb_google_disconnect_sync (EBookMetaBackend *meta_backend,
			    GCancellable *cancellable,
			    GError **error)
{
	EBookBackendGoogle *bbgoogle;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	g_clear_object (&bbgoogle->priv->service);
	g_clear_object (&bbgoogle->priv->authorizer);

	return TRUE;
}

static gboolean
ebb_google_get_changes_sync (EBookMetaBackend *meta_backend,
			     const gchar *last_sync_tag,
			     gboolean is_repeat,
			     gchar **out_new_sync_tag,
			     gboolean *out_repeat,
			     GSList **out_created_objects, /* EBookMetaBackendInfo * */
			     GSList **out_modified_objects, /* EBookMetaBackendInfo * */
			     GSList **out_removed_objects, /* EBookMetaBackendInfo * */
			     GCancellable *cancellable,
			     GError **error)
{
	EBookBackendGoogle *bbgoogle;
	EBookCache *book_cache;
	gint64 updated_time = 0;
	GTimeVal last_updated;
	GDataFeed *feed;
	GDataContactsQuery *contacts_query;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	if (!ebb_google_get_groups_sync (bbgoogle, TRUE, cancellable, error))
		return FALSE;

	book_cache = e_book_meta_backend_ref_cache (meta_backend);

	if (!last_sync_tag ||
	    !g_time_val_from_iso8601 (last_sync_tag, &last_updated)) {
		last_updated.tv_sec = 0;
	}

	contacts_query = gdata_contacts_query_new_with_limits (NULL, 0, G_MAXINT);
	if (last_updated.tv_sec > 0 && !bbgoogle->priv->groups_changed) {
		gdata_query_set_updated_min (GDATA_QUERY (contacts_query), last_updated.tv_sec);
		gdata_contacts_query_set_show_deleted (contacts_query, TRUE);
	}

	feed = gdata_contacts_service_query_contacts (GDATA_CONTACTS_SERVICE (bbgoogle->priv->service), GDATA_QUERY (contacts_query), cancellable, NULL, NULL, &local_error);

	if (last_updated.tv_sec > 0 && !bbgoogle->priv->groups_changed && (
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER) ||
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR))) {
		g_clear_error (&local_error);

		gdata_query_set_updated_min (GDATA_QUERY (contacts_query), -1);

		feed = gdata_contacts_service_query_contacts (GDATA_CONTACTS_SERVICE (bbgoogle->priv->service), GDATA_QUERY (contacts_query), cancellable, NULL, NULL, &local_error);
	}

	if (feed && !g_cancellable_is_cancelled (cancellable) && !local_error) {
		GList *link;

		if (gdata_feed_get_updated (feed) > updated_time)
			updated_time = gdata_feed_get_updated (feed);

		for (link = gdata_feed_get_entries (feed); link && !g_cancellable_is_cancelled (cancellable); link = g_list_next (link)) {
			GDataContactsContact *gdata_contact = link->data;
			EContact *cached_contact = NULL;
			gchar *uid;

			if (!GDATA_IS_CONTACTS_CONTACT (gdata_contact))
				continue;

			uid = g_strdup (gdata_entry_get_id (GDATA_ENTRY (gdata_contact)));
			if (!uid || !*uid) {
				g_free (uid);
				continue;
			}

			if (!e_book_cache_get_contact (book_cache, uid, FALSE, &cached_contact, cancellable, NULL))
				cached_contact = NULL;

			if (gdata_contacts_contact_is_deleted (gdata_contact)) {
				*out_removed_objects = g_slist_prepend (*out_removed_objects,
					e_book_meta_backend_info_new (uid, NULL, NULL, NULL));
			} else {
				EContact *new_contact;

				if (cached_contact) {
					gchar *old_etag;

					old_etag = e_contact_get (cached_contact, E_CONTACT_REV);

					if (g_strcmp0 (gdata_entry_get_etag (GDATA_ENTRY (gdata_contact)), old_etag) == 0) {
						g_object_unref (cached_contact);
						g_free (old_etag);
						g_free (uid);
						continue;
					}

					g_free (old_etag);
				}

				g_rec_mutex_lock (&bbgoogle->priv->groups_lock);
				new_contact = e_contact_new_from_gdata_entry (GDATA_ENTRY (gdata_contact),
					bbgoogle->priv->groups_by_id, bbgoogle->priv->system_groups_by_entry_id);
				g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

				if (new_contact) {
					const gchar *revision, *photo_etag;
					gchar *object, *extra;

					photo_etag = gdata_contacts_contact_get_photo_etag (gdata_contact);
					if (photo_etag && cached_contact) {
						gchar *old_photo_etag;

						old_photo_etag = e_vcard_util_dup_x_attribute (E_VCARD (cached_contact), E_GOOGLE_X_PHOTO_ETAG);
						if (g_strcmp0 (photo_etag, old_photo_etag) == 0) {
							EContactPhoto *photo;

							/* To not download it again, when it's already available locally */
							photo_etag = NULL;

							/* Copy the photo attribute to the changed contact */
							photo = e_contact_get (cached_contact, E_CONTACT_PHOTO);
							e_contact_set (new_contact, E_CONTACT_PHOTO, photo);

							e_contact_photo_free (photo);
						}

						g_free (old_photo_etag);
					}

					if (photo_etag) {
						guint8 *photo_data;
						gsize photo_length = 0;
						gchar *photo_content_type = NULL;
						GError *local_error2 = NULL;

						photo_data = gdata_contacts_contact_get_photo (gdata_contact, GDATA_CONTACTS_SERVICE (bbgoogle->priv->service),
							&photo_length, &photo_content_type, cancellable, &local_error2);

						if (!local_error2) {
							EContactPhoto *photo;

							photo = e_contact_photo_new ();
							photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
							photo->data.inlined.data = (guchar *) photo_data;
							photo->data.inlined.length = photo_length;
							photo->data.inlined.mime_type = photo_content_type;

							e_contact_set (E_CONTACT (new_contact), E_CONTACT_PHOTO, photo);

							e_contact_photo_free (photo);

							/* Read of the photo frees previously obtained photo_etag */
							photo_etag = gdata_contacts_contact_get_photo_etag (gdata_contact);

							e_vcard_util_set_x_attribute (E_VCARD (new_contact), E_GOOGLE_X_PHOTO_ETAG, photo_etag);
						} else {
							g_debug ("%s: Downloading contact photo for '%s' failed: %s", G_STRFUNC,
								gdata_entry_get_id (GDATA_ENTRY (gdata_contact)), local_error2->message);

							g_clear_error (&local_error2);
						}
					}

					revision = gdata_entry_get_etag (GDATA_ENTRY (gdata_contact));
					e_contact_set (new_contact, E_CONTACT_REV, revision);
					object = e_vcard_to_string (E_VCARD (new_contact), EVC_FORMAT_VCARD_30);
					extra = gdata_parsable_get_xml (GDATA_PARSABLE (gdata_contact));

					if (cached_contact) {
						*out_modified_objects = g_slist_prepend (*out_modified_objects,
							e_book_meta_backend_info_new (uid, revision, object, extra));
					} else {
						*out_created_objects = g_slist_prepend (*out_created_objects,
							e_book_meta_backend_info_new (uid, revision, object, extra));
					}

					g_free (object);
					g_free (extra);
				}

				g_clear_object (&new_contact);
			}

			g_clear_object (&cached_contact);
			g_free (uid);
		}
	}

	g_clear_object (&contacts_query);
	g_clear_object (&feed);

	if (!g_cancellable_is_cancelled (cancellable) && !local_error) {
		last_updated.tv_sec = updated_time;
		last_updated.tv_usec = 0;

		*out_new_sync_tag = g_time_val_to_iso8601 (&last_updated);
	}

	g_clear_object (&book_cache);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
ebb_google_load_contact_sync (EBookMetaBackend *meta_backend,
			      const gchar *uid,
			      const gchar *extra,
			      EContact **out_contact,
			      gchar **out_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendGoogle *bbgoogle;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	/* Only "load" preloaded during save, otherwise fail with an error,
	   because the backend provides objects within get_changes_sync() */

	if (bbgoogle->priv->preloaded) {
		EContact *contact;

		contact = g_hash_table_lookup (bbgoogle->priv->preloaded, uid);
		if (contact) {
			*out_contact = e_contact_duplicate (contact);

			g_hash_table_remove (bbgoogle->priv->preloaded, uid);

			return TRUE;
		}
	}

	g_set_error_literal (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
		e_book_client_error_to_string (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));

	return FALSE;
}

static gchar *
ebb_google_create_group_sync (EBookBackendGoogle *bbgoogle,
			      const gchar *category_name,
			      GCancellable *cancellable,
			      GError **error)
{
	GDataEntry *group, *new_group;
	const gchar *system_group_id;
	gchar *uid;

	system_group_id = e_contact_map_google_with_evo_group (category_name, FALSE);
	if (system_group_id) {
		gchar *group_entry_id;

		g_rec_mutex_lock (&bbgoogle->priv->groups_lock);
		group_entry_id = g_strdup (g_hash_table_lookup (bbgoogle->priv->system_groups_by_id, system_group_id));
		g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

		g_return_val_if_fail (group_entry_id != NULL, NULL);

		return group_entry_id;
	}

	group = GDATA_ENTRY (gdata_contacts_group_new (NULL));

	gdata_entry_set_title (group, category_name);

	/* Insert the new group */
	new_group = GDATA_ENTRY (gdata_contacts_service_insert_group (
			GDATA_CONTACTS_SERVICE (bbgoogle->priv->service),
			GDATA_CONTACTS_GROUP (group),
			cancellable, error));
	g_object_unref (group);

	if (new_group == NULL)
		return NULL;

	/* Add the new group to the group mappings */
	uid = g_strdup (gdata_entry_get_id (new_group));

	g_rec_mutex_lock (&bbgoogle->priv->groups_lock);
	g_hash_table_replace (bbgoogle->priv->groups_by_id, e_contact_sanitise_google_group_id (uid), g_strdup (category_name));
	g_hash_table_replace (bbgoogle->priv->groups_by_name, g_strdup (category_name), e_contact_sanitise_google_group_id (uid));
	g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

	g_object_unref (new_group);

	/* Update the cache. */
	ebb_google_cache_update_group (bbgoogle, uid, category_name);

	return uid;
}

static gboolean
ebb_google_photo_changed (EBookMetaBackend *meta_backend,
			  EContact *old_contact,
			  EContact *new_contact,
			  GCancellable *cancellable)
{
	EContact *old_contact_copy = NULL;
	EContactPhoto *old_photo;
	EContactPhoto *new_photo;
	gboolean changed = FALSE;

	old_photo = e_contact_get (old_contact, E_CONTACT_PHOTO);
	new_photo = e_contact_get (new_contact, E_CONTACT_PHOTO);

	if (!old_photo && new_photo)
		changed = TRUE;

	if (old_photo && !new_photo)
		changed = TRUE;

	/* old_photo comes from cache, thus it's always URI (to local file or elsewhere),
	   while the new_photo is to be saved, which is always inlined. */
	if (!changed && old_photo && new_photo &&
	    old_photo->type == E_CONTACT_PHOTO_TYPE_URI &&
	    new_photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		e_contact_photo_free (old_photo);
		old_photo = NULL;

		old_contact_copy = e_contact_duplicate (old_contact);

		if (e_book_meta_backend_inline_local_photos_sync (meta_backend, old_contact_copy, cancellable, NULL))
			old_photo = e_contact_get (old_contact_copy, E_CONTACT_PHOTO);
	}

	if (old_photo && new_photo &&
	    old_photo->type == E_CONTACT_PHOTO_TYPE_INLINED &&
	    new_photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		guchar *old_data;
		guchar *new_data;
		gsize old_length;
		gsize new_length;

		old_data = old_photo->data.inlined.data;
		new_data = new_photo->data.inlined.data;

		old_length = old_photo->data.inlined.length;
		new_length = new_photo->data.inlined.length;

		changed =
			(old_length != new_length) ||
			(memcmp (old_data, new_data, old_length) != 0);
	}

	e_contact_photo_free (old_photo);
	e_contact_photo_free (new_photo);
	g_clear_object (&old_contact_copy);

	return changed;
}

static GDataEntry *
ebb_google_update_contact_photo_sync (GDataContactsContact *contact,
				      GDataContactsService *service,
				      EContactPhoto *photo,
				      GCancellable *cancellable,
				      GError **error)
{
	GDataAuthorizationDomain *authorization_domain;
	GDataEntry *gdata_contact = NULL;
	const gchar *content_type;
	const guint8 *photo_data;
	gsize photo_length;
	gboolean success;

	authorization_domain = gdata_contacts_service_get_primary_authorization_domain ();

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
		gdata_contact = gdata_service_query_single_entry (
			GDATA_SERVICE (service),
			authorization_domain,
			gdata_entry_get_id (GDATA_ENTRY (contact)),
			NULL, GDATA_TYPE_CONTACTS_CONTACT,
			cancellable, error);
	}

	return gdata_contact;
}

static gboolean
ebb_google_save_contact_sync (EBookMetaBackend *meta_backend,
			      gboolean overwrite_existing,
			      EConflictResolution conflict_resolution,
			      /* const */ EContact *contact,
			      const gchar *extra,
			      gchar **out_new_uid,
			      gchar **out_new_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	EBookBackendGoogle *bbgoogle;
	EBookCache *book_cache;
	GDataEntry *entry = NULL;
	GDataContactsContact *gdata_contact;
	EContact *cached_contact = NULL;
	EContact *new_contact;
	const gchar *uid;
	EContactPhoto *photo;
	gboolean photo_changed;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);
	g_return_val_if_fail (out_new_extra != NULL, FALSE);

	book_cache = e_book_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (book_cache != NULL, FALSE);

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	if (!overwrite_existing || !e_book_cache_get_contact (book_cache, e_contact_get_const (contact, E_CONTACT_UID),
		FALSE, &cached_contact, cancellable, NULL)) {
		cached_contact = NULL;
	}

	if (extra && *extra)
		entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, extra, -1, NULL));

	g_rec_mutex_lock (&bbgoogle->priv->groups_lock);

	/* Ensure the system groups have been fetched. */
	if (g_hash_table_size (bbgoogle->priv->system_groups_by_id) == 0)
		ebb_google_get_groups_sync (bbgoogle, FALSE, cancellable, NULL);

	if (overwrite_existing || entry) {
		if (gdata_entry_update_from_e_contact (entry, contact, FALSE,
			bbgoogle->priv->groups_by_name,
			bbgoogle->priv->system_groups_by_id,
			ebb_google_create_group_sync,
			bbgoogle,
			cancellable)) {
			overwrite_existing = TRUE;
		} else {
			g_clear_object (&entry);
		}
	} else {
		/* Build the GDataEntry from the vCard */
		entry = gdata_entry_new_from_e_contact (
			contact,
			bbgoogle->priv->groups_by_name,
			bbgoogle->priv->system_groups_by_id,
			ebb_google_create_group_sync,
			bbgoogle,
			cancellable);
	}

	g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

	photo_changed = cached_contact && ebb_google_photo_changed (meta_backend, cached_contact, contact, cancellable);

	g_clear_object (&cached_contact);
	g_clear_object (&book_cache);

	if (!entry) {
		g_propagate_error (error, e_data_book_create_error (E_DATA_BOOK_STATUS_OTHER_ERROR, _("Object to save is not a valid vCard")));
		return FALSE;
	}

	if (overwrite_existing) {
		gdata_contact = GDATA_CONTACTS_CONTACT (gdata_service_update_entry (
			bbgoogle->priv->service,
			gdata_contacts_service_get_primary_authorization_domain (),
			entry, cancellable, &local_error));
	} else {
		gdata_contact = gdata_contacts_service_insert_contact (
			GDATA_CONTACTS_SERVICE (bbgoogle->priv->service),
			GDATA_CONTACTS_CONTACT (entry),
			cancellable, &local_error);
	}

	photo = g_object_steal_data (G_OBJECT (entry), "photo");

	g_object_unref (entry);

	if (!gdata_contact) {
		ebb_google_data_book_error_from_gdata_error (error, local_error);
		g_clear_error (&local_error);
		e_contact_photo_free (photo);

		return FALSE;
	}

	if (photo_changed) {
		entry = ebb_google_update_contact_photo_sync (gdata_contact, GDATA_CONTACTS_SERVICE (bbgoogle->priv->service), photo, cancellable, &local_error);
		if (!entry) {
			ebb_google_data_book_error_from_gdata_error (error, local_error);
			g_clear_error (&local_error);
			e_contact_photo_free (photo);
			g_clear_object (&gdata_contact);

			return FALSE;
		}

		g_object_unref (gdata_contact);
		gdata_contact = GDATA_CONTACTS_CONTACT (entry);
	}

	g_rec_mutex_lock (&bbgoogle->priv->groups_lock);
	new_contact = e_contact_new_from_gdata_entry (GDATA_ENTRY (gdata_contact),
		bbgoogle->priv->groups_by_id,
		bbgoogle->priv->system_groups_by_entry_id);
	g_rec_mutex_unlock (&bbgoogle->priv->groups_lock);

	if (!new_contact) {
		g_object_unref (gdata_contact);
		e_contact_photo_free (photo);
		g_propagate_error (error, e_data_book_create_error (E_DATA_BOOK_STATUS_OTHER_ERROR, _("Failed to create contact from returned server data")));
		return FALSE;
	}

	e_contact_set (new_contact, E_CONTACT_PHOTO, photo);
	e_vcard_util_set_x_attribute (E_VCARD (new_contact), E_GOOGLE_X_PHOTO_ETAG, gdata_contacts_contact_get_photo_etag (gdata_contact));

	*out_new_extra = gdata_parsable_get_xml (GDATA_PARSABLE (gdata_contact));

	g_object_unref (gdata_contact);

	e_contact_photo_free (photo);

	uid = e_contact_get_const (new_contact, E_CONTACT_UID);

	if (!uid) {
		g_propagate_error (error, e_data_book_create_error (E_DATA_BOOK_STATUS_OTHER_ERROR, _("Server returned contact without UID")));

		g_object_unref (new_contact);
		g_free (*out_new_extra);
		*out_new_extra = NULL;

		return FALSE;
	}

	if (bbgoogle->priv->preloaded) {
		*out_new_uid = g_strdup (uid);
		g_hash_table_insert (bbgoogle->priv->preloaded, g_strdup (uid), new_contact);
	} else {
		g_object_unref (new_contact);
	}

	return TRUE;
}

static gboolean
ebb_google_remove_contact_sync (EBookMetaBackend *meta_backend,
				EConflictResolution conflict_resolution,
				const gchar *uid,
				const gchar *extra,
				const gchar *object,
				GCancellable *cancellable,
				GError **error)
{
	EBookBackendGoogle *bbgoogle;
	GDataEntry *entry;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (extra != NULL, FALSE);

	entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, extra, -1, NULL));
	if (!entry) {
		g_propagate_error (error, e_data_book_create_error (E_DATA_BOOK_STATUS_INVALID_ARG, NULL));
		return FALSE;
	}

	bbgoogle = E_BOOK_BACKEND_GOOGLE (meta_backend);

	if (!gdata_service_delete_entry (bbgoogle->priv->service,
		gdata_contacts_service_get_primary_authorization_domain (), entry,
		cancellable, &local_error)) {
		ebb_google_data_book_error_from_gdata_error (error, local_error);
		g_error_free (local_error);
		g_object_unref (entry);

		return FALSE;
	}

	g_object_unref (entry);

	return TRUE;
}

static gchar *
ebb_google_get_backend_property (EBookBackend *book_backend,
				 const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			"net",
			"do-initial-query",
			"contact-lists",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			NULL);

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup ("");

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		return g_strjoin (",",
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
			e_contact_field_name (E_CONTACT_CATEGORY_LIST),
			e_contact_field_name (E_CONTACT_FILE_AS),
			e_contact_field_name (E_CONTACT_NICKNAME),
			NULL);
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_google_parent_class)->get_backend_property (book_backend, prop_name);
}

static void
ebb_google_constructed (GObject *object)
{
	EBookBackendGoogle *bbgoogle = E_BOOK_BACKEND_GOOGLE (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_google_parent_class)->constructed (object);

	/* Set it as always writable, regardless online/offline state */
	e_book_backend_set_writable (E_BOOK_BACKEND (bbgoogle), TRUE);
}

static void
ebb_google_dispose (GObject *object)
{
	EBookBackendGoogle *bbgoogle = E_BOOK_BACKEND_GOOGLE (object);

	g_clear_object (&bbgoogle->priv->service);
	g_clear_object (&bbgoogle->priv->authorizer);

	g_hash_table_destroy (bbgoogle->priv->preloaded);
	bbgoogle->priv->preloaded = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
ebb_google_finalize (GObject *object)
{
	EBookBackendGoogle *bbgoogle = E_BOOK_BACKEND_GOOGLE (object);

	g_clear_pointer (&bbgoogle->priv->groups_by_id, (GDestroyNotify) g_hash_table_destroy);
	g_clear_pointer (&bbgoogle->priv->groups_by_id, (GDestroyNotify) g_hash_table_destroy);
	g_clear_pointer (&bbgoogle->priv->groups_by_name, (GDestroyNotify) g_hash_table_destroy);
	g_clear_pointer (&bbgoogle->priv->system_groups_by_entry_id, (GDestroyNotify) g_hash_table_destroy);
	g_clear_pointer (&bbgoogle->priv->system_groups_by_id, (GDestroyNotify) g_hash_table_destroy);

	g_rec_mutex_clear (&bbgoogle->priv->groups_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static void
e_book_backend_google_init (EBookBackendGoogle *bbgoogle)
{
	bbgoogle->priv = G_TYPE_INSTANCE_GET_PRIVATE (bbgoogle, E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGooglePrivate);
	bbgoogle->priv->preloaded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	g_rec_mutex_init (&bbgoogle->priv->groups_lock);

	bbgoogle->priv->groups_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	bbgoogle->priv->groups_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	bbgoogle->priv->system_groups_by_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	/* shares keys and values with system_groups_by_id */
	bbgoogle->priv->system_groups_by_entry_id = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *klass)
{
	GObjectClass *object_class;
	EBookBackendClass *book_backend_class;
	EBookMetaBackendClass *book_meta_backend_class;

	g_type_class_add_private (klass, sizeof (EBookBackendGooglePrivate));

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendgoogle.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendGoogleFactory";
	book_meta_backend_class->connect_sync = ebb_google_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_google_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_google_get_changes_sync;
	book_meta_backend_class->load_contact_sync = ebb_google_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_google_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_google_remove_contact_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->get_backend_property = ebb_google_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ebb_google_constructed;
	object_class->dispose = ebb_google_dispose;
	object_class->finalize = ebb_google_finalize;
}

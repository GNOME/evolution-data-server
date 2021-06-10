/*
 * Copyright (C) 2020 Red Hat (www.redhat.com)
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
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-backend-webdav-notes.h"

#define E_WEBDAV_NOTES_X_ETAG "X-EVOLUTION-WEBDAV-NOTES-ETAG"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

struct _ECalBackendWebDAVNotesPrivate {
	/* The main WebDAV session  */
	EWebDAVSession *webdav;
	GMutex webdav_lock;

	/* If already been connected, then the connect_sync() will relax server checks,
	   to avoid unnecessary requests towards the server. */
	gboolean been_connected;

	gboolean etag_supported;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendWebDAVNotes, e_cal_backend_webdav_notes, E_TYPE_CAL_META_BACKEND)

static EWebDAVSession *
ecb_webdav_notes_ref_session (ECalBackendWebDAVNotes *cbnotes)
{
	EWebDAVSession *webdav;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (cbnotes), NULL);

	g_mutex_lock (&cbnotes->priv->webdav_lock);
	if (cbnotes->priv->webdav)
		webdav = g_object_ref (cbnotes->priv->webdav);
	else
		webdav = NULL;
	g_mutex_unlock (&cbnotes->priv->webdav_lock);

	return webdav;
}

static gboolean
ecb_webdav_notes_connect_sync (ECalMetaBackend *meta_backend,
			       const ENamedParameters *credentials,
			       ESourceAuthenticationResult *out_auth_result,
			       gchar **out_certificate_pem,
			       GTlsCertificateFlags *out_certificate_errors,
			       GCancellable *cancellable,
			       GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	GHashTable *capabilities = NULL, *allows = NULL;
	ESource *source;
	gboolean success, is_writable = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);

	g_mutex_lock (&cbnotes->priv->webdav_lock);
	if (cbnotes->priv->webdav) {
		g_mutex_unlock (&cbnotes->priv->webdav_lock);
		return TRUE;
	}
	g_mutex_unlock (&cbnotes->priv->webdav_lock);

	source = e_backend_get_source (E_BACKEND (meta_backend));

	webdav = e_webdav_session_new (source);

	e_soup_session_setup_logging (E_SOUP_SESSION (webdav), g_getenv ("WEBDAV_NOTES_DEBUG"));

	e_binding_bind_property (
		cbnotes, "proxy-resolver",
		webdav, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	e_soup_session_set_credentials (E_SOUP_SESSION (webdav), credentials);

	if (cbnotes->priv->been_connected) {
		g_mutex_lock (&cbnotes->priv->webdav_lock);
		cbnotes->priv->webdav = webdav;
		g_mutex_unlock (&cbnotes->priv->webdav_lock);

		return TRUE;
	}

	/* This is just to make it similar to the CalDAV backend. */
	cbnotes->priv->etag_supported = TRUE;

	success = e_webdav_session_options_sync (webdav, NULL,
		&capabilities, &allows, cancellable, &local_error);

	if (success && !g_cancellable_is_cancelled (cancellable)) {
		GSList *privileges = NULL, *link;

		/* Ignore any errors here */
		if (e_webdav_session_get_current_user_privilege_set_sync (webdav, NULL, &privileges, cancellable, NULL)) {
			for (link = privileges; link && !is_writable; link = g_slist_next (link)) {
				EWebDAVPrivilege *privilege = link->data;

				if (privilege) {
					is_writable =
						privilege->hint == E_WEBDAV_PRIVILEGE_HINT_WRITE ||
						privilege->hint == E_WEBDAV_PRIVILEGE_HINT_WRITE_CONTENT ||
						privilege->hint == E_WEBDAV_PRIVILEGE_HINT_ALL;
				}
			}

			g_slist_free_full (privileges, e_webdav_privilege_free);
		} else {
			is_writable = allows && (
				g_hash_table_contains (allows, SOUP_METHOD_PUT) ||
				g_hash_table_contains (allows, SOUP_METHOD_POST) ||
				g_hash_table_contains (allows, SOUP_METHOD_DELETE));
		}
	}

	if (success) {
		e_cal_backend_set_writable (E_CAL_BACKEND (cbnotes), is_writable);
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
	}

	if (success) {
		gchar *ctag = NULL;

		/* Some servers, notably Google, allow OPTIONS when not
		   authorized (aka without credentials), thus try something
		   more aggressive, just in case.

		   The 'getctag' extension is not required, thus check
		   for unauthorized error only. */
		if (!e_webdav_session_getctag_sync (webdav, NULL, &ctag, cancellable, &local_error) &&
		    g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			success = FALSE;
		} else {
			g_clear_error (&local_error);
		}

		g_free (ctag);
	}

	if (!success) {
		gboolean credentials_empty;
		gboolean is_ssl_error;

		credentials_empty = (!credentials || !e_named_parameters_count (credentials) ||
			(e_named_parameters_count (credentials) == 1 && e_named_parameters_exists (credentials, E_SOURCE_CREDENTIAL_SSL_TRUST))) &&
			e_soup_session_get_authentication_requires_credentials (E_SOUP_SESSION (webdav));
		is_ssl_error = g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;

		/* because evolution knows only G_IO_ERROR_CANCELLED */
		if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_CANCELLED)) {
			local_error->domain = G_IO_ERROR;
			local_error->code = G_IO_ERROR_CANCELLED;
		} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN) && credentials_empty) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
		} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			if (credentials_empty)
				*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
			else
				*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
		} else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
			   (!e_soup_session_get_authentication_requires_credentials (E_SOUP_SESSION (webdav)) &&
			   g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
		} else if (!local_error) {
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED,
				_("Unknown error"));
		}

		if (local_error) {
			g_propagate_error (error, local_error);
			local_error = NULL;
		}

		if (is_ssl_error) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_SSL_FAILED);
			e_soup_session_get_ssl_error_details (E_SOUP_SESSION (webdav), out_certificate_pem, out_certificate_errors);
		} else {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
		}
	}

	if (capabilities)
		g_hash_table_destroy (capabilities);
	if (allows)
		g_hash_table_destroy (allows);

	if (success && !g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_lock (&cbnotes->priv->webdav_lock);
		cbnotes->priv->webdav = webdav;
		g_mutex_unlock (&cbnotes->priv->webdav_lock);
		cbnotes->priv->been_connected = TRUE;
	} else {
		if (success) {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
			success = FALSE;
		}

		g_clear_object (&webdav);
	}

	return success;
}

static gboolean
ecb_webdav_notes_disconnect_sync (ECalMetaBackend *meta_backend,
				  GCancellable *cancellable,
				  GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	ESource *source;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);

	g_mutex_lock (&cbnotes->priv->webdav_lock);
	if (cbnotes->priv->webdav)
		soup_session_abort (SOUP_SESSION (cbnotes->priv->webdav));

	g_clear_object (&cbnotes->priv->webdav);
	g_mutex_unlock (&cbnotes->priv->webdav_lock);

	source = e_backend_get_source (E_BACKEND (meta_backend));
	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

	return TRUE;
}

typedef struct _WebDAVNotesChangesData {
	GSList **out_modified_objects;
	GSList **out_removed_objects;
	GHashTable *known_items; /* gchar *href ~> ECalMetaBackendInfo * */
} WebDAVNotesChangesData;

static gboolean
ecb_webdav_notes_search_changes_cb (ECalCache *cal_cache,
				    const gchar *uid,
				    const gchar *rid,
				    const gchar *revision,
				    const gchar *object,
				    const gchar *extra,
				    guint32 custom_flags,
				    EOfflineState offline_state,
				    gpointer user_data)
{
	WebDAVNotesChangesData *ccd = user_data;

	g_return_val_if_fail (ccd != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	/* The 'extra' can be NULL for added components in offline mode */
	if (((extra && *extra) || offline_state != E_OFFLINE_STATE_LOCALLY_CREATED) && (!rid || !*rid)) {
		ECalMetaBackendInfo *nfo;

		nfo = (extra && *extra) ? g_hash_table_lookup (ccd->known_items, extra) : NULL;
		if (nfo) {
			if (g_strcmp0 (revision, nfo->revision) == 0) {
				g_hash_table_remove (ccd->known_items, extra);
			} else {
				if (!nfo->uid || !*(nfo->uid)) {
					g_free (nfo->uid);
					nfo->uid = g_strdup (uid);
				}

				*(ccd->out_modified_objects) = g_slist_prepend (*(ccd->out_modified_objects),
					e_cal_meta_backend_info_copy (nfo));

				g_hash_table_remove (ccd->known_items, extra);
			}
		} else {
			*(ccd->out_removed_objects) = g_slist_prepend (*(ccd->out_removed_objects),
				e_cal_meta_backend_info_new (uid, revision, object, extra));
		}
	}

	return TRUE;
}

static void
ecb_webdav_notes_check_credentials_error (ECalBackendWebDAVNotes *cbnotes,
					  EWebDAVSession *webdav,
					  GError *op_error)
{
	g_return_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (cbnotes));

	if (g_error_matches (op_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED) && webdav) {
		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_TLS_NOT_AVAILABLE;
	} else if (g_error_matches (op_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		   g_error_matches (op_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN)) {
		gboolean was_forbidden = g_error_matches (op_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN);

		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_AUTHENTICATION_REQUIRED;

		cbnotes->priv->been_connected = FALSE;

		if (webdav) {
			ENamedParameters *credentials;
			gboolean empty_credentials;

			credentials = e_soup_session_dup_credentials (E_SOUP_SESSION (webdav));
			empty_credentials = !credentials || !e_named_parameters_count (credentials);
			e_named_parameters_free (credentials);

			if (!empty_credentials) {
				if (was_forbidden) {
					if (e_webdav_session_get_last_dav_error_is_permission (webdav)) {
						op_error->code = E_CLIENT_ERROR_PERMISSION_DENIED;
						g_free (op_error->message);
						op_error->message = g_strdup (e_client_error_to_string (op_error->code));
					} else {
						/* To avoid credentials prompt */
						op_error->code = E_CLIENT_ERROR_OTHER_ERROR;
					}
				} else {
					op_error->code = E_CLIENT_ERROR_AUTHENTICATION_FAILED;
				}
			}
		}
	}
}

static gboolean
ecb_webdav_notes_getetag_cb (EWebDAVSession *webdav,
			     xmlNodePtr prop_node,
			     const SoupURI *request_uri,
			     const gchar *href,
			     guint status_code,
			     gpointer user_data)
{
	if (status_code == SOUP_STATUS_OK) {
		gchar **out_etag = user_data;
		const xmlChar *etag;

		g_return_val_if_fail (out_etag != NULL, FALSE);

		etag = e_xml_find_child_and_get_text (prop_node, E_WEBDAV_NS_DAV, "getetag");

		if (etag && *etag)
			*out_etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) etag));
	}

	return FALSE;
}

static gboolean
ecb_webdav_notes_getetag_sync (EWebDAVSession *webdav,
			       const gchar *uri,
			       gchar **out_etag,
			       GCancellable *cancellable,
			       GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_etag != NULL, FALSE);

	*out_etag = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "getetag");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		ecb_webdav_notes_getetag_cb, out_etag, cancellable, error);

	g_object_unref (xml);

	return success && *out_etag != NULL;
}

static ICalComponent *
ecb_webdav_notes_new_icomp (glong creation_date,
			    glong last_modified,
			    const gchar *uid,
			    const gchar *revision,
			    const gchar *summary,
			    const gchar *description)
{
	ICalComponent *icomp;
	ICalTime *itt;
	glong tt;

	icomp = i_cal_component_new_vjournal ();

	if (creation_date > 0)
		tt = creation_date;
	else if (last_modified > 0)
		tt = last_modified;
	else
		tt = (time_t) time (NULL);

	itt = i_cal_time_new_from_timet_with_zone ((time_t) tt, FALSE, i_cal_timezone_get_utc_timezone ());
	i_cal_component_take_property (icomp, i_cal_property_new_created (itt));
	g_object_unref (itt);

	if (last_modified > 0)
		tt = last_modified;
	else
		tt = (time_t) time (NULL);

	itt = i_cal_time_new_from_timet_with_zone ((time_t) tt, FALSE, i_cal_timezone_get_utc_timezone ());
	i_cal_component_take_property (icomp, i_cal_property_new_lastmodified (itt));
	g_object_unref (itt);

	i_cal_component_set_uid (icomp, uid);

	if (summary && g_str_has_suffix (summary, ".txt")) {
		gchar *tmp;

		tmp = g_strndup (summary, strlen (summary) - 4);
		i_cal_component_set_summary (icomp, tmp);
		g_free (tmp);
	} else if (summary && *summary) {
		i_cal_component_set_summary (icomp, summary);
	}

	if (description)
		i_cal_component_set_description (icomp, description);

	e_cal_util_component_set_x_property (icomp, E_WEBDAV_NOTES_X_ETAG, revision);

	return icomp;
}

static gboolean
ecb_webdav_notes_get_objects_sync (EWebDAVSession *webdav,
				   GHashTable *resources_hash, /* gchar *href ~> EWebDAVResource * */
				   GSList *infos, /* ECalMetaBackendInfo * */
				   GCancellable *cancellable,
				   GError **error)
{
	GSList *link;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);

	for (link = infos; success && link; link = g_slist_next (link)) {
		ECalMetaBackendInfo *nfo = link->data;
		gchar *etag = NULL, *bytes = NULL;

		if (!nfo)
			continue;

		success = e_webdav_session_get_data_sync (webdav, nfo->extra, NULL, &etag, &bytes, NULL, cancellable, error);

		if (success) {
			EWebDAVResource *resource;

			resource = g_hash_table_lookup (resources_hash, nfo->extra);

			if (resource) {
				ICalComponent *icomp;

				if (g_strcmp0 (nfo->revision, etag) != 0) {
					g_free (nfo->revision);
					nfo->revision = etag;
					etag = NULL;
				}

				icomp = ecb_webdav_notes_new_icomp (resource->creation_date,
					resource->last_modified,
					nfo->uid,
					nfo->revision,
					resource->display_name,
					bytes);

				g_warn_if_fail (nfo->object == NULL);

				nfo->object = i_cal_component_as_ical_string (icomp);

				g_object_unref (icomp);
			} else { /* !resource */
				g_warn_if_reached ();
			}
		}

		g_free (etag);
		g_free (bytes);
	}

	return success;
}

static gchar *
ecb_webdav_notes_href_to_uid (const gchar *href)
{
	const gchar *filename;

	if (!href || !*href)
		return NULL;

	filename = strrchr (href, '/');

	if (filename && filename[1])
		return g_uri_unescape_string (filename + 1, NULL);

	return g_uri_unescape_string (href, NULL);
}

static gboolean
ecb_webdav_notes_get_changes_sync (ECalMetaBackend *meta_backend,
				   const gchar *last_sync_tag,
				   gboolean is_repeat,
				   gchar **out_new_sync_tag,
				   gboolean *out_repeat,
				   GSList **out_created_objects,
				   GSList **out_modified_objects,
				   GSList **out_removed_objects,
				   GCancellable *cancellable,
				   GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	GHashTable *known_items; /* gchar *href ~> ECalMetaBackendInfo * */
	GHashTable *resources_hash; /* gchar *href ~> EWebDAVResource *, both borrowed from 'resources' GSList */
	GHashTableIter iter;
	GSList *resources = NULL, *link;
	gpointer key = NULL, value = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_created_objects, FALSE);
	g_return_val_if_fail (out_modified_objects, FALSE);
	g_return_val_if_fail (out_removed_objects, FALSE);

	*out_new_sync_tag = NULL;
	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);
	webdav = ecb_webdav_notes_ref_session (cbnotes);

	if (cbnotes->priv->etag_supported) {
		gchar *new_sync_tag = NULL;

		success = ecb_webdav_notes_getetag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL);
		if (!success) {
			cbnotes->priv->etag_supported = g_cancellable_set_error_if_cancelled (cancellable, error);
			if (cbnotes->priv->etag_supported || !webdav) {
				g_clear_object (&webdav);
				return FALSE;
			}
		} else if (new_sync_tag && last_sync_tag && g_strcmp0 (last_sync_tag, new_sync_tag) == 0) {
			*out_new_sync_tag = new_sync_tag;
			g_clear_object (&webdav);
			return TRUE;
		}

		*out_new_sync_tag = new_sync_tag;
	}

	known_items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);
	resources_hash = g_hash_table_new (g_str_hash, g_str_equal);

	success = e_webdav_session_list_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS_AND_CHILDREN,
		E_WEBDAV_LIST_ETAG | E_WEBDAV_LIST_DISPLAY_NAME | E_WEBDAV_LIST_CREATION_DATE | E_WEBDAV_LIST_LAST_MODIFIED,
		&resources, cancellable, &local_error);

	if (success) {
		ECalCache *cal_cache;
		WebDAVNotesChangesData ccd;

		for (link = resources; link; link = g_slist_next (link)) {
			EWebDAVResource *resource = link->data;

			if (resource && resource->kind == E_WEBDAV_RESOURCE_KIND_RESOURCE && resource->href && g_str_has_suffix (resource->href, ".txt")) {
				gchar *filename = ecb_webdav_notes_href_to_uid (resource->href);

				g_hash_table_insert (known_items, g_strdup (resource->href),
					e_cal_meta_backend_info_new (filename, resource->etag, NULL, resource->href));

				g_hash_table_insert (resources_hash, resource->href, resource);

				g_free (filename);
			}
		}

		ccd.out_modified_objects = out_modified_objects;
		ccd.out_removed_objects = out_removed_objects;
		ccd.known_items = known_items;

		cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

		success = e_cal_cache_search_with_callback (cal_cache, NULL, ecb_webdav_notes_search_changes_cb, &ccd, cancellable, &local_error);

		g_clear_object (&cal_cache);
	}

	if (success) {
		g_hash_table_iter_init (&iter, known_items);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			*out_created_objects = g_slist_prepend (*out_created_objects, e_cal_meta_backend_info_copy (value));
		}
	}

	g_hash_table_destroy (known_items);

	if (success && (*out_created_objects || *out_modified_objects)) {
		success = ecb_webdav_notes_get_objects_sync (webdav, resources_hash, *out_created_objects, cancellable, &local_error);
		success = success && ecb_webdav_notes_get_objects_sync (webdav, resources_hash, *out_modified_objects, cancellable, &local_error);
	}

	if (local_error) {
		ecb_webdav_notes_check_credentials_error (cbnotes, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_slist_free_full (resources, e_webdav_resource_free);
	g_hash_table_destroy (resources_hash);
	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_webdav_notes_list_existing_sync (ECalMetaBackend *meta_backend,
				     gchar **out_new_sync_tag,
				     GSList **out_existing_objects,
				     GCancellable *cancellable,
				     GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	GSList *resources = NULL;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	*out_existing_objects = NULL;

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);
	webdav = ecb_webdav_notes_ref_session (cbnotes);

	success = e_webdav_session_list_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS_AND_CHILDREN, E_WEBDAV_LIST_ETAG, &resources, cancellable, &local_error);

	if (success) {
		GSList *link;

		for (link = resources; link; link = g_slist_next (link)) {
			EWebDAVResource *resource = link->data;

			if (resource && resource->kind == E_WEBDAV_RESOURCE_KIND_RESOURCE && resource->href && g_str_has_suffix (resource->href, ".txt")) {
				gchar *filename = ecb_webdav_notes_href_to_uid (resource->href);

				*out_existing_objects = g_slist_prepend (*out_existing_objects,
					e_cal_meta_backend_info_new (filename, resource->etag, NULL, resource->href));

				g_free (filename);
			}
		}

		*out_existing_objects = g_slist_reverse (*out_existing_objects);
	}

	if (local_error) {
		ecb_webdav_notes_check_credentials_error (cbnotes, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_slist_free_full (resources, e_webdav_resource_free);
	g_clear_object (&webdav);

	return success;
}

static gchar *
ecb_webdav_notes_uid_to_uri (ECalBackendWebDAVNotes *cbnotes,
			     const gchar *uid)
{
	ESourceWebdav *webdav_extension;
	SoupURI *soup_uri;
	gchar *uri, *tmp, *filename, *uid_hash = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (cbnotes), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	webdav_extension = e_source_get_extension (e_backend_get_source (E_BACKEND (cbnotes)), E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	soup_uri = e_source_webdav_dup_soup_uri (webdav_extension);
	g_return_val_if_fail (soup_uri != NULL, NULL);

	/* UIDs with forward slashes can cause trouble, because the destination server can
	   consider them as a path delimiter. Double-encode the URL doesn't always work,
	   thus rather cause a mismatch between stored UID and its href on the server. */
	if (strchr (uid, '/')) {
		uid_hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, uid, -1);

		if (uid_hash)
			uid = uid_hash;
	}

	filename = soup_uri_encode (uid, NULL);

	if (soup_uri->path) {
		gchar *slash = strrchr (soup_uri->path, '/');

		if (slash && !slash[1])
			*slash = '\0';
	}

	soup_uri_set_user (soup_uri, NULL);
	soup_uri_set_password (soup_uri, NULL);

	tmp = g_strconcat (soup_uri->path && *soup_uri->path ? soup_uri->path : "", "/", filename, NULL);
	soup_uri_set_path (soup_uri, tmp);
	g_free (tmp);

	uri = soup_uri_to_string (soup_uri, FALSE);

	soup_uri_free (soup_uri);
	g_free (filename);
	g_free (uid_hash);

	return uri;
}

static void
ecb_webdav_notes_store_component_etag (ICalComponent *icomp,
				       const gchar *etag)
{
	ICalComponent *subcomp;

	g_return_if_fail (icomp != NULL);
	g_return_if_fail (etag != NULL);

	e_cal_util_component_set_x_property (icomp, E_WEBDAV_NOTES_X_ETAG, etag);

	for (subcomp = i_cal_component_get_first_component (icomp, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, I_CAL_ANY_COMPONENT)) {
		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VJOURNAL_COMPONENT) {
			e_cal_util_component_set_x_property (subcomp, E_WEBDAV_NOTES_X_ETAG, etag);
		}
	}
}

static gboolean
ecb_webdav_notes_load_component_sync (ECalMetaBackend *meta_backend,
				      const gchar *uid,
				      const gchar *extra,
				      ICalComponent **out_component,
				      gchar **out_extra,
				      GCancellable *cancellable,
				      GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	EWebDAVResource *use_resource = NULL;
	gchar *uri = NULL, *href = NULL, *etag = NULL, *bytes = NULL;
	gsize length = -1;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);

	/* When called immediately after save and the server didn't change the component,
	   then the 'extra' contains "href" + "\n" + "vCalendar", to avoid unneeded GET
	   from the server. */
	if (extra && *extra) {
		const gchar *newline;

		newline = strchr (extra, '\n');
		if (newline && newline[1] && newline != extra) {
			ICalComponent *vcalendar;

			vcalendar = i_cal_component_new_from_string (newline + 1);
			if (vcalendar) {
				*out_extra = g_strndup (extra, newline - extra);
				*out_component = vcalendar;

				return TRUE;
			}
		}
	}

	webdav = ecb_webdav_notes_ref_session (cbnotes);

	if (extra && *extra) {
		uri = g_strdup (extra);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, &bytes, &length, cancellable, &local_error);

		if (!success) {
			g_free (uri);
			uri = NULL;
		}
	}

	if (!success && cbnotes->priv->etag_supported) {
		gchar *new_sync_tag = NULL;

		if (ecb_webdav_notes_getetag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL) && new_sync_tag) {
			gchar *last_sync_tag;

			last_sync_tag = e_cal_meta_backend_dup_sync_tag (meta_backend);

			/* The calendar didn't change, thus the component cannot be there */
			if (g_strcmp0 (last_sync_tag, new_sync_tag) == 0) {
				g_clear_error (&local_error);
				g_clear_object (&webdav);
				g_free (last_sync_tag);
				g_free (new_sync_tag);

				g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));

				return FALSE;
			}

			g_free (last_sync_tag);
		}

		g_free (new_sync_tag);
	}

	if (!success) {
		uri = ecb_webdav_notes_uid_to_uri (cbnotes, uid);
		g_return_val_if_fail (uri != NULL, FALSE);

		g_clear_error (&local_error);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, &bytes, &length, cancellable, &local_error);
	}

	if (success) {
		GSList *resources = NULL;

		success = e_webdav_session_list_sync (webdav, href, E_WEBDAV_DEPTH_THIS,
			E_WEBDAV_LIST_DISPLAY_NAME | E_WEBDAV_LIST_CREATION_DATE | E_WEBDAV_LIST_LAST_MODIFIED,
			&resources, cancellable, &local_error);

		if (success) {
			GSList *link;

			for (link = resources; link; link = g_slist_next (link)) {
				EWebDAVResource *resource = link->data;

				if (resource && resource->kind == E_WEBDAV_RESOURCE_KIND_RESOURCE && g_strcmp0 (resource->href, href) == 0) {
					use_resource = resource;
					link->data = NULL;
					break;
				}
			}

			g_slist_free_full (resources, e_webdav_resource_free);

			success = use_resource != NULL;

			if (!success)
				local_error = ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND);
		}
	}

	if (success) {
		*out_component = NULL;

		if (href && etag && length != ((gsize) -1)) {
			ICalComponent *icomp;
			gchar *filename = ecb_webdav_notes_href_to_uid (use_resource->href);

			icomp = ecb_webdav_notes_new_icomp (use_resource->creation_date,
				use_resource->last_modified,
				filename,
				etag,
				use_resource->display_name,
				bytes);

			g_free (filename);

			if (icomp) {
				ecb_webdav_notes_store_component_etag (icomp, etag);

				*out_component = icomp;
			}
		}

		if (!*out_component) {
			success = FALSE;

			if (!href)
				g_propagate_error (&local_error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Server didn’t return object’s href")));
			else if (!etag)
				g_propagate_error (&local_error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Server didn’t return object’s ETag")));
			else
				g_propagate_error (&local_error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		} else if (out_extra) {
			*out_extra = g_strdup (href);
		}
	}

	e_webdav_resource_free (use_resource);
	g_free (uri);
	g_free (href);
	g_free (etag);
	g_free (bytes);

	if (local_error) {
		ecb_webdav_notes_check_credentials_error (cbnotes, webdav, local_error);

		if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_NOT_FOUND)) {
			local_error->domain = E_CAL_CLIENT_ERROR;
			local_error->code = E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND;
		}

		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

/* A rough code to mimic what Nextcloud does, it's not accurate, but it's close
   enough to work similarly. It skips leading whitespaces and uses up to the first
   100 letters of the first non-empty line as the base file name. */
static gchar *
ecb_webdav_notes_construct_base_filename (const gchar *description)
{
	GString *base_filename;
	gunichar uchr;
	gboolean add_space = FALSE;

	if (!description || !*description || !g_utf8_validate (description, -1, NULL))
		return g_strdup (_("New note"));

	base_filename = g_string_sized_new (102);

	while (uchr = g_utf8_get_char (description), g_unichar_isspace (uchr))
		description = g_utf8_next_char (description);

	while (uchr = g_utf8_get_char (description), uchr && uchr != '\r' && uchr != '\n') {
		if (g_unichar_isspace (uchr)) {
			add_space = TRUE;
		} else if ((uchr >> 8) != 0 || !strchr ("\"/\\?:*|", (uchr & 0xFF))) {
			if (base_filename->len >= 98)
				break;

			if (add_space) {
				g_string_append_c (base_filename, ' ');
				add_space = FALSE;
			}

			g_string_append_unichar (base_filename, uchr);

			if (base_filename->len >= 100)
				break;
		}

		description = g_utf8_next_char (description);
	}

	if (!base_filename->len)
		g_string_append (base_filename, _("New note"));

	return g_string_free (base_filename, FALSE);
}

static gboolean
ecb_webdav_notes_save_component_sync (ECalMetaBackend *meta_backend,
				      gboolean overwrite_existing,
				      EConflictResolution conflict_resolution,
				      const GSList *instances,
				      const gchar *extra,
				      ECalOperationFlags opflags,
				      gchar **out_new_uid,
				      gchar **out_new_extra,
				      GCancellable *cancellable,
				      GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	ICalComponent *icomp;
	gchar *href = NULL, *etag = NULL;
	const gchar *description = NULL, *uid = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);
	g_return_val_if_fail (instances->data != NULL, FALSE);
	g_return_val_if_fail (instances->next == NULL, FALSE);
	g_return_val_if_fail (out_new_uid, FALSE);
	g_return_val_if_fail (out_new_extra, FALSE);

	icomp = e_cal_component_get_icalcomponent (instances->data);
	g_return_val_if_fail (i_cal_component_isa (icomp) == I_CAL_VJOURNAL_COMPONENT, FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);

	description = i_cal_component_get_description (icomp);
	etag = e_cal_util_component_dup_x_property (icomp, E_WEBDAV_NOTES_X_ETAG);
	uid = i_cal_component_get_uid (icomp);

	webdav = ecb_webdav_notes_ref_session (cbnotes);

	if (uid && (!overwrite_existing || (extra && *extra))) {
		gchar *new_extra = NULL, *new_etag = NULL;
		gchar *base_filename, *expected_filename;
		gboolean force_write = FALSE, new_filename = FALSE;
		guint counter = 1;

		base_filename = ecb_webdav_notes_construct_base_filename (description);
		expected_filename = g_strconcat (base_filename, ".txt", NULL);

		if (overwrite_existing) {
			switch (conflict_resolution) {
			case E_CONFLICT_RESOLUTION_FAIL:
			case E_CONFLICT_RESOLUTION_USE_NEWER:
			case E_CONFLICT_RESOLUTION_KEEP_SERVER:
			case E_CONFLICT_RESOLUTION_WRITE_COPY:
				break;
			case E_CONFLICT_RESOLUTION_KEEP_LOCAL:
				force_write = TRUE;
				break;
			}
		}

		new_filename = g_strcmp0 (expected_filename, uid) != 0;

		/* Checkw whether the saved file on the server is already with the "(nnn)" suffix */
		if (new_filename && expected_filename && uid &&
		    g_str_has_suffix (uid, ").txt") &&
		    g_ascii_strncasecmp (uid, expected_filename, strlen (expected_filename) - 4 /* strlen (".txt") */) == 0) {
			gint ii = strlen (expected_filename) - 4;

			if (uid[ii] == ' ' && uid[ii + 1] == '(') {
				ii += 2;

				while (uid[ii] >= '0' && uid[ii] <= '9')
					ii++;

				if (g_strcmp0 (uid + ii, ").txt") == 0)
					new_filename = FALSE;
			}
		}

		if (!extra || !*extra || new_filename) {
			href = ecb_webdav_notes_uid_to_uri (cbnotes, expected_filename);
			uid = expected_filename;
			force_write = FALSE;
		}

		do {
			if (!counter)
				break;

			g_clear_error (&local_error);

			if (counter > 1) {
				g_free (expected_filename);
				expected_filename = g_strdup_printf ("%s (%u).txt", base_filename, counter);

				g_free (href);
				href = ecb_webdav_notes_uid_to_uri (cbnotes, expected_filename);

				uid = expected_filename;
			}

			success = e_webdav_session_put_data_sync (webdav, (!new_filename && extra && *extra) ? extra : href,
				force_write ? "" : (overwrite_existing && !new_filename) ? etag : NULL, "text/plain; charset=\"utf-8\"",
				description ? description : "", -1, &new_extra, &new_etag, cancellable, &local_error);

			counter++;
		} while (!success && new_filename && !g_cancellable_is_cancelled (cancellable) &&
			 g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_PRECONDITION_FAILED));

		if (success && new_filename && extra && *extra) {
			/* The name on the server changed, remove the old file */
			e_webdav_session_delete_sync (webdav, extra, E_WEBDAV_DEPTH_THIS, etag, cancellable, NULL);
		}

		if (success) {
			/* Only if both are returned and it's not a weak ETag */
			if (new_extra && *new_extra && new_etag && *new_etag &&
			    g_ascii_strncasecmp (new_etag, "W/", 2) != 0) {
				ICalComponent *icomp_copy;
				gchar *tmp = NULL, *ical_string;
				glong now = (glong) time (NULL);

				if (g_str_has_suffix (uid, ".txt"))
					tmp = g_strndup (uid, strlen (uid) - 4);

				icomp_copy = ecb_webdav_notes_new_icomp (now, now, uid, new_etag,
					tmp ? tmp : uid, description);

				g_free (tmp);

				ical_string = i_cal_component_as_ical_string (icomp_copy);

				/* Encodes the href and the component into one string, which
				   will be decoded in the load function */
				tmp = g_strconcat (new_extra, "\n", ical_string, NULL);
				g_free (new_extra);
				new_extra = tmp;

				g_object_unref (icomp_copy);
				g_free (ical_string);
			}

			/* To read the component back, either from the new_extra
			   or from the server, because the server could change it */
			*out_new_uid = g_strdup (uid);

			if (out_new_extra)
				*out_new_extra = new_extra;
			else
				g_free (new_extra);
		}

		g_free (base_filename);
		g_free (expected_filename);
		g_free (new_etag);
	} else if (uid) {
		success = FALSE;
		g_propagate_error (error, ECC_ERROR_EX (E_CAL_CLIENT_ERROR_INVALID_OBJECT, _("Missing information about component URL, local cache is possibly incomplete or broken. Remove it, please.")));
	} else {
		success = FALSE;
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
	}

	g_free (href);
	g_free (etag);

	if (overwrite_existing && g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_PRECONDITION_FAILED)) {
		g_clear_error (&local_error);

		/* Pretend success when using the serer version on conflict,
		   the component will be updated during the refresh */
		if (conflict_resolution == E_CONFLICT_RESOLUTION_KEEP_SERVER)
			success = TRUE;
		else
			local_error = EC_ERROR (E_CLIENT_ERROR_OUT_OF_SYNC);
	}

	if (local_error) {
		ecb_webdav_notes_check_credentials_error (cbnotes, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_webdav_notes_remove_component_sync (ECalMetaBackend *meta_backend,
					EConflictResolution conflict_resolution,
					const gchar *uid,
					const gchar *extra,
					const gchar *object,
					ECalOperationFlags opflags,
					GCancellable *cancellable,
					GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	ICalComponent *icomp;
	gchar *etag = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);

	if (!extra || !*extra) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	icomp = i_cal_component_new_from_string (object);
	if (!icomp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	if (conflict_resolution == E_CONFLICT_RESOLUTION_FAIL)
		etag = e_cal_util_component_dup_x_property (icomp, E_WEBDAV_NOTES_X_ETAG);

	webdav = ecb_webdav_notes_ref_session (cbnotes);

	success = e_webdav_session_delete_sync (webdav, extra,
		NULL, etag, cancellable, &local_error);

	g_object_unref (icomp);
	g_free (etag);

	/* Ignore not found errors, this was a delete and the resource is gone.
	   It can be that it had been deleted on the server by other application. */
	if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_NOT_FOUND)) {
		g_clear_error (&local_error);
		success = TRUE;
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_PRECONDITION_FAILED)) {
		g_clear_error (&local_error);

		/* Pretend success when using the serer version on conflict,
		   the component will be updated during the refresh */
		if (conflict_resolution == E_CONFLICT_RESOLUTION_KEEP_SERVER)
			success = TRUE;
		else
			local_error = EC_ERROR (E_CLIENT_ERROR_OUT_OF_SYNC);
	}

	if (local_error) {
		ecb_webdav_notes_check_credentials_error (cbnotes, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_webdav_notes_get_ssl_error_details (ECalMetaBackend *meta_backend,
					gchar **out_certificate_pem,
					GTlsCertificateFlags *out_certificate_errors)
{
	ECalBackendWebDAVNotes *cbnotes;
	EWebDAVSession *webdav;
	gboolean res;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (meta_backend), FALSE);

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (meta_backend);
	webdav = ecb_webdav_notes_ref_session (cbnotes);

	if (!webdav)
		return FALSE;

	res = e_soup_session_get_ssl_error_details (E_SOUP_SESSION (webdav), out_certificate_pem, out_certificate_errors);

	g_clear_object (&webdav);

	return res;
}

static gchar *
ecb_webdav_notes_get_usermail (ECalBackendWebDAVNotes *cbnotes)
{
	ESource *source;
	ESourceAuthentication *auth_extension;
	ESourceWebdav *webdav_extension;
	const gchar *extension_name;
	gchar *usermail;
	gchar *username;
	gchar *res = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (cbnotes), NULL);

	source = e_backend_get_source (E_BACKEND (cbnotes));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	/* This will never return an empty string. */
	usermail = e_source_webdav_dup_email_address (webdav_extension);

	if (usermail)
		return usermail;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);
	username = e_source_authentication_dup_user (auth_extension);

	if (username && strchr (username, '@') && strrchr (username, '.') > strchr (username, '@')) {
		res = username;
		username = NULL;
	}

	g_free (username);

	return res;
}

static gchar *
ecb_webdav_notes_get_backend_property (ECalBackend *backend,
				       const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (backend)),
			E_CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED,
			E_CAL_STATIC_CAPABILITY_SIMPLE_MEMO,
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		return ecb_webdav_notes_get_usermail (E_CAL_BACKEND_WEBDAV_NOTES (backend));
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_webdav_notes_parent_class)->impl_get_backend_property (backend, prop_name);
}

static void
ecb_webdav_notes_notify_property_changed_cb (GObject *object,
					     GParamSpec *param,
					     gpointer user_data)
{
	ECalBackendWebDAVNotes *cbnotes = user_data;
	ECalBackend *cal_backend;
	gboolean email_address_changed;
	gchar *value;

	g_return_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (cbnotes));

	cal_backend = E_CAL_BACKEND (cbnotes);

	email_address_changed = param && g_strcmp0 (param->name, "email-address") == 0;

	if (email_address_changed) {
		value = ecb_webdav_notes_get_backend_property (cal_backend, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS);
		e_cal_backend_notify_property_changed (cal_backend, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS, value);
		e_cal_backend_notify_property_changed (cal_backend, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS, value);
		g_free (value);
	}
}

static void
ecb_webdav_notes_refresh_sync (ECalBackendSync *sync_backend,
			       EDataCal *cal,
			       GCancellable *cancellable,
			       GError **error)
{
	ECalBackendWebDAVNotes *cbnotes;

	g_return_if_fail (E_IS_CAL_BACKEND_WEBDAV_NOTES (sync_backend));

	cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (sync_backend);
	cbnotes->priv->been_connected = FALSE;

	/* Chain up to parent's method. */
	E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_webdav_notes_parent_class)->refresh_sync (sync_backend, cal, cancellable, error);
}

static gchar *
ecb_webdav_notes_dup_component_revision_cb (ECalCache *cal_cache,
					    ICalComponent *icomp)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, E_WEBDAV_NOTES_X_ETAG);
}

static void
e_cal_backend_webdav_notes_constructed (GObject *object)
{
	ECalBackendWebDAVNotes *cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (object);
	ECalCache *cal_cache;
	ESource *source;
	ESourceWebdav *webdav_extension;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_webdav_notes_parent_class)->constructed (object);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbnotes));

	g_signal_connect (cal_cache, "dup-component-revision",
		G_CALLBACK (ecb_webdav_notes_dup_component_revision_cb), NULL);

	g_clear_object (&cal_cache);

	source = e_backend_get_source (E_BACKEND (cbnotes));
	webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);

	g_signal_connect_object (webdav_extension, "notify::email-address",
		G_CALLBACK (ecb_webdav_notes_notify_property_changed_cb), cbnotes, 0);
}

static void
e_cal_backend_webdav_notes_dispose (GObject *object)
{
	ECalBackendWebDAVNotes *cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (object);

	g_mutex_lock (&cbnotes->priv->webdav_lock);
	g_clear_object (&cbnotes->priv->webdav);
	g_mutex_unlock (&cbnotes->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_webdav_notes_parent_class)->dispose (object);
}

static void
e_cal_backend_webdav_notes_finalize (GObject *object)
{
	ECalBackendWebDAVNotes *cbnotes = E_CAL_BACKEND_WEBDAV_NOTES (object);

	g_mutex_clear (&cbnotes->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_webdav_notes_parent_class)->finalize (object);
}

static void
e_cal_backend_webdav_notes_init (ECalBackendWebDAVNotes *cbnotes)
{
	cbnotes->priv = e_cal_backend_webdav_notes_get_instance_private (cbnotes);

	g_mutex_init (&cbnotes->priv->webdav_lock);
}

static void
e_cal_backend_webdav_notes_class_init (ECalBackendWebDAVNotesClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *cal_backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_webdav_notes_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_webdav_notes_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_webdav_notes_get_changes_sync;
	cal_meta_backend_class->list_existing_sync = ecb_webdav_notes_list_existing_sync;
	cal_meta_backend_class->load_component_sync = ecb_webdav_notes_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_webdav_notes_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_webdav_notes_remove_component_sync;
	cal_meta_backend_class->get_ssl_error_details = ecb_webdav_notes_get_ssl_error_details;

	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->refresh_sync = ecb_webdav_notes_refresh_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_webdav_notes_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_cal_backend_webdav_notes_constructed;
	object_class->dispose = e_cal_backend_webdav_notes_dispose;
	object_class->finalize = e_cal_backend_webdav_notes_finalize;
}

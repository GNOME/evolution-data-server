/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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
 */

/**
 * SECTION: e-cal-meta-backend
 * @include: libedata-cal/libedata-cal.h
 * @short_description: An #ECalBackend descendant for calendar backends
 *
 * The #ECalMetaBackend is an abstract #ECalBackend descendant which
 * aims to implement all evolution-data-server internals for the backend
 * itself and lefts the backend do as minimum work as possible, like
 * loading and saving components, listing available components and so on,
 * thus the backend implementation can focus on things like converting
 * (possibly) remote data into iCalendar objects and back.
 *
 * As the #ECalMetaBackend uses an #ECalCache, the offline support
 * is provided by default.
 *
 * The structure is thread safe.
 **/

#include "evolution-data-server-config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "e-cal-backend-sexp.h"
#include "e-cal-backend-sync.h"
#include "e-cal-backend-util.h"
#include "e-cal-meta-backend.h"

#define ECMB_KEY_SYNC_TAG "sync-tag"
#define ECMB_KEY_DID_CONNECT "did-connect"

#define LOCAL_PREFIX "file://"

struct _ECalMetaBackendPrivate {
	GMutex property_lock;
	ECalCache *cache;
	ENamedParameters *last_credentials;
	GHashTable *view_cancellables;
	GCancellable *refresh_cancellable;	/* Set when refreshing the content */
	GCancellable *source_changed_cancellable; /* Set when processing source changed signal */
	GCancellable *go_offline_cancellable;	/* Set when going offline */
	gboolean current_online_state;		/* The only state of the internal structures;
						   used to detect false notifications on EBackend::online */
	gulong source_changed_id;
	gulong notify_online_id;
	guint refresh_timeout_id;
};

enum {
	PROP_0,
	PROP_CACHE
};

enum {
	REFRESH_COMPLETED,
	SOURCE_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (ECalMetaBackend, e_cal_meta_backend, E_TYPE_CAL_BACKEND_SYNC)

G_DEFINE_BOXED_TYPE (ECalMetaBackendInfo, e_cal_meta_backend_info, e_cal_meta_backend_info_copy, e_cal_meta_backend_info_free)

static void ecmb_schedule_refresh (ECalMetaBackend *meta_backend);
static void ecmb_schedule_source_changed (ECalMetaBackend *meta_backend);
static void ecmb_schedule_go_offline (ECalMetaBackend *meta_backend);
static gboolean ecmb_load_component_wrapper_sync (ECalMetaBackend *meta_backend,
						  ECalCache *cal_cache,
						  const gchar *uid,
						  GCancellable *cancellable,
						  GError **error);
static gboolean ecmb_save_component_wrapper_sync (ECalMetaBackend *meta_backend,
						  ECalCache *cal_cache,
						  gboolean overwrite_existing,
						  EConflictResolution conflict_resolution,
						  const GSList *in_instances,
						  const gchar *extra,
						  const gchar *orig_uid,
						  gboolean *requires_put,
						  GCancellable *cancellable,
						  GError **error);

/**
 * e_cal_cache_search_data_new:
 * @uid: a component UID; cannot be %NULL
 * @rid: (nullable): a component Recurrence-ID; can be %NULL
 * @revision: (nullable): the component revision; can be %NULL
 *
 * Creates a new #ECalMetaBackendInfo prefilled with the given values.
 *
 * Returns: (transfer full): A new #ECalMetaBackendInfo. Free it with
 *    e_cal_meta_backend_info_new() when no longer needed.
 *
 * Since: 3.26
 **/
ECalMetaBackendInfo *
e_cal_meta_backend_info_new (const gchar *uid,
			     const gchar *rid,
			     const gchar *revision)
{
	ECalMetaBackendInfo *info;

	g_return_val_if_fail (uid != NULL, NULL);

	info = g_new0 (ECalMetaBackendInfo, 1);
	info->uid = g_strdup (uid);
	info->rid = g_strdup (rid && *rid ? rid : NULL);
	info->revision = g_strdup (revision);

	return info;
}

/**
 * e_cal_meta_backend_info_copy:
 * @src: (nullable): a source ECalMetaBackendInfo to copy, or %NULL
 *
 * Returns: (transfer full): Copy of the given @src. Free it with
 *    e_cal_meta_backend_info_free() when no longer needed.
 *    If the @src is %NULL, then returns %NULL as well.
 *
 * Since: 3.26
 **/
ECalMetaBackendInfo *
e_cal_meta_backend_info_copy (const ECalMetaBackendInfo *src)
{
	if (src)
		return NULL;

	return e_cal_meta_backend_info_new (src->uid, src->rid, src->revision);
}

/**
 * e_cal_meta_backend_info_free:
 * @ptr: (nullable): an #ECalMetaBackendInfo
 *
 * Frees the @ptr structure, previously allocated with e_cal_meta_backend_info_new()
 * or e_cal_meta_backend_info_copy().
 *
 * Since: 3.26
 **/
void
e_cal_meta_backend_info_free (gpointer ptr)
{
	ECalMetaBackendInfo *info = ptr;

	if (info) {
		g_free (info->uid);
		g_free (info->rid);
		g_free (info->revision);
		g_free (info);
	}
}

/* Unref returned cancellable with g_object_unref(), when done with it */
static GCancellable *
ecmb_create_view_cancellable (ECalMetaBackend *meta_backend,
			      EDataCalView *view)
{
	GCancellable *cancellable;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	g_mutex_lock (&meta_backend->priv->property_lock);

	cancellable = g_cancellable_new ();
	g_hash_table_insert (meta_backend->priv->view_cancellables, view, g_object_ref (cancellable));

	g_mutex_unlock (&meta_backend->priv->property_lock);

	return cancellable;
}

static GCancellable *
ecmb_steal_view_cancellable (ECalMetaBackend *meta_backend,
			     EDataCalView *view)
{
	GCancellable *cancellable;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);
	g_return_val_if_fail (E_IS_DATA_CAL_VIEW (view), NULL);

	g_mutex_lock (&meta_backend->priv->property_lock);

	cancellable = g_hash_table_lookup (meta_backend->priv->view_cancellables, view);
	if (cancellable) {
		g_object_ref (cancellable);
		g_hash_table_remove (meta_backend->priv->view_cancellables, view);
	}

	g_mutex_unlock (&meta_backend->priv->property_lock);

	return cancellable;
}

static gboolean
ecmb_connect_wrapper_sync (ECalMetaBackend *meta_backend,
			   GCancellable *cancellable,
			   GError **error)
{
	ENamedParameters *credentials;
	ESourceAuthenticationResult auth_result = E_SOURCE_AUTHENTICATION_UNKNOWN;
	ESourceCredentialsReason creds_reason = E_SOURCE_CREDENTIALS_REASON_ERROR;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);

	if (!e_backend_get_online (E_BACKEND (meta_backend))) {
		g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_REPOSITORY_OFFLINE,
			e_client_error_to_string (E_CLIENT_ERROR_REPOSITORY_OFFLINE));

		return FALSE;
	}

	g_mutex_lock (&meta_backend->priv->property_lock);
	credentials = e_named_parameters_new_clone (meta_backend->priv->last_credentials);
	g_mutex_unlock (&meta_backend->priv->property_lock);

	if (e_cal_meta_backend_connect_sync (meta_backend, credentials, &auth_result, &certificate_pem, &certificate_errors,
		cancellable, &local_error)) {
		e_named_parameters_free (credentials);
		return TRUE;
	}

	e_named_parameters_free (credentials);

	g_warn_if_fail (auth_result != E_SOURCE_AUTHENTICATION_ACCEPTED);

	switch (auth_result) {
	case E_SOURCE_AUTHENTICATION_UNKNOWN:
		if (local_error)
			g_propagate_error (error, local_error);
		g_free (certificate_pem);
		return FALSE;
	case E_SOURCE_AUTHENTICATION_ERROR:
		creds_reason = E_SOURCE_CREDENTIALS_REASON_ERROR;
		break;
	case E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED:
		creds_reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
		break;
	case E_SOURCE_AUTHENTICATION_ACCEPTED:
		g_warn_if_reached ();
		break;
	case E_SOURCE_AUTHENTICATION_REJECTED:
		creds_reason = E_SOURCE_CREDENTIALS_REASON_REJECTED;
		break;
	case E_SOURCE_AUTHENTICATION_REQUIRED:
		creds_reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;
		break;
	}

	e_backend_schedule_credentials_required (E_BACKEND (meta_backend), creds_reason, certificate_pem, certificate_errors,
		local_error, cancellable, G_STRFUNC);

	g_clear_error (&local_error);
	g_free (certificate_pem);

	return FALSE;
}

static gboolean
ecmb_gather_locally_cached_objects_cb (ECalCache *cal_cache,
				       const gchar *uid,
				       const gchar *rid,
				       const gchar *revision,
				       const gchar *object,
				       const gchar *extra,
				       EOfflineState offline_state,
				       gpointer user_data)
{
	GHashTable *locally_cached = user_data;

	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (locally_cached != NULL, FALSE);

	if (offline_state == E_OFFLINE_STATE_SYNCED) {
		g_hash_table_insert (locally_cached,
			e_cal_component_id_new (uid, rid),
			g_strdup (revision));
	}

	return TRUE;
}

static gboolean
ecmb_get_changes_sync (ECalMetaBackend *meta_backend,
		       const gchar *last_sync_tag,
		       gchar **out_new_sync_tag,
		       gboolean *out_repeat,
		       GSList **out_created_objects,
		       GSList **out_modified_objects,
		       GSList **out_removed_objects,
		       GCancellable *cancellable,
		       GError **error)
{
	GHashTable *locally_cached; /* ECalComponentId * ~> gchar *revision */
	GHashTableIter iter;
	GSList *existing_objects = NULL, *link;
	ECalCache *cal_cache;
	gpointer key, value;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (out_created_objects, FALSE);
	g_return_val_if_fail (out_modified_objects, FALSE);
	g_return_val_if_fail (out_removed_objects, FALSE);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	if (!e_backend_get_online (E_BACKEND (meta_backend)))
		return TRUE;

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return FALSE;
	}

	if (!ecmb_connect_wrapper_sync (meta_backend, cancellable, error) ||
	    !e_cal_meta_backend_list_existing_sync (meta_backend, out_new_sync_tag, &existing_objects, cancellable, error)) {
		g_object_unref (cal_cache);
		return FALSE;
	}

	locally_cached = g_hash_table_new_full (
		(GHashFunc) e_cal_component_id_hash,
		(GEqualFunc) e_cal_component_id_equal,
		(GDestroyNotify) e_cal_component_free_id,
		g_free);

	g_warn_if_fail (e_cal_cache_search_with_callback (cal_cache, NULL,
		ecmb_gather_locally_cached_objects_cb, locally_cached, cancellable, error));

	for (link = existing_objects; link; link = g_slist_next (link)) {
		ECalMetaBackendInfo *nfo = link->data;
		ECalComponentId id;

		if (!nfo)
			continue;

		id.uid = nfo->uid;
		id.rid = nfo->rid;

		if (!g_hash_table_contains (locally_cached, &id)) {
			link->data = NULL;

			*out_created_objects = g_slist_prepend (*out_created_objects, nfo);
		} else {
			const gchar *local_revision = g_hash_table_lookup (locally_cached, &id);

			if (g_strcmp0 (local_revision, nfo->revision) != 0) {
				link->data = NULL;

				*out_modified_objects = g_slist_prepend (*out_modified_objects, nfo);
			}

			g_hash_table_remove (locally_cached, &id);
		}
	}

	/* What left in the hash table is removed from the remote side */
	g_hash_table_iter_init (&iter, locally_cached);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const ECalComponentId *id = key;
		const gchar *revision = value;
		ECalMetaBackendInfo *nfo;

		if (!id) {
			g_warn_if_reached ();
			continue;
		}

		/* Skit detached instances, if the master object is still in the cache */
		if (id->rid && *id->rid) {
			ECalComponentId master_id;

			master_id.uid = id->uid;
			master_id.rid = NULL;

			if (!g_hash_table_contains (locally_cached, &master_id))
				continue;
		}

		nfo = e_cal_meta_backend_info_new (id->uid, id->rid, revision);
		*out_removed_objects = g_slist_prepend (*out_removed_objects, nfo);
	}

	g_slist_free_full (existing_objects, e_cal_meta_backend_info_free);
	g_hash_table_destroy (locally_cached);
	g_object_unref (cal_cache);

	*out_created_objects = g_slist_reverse (*out_created_objects);
	*out_modified_objects = g_slist_reverse (*out_modified_objects);
	*out_removed_objects = g_slist_reverse (*out_removed_objects);

	return TRUE;
}

static void
ecmb_start_view_thread_func (ECalBackend *cal_backend,
			     gpointer user_data,
			     GCancellable *cancellable,
			     GError **error)
{
	EDataCalView *view = user_data;
	ECalBackendSExp *sexp;
	ECalCache *cal_cache;
	const gchar *expr = NULL;

	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));
	g_return_if_fail (E_IS_DATA_CAL_VIEW (view));

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	/* Fill the view with known (locally stored) components satisfying the expression */
	sexp = e_data_cal_view_get_sexp (view);
	if (sexp)
		expr = e_cal_backend_sexp_text (sexp);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cal_backend));
	if (cal_cache) {
		GSList *components = NULL;

		if (e_cal_cache_search_components (cal_cache, expr, &components, cancellable, error) && components) {
			if (!g_cancellable_is_cancelled (cancellable))
				e_data_cal_view_notify_components_added (view, components);

			g_slist_free_full (components, g_object_unref);
		}

		g_object_unref (cal_cache);
	}
}

static gboolean
ecmb_upload_local_changes_sync (ECalMetaBackend *meta_backend,
				ECalCache *cal_cache,
				EConflictResolution conflict_resolution,
				GCancellable *cancellable,
				GError **error)
{
	GSList *offline_changes, *link;
	GHashTable *covered_uids;
	ECache *cache;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	cache = E_CACHE (cal_cache);
	covered_uids = g_hash_table_new (g_str_hash, g_str_equal);

	offline_changes = e_cal_cache_get_offline_changes (cal_cache, cancellable, error);
	for (link = offline_changes; link && success; link = g_slist_next (link)) {
		ECalCacheOfflineChange *change = link->data;
		gchar *extra = NULL;

		success = !g_cancellable_set_error_if_cancelled (cancellable, error);
		if (!success)
			break;

		if (!change || g_hash_table_contains (covered_uids, change->uid))
			continue;

		g_hash_table_insert (covered_uids, change->uid, NULL);

		if (!e_cal_cache_get_component_extra (cal_cache, change->uid, NULL, &extra, cancellable, NULL))
			extra = NULL;

		if (change->state == E_OFFLINE_STATE_LOCALLY_CREATED ||
		    change->state == E_OFFLINE_STATE_LOCALLY_MODIFIED) {
			GSList *instances = NULL;

			success = e_cal_cache_get_components_by_uid (cal_cache, change->uid, &instances, cancellable, error);
			if (success) {
				success = ecmb_save_component_wrapper_sync (meta_backend, cal_cache,
					change->state == E_OFFLINE_STATE_LOCALLY_MODIFIED,
					conflict_resolution, instances, extra, change->uid, NULL, cancellable, error);
			}

			g_slist_free_full (instances, g_object_unref);
		} else if (change->state == E_OFFLINE_STATE_LOCALLY_DELETED) {
			GError *local_error = NULL;

			success = e_cal_meta_backend_remove_component_sync (meta_backend, conflict_resolution,
				change->uid, extra, cancellable, &local_error);

			if (!success) {
				if (g_error_matches (local_error, E_DATA_CAL_ERROR, ObjectNotFound)) {
					g_clear_error (&local_error);
					success = TRUE;
				} else if (local_error) {
					g_propagate_error (error, local_error);
				}
			}
		} else {
			g_warn_if_reached ();
		}

		g_free (extra);
	}

	g_slist_free_full (offline_changes, e_cal_cache_offline_change_free);
	g_hash_table_destroy (covered_uids);

	if (success)
		success = e_cache_clear_offline_changes (cache, cancellable, error);

	return success;
}

static void
ecmb_refresh_thread_func (ECalBackend *cal_backend,
			  gpointer user_data,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	ECacheOfflineFlag offline_flag = E_CACHE_IS_ONLINE;
	gboolean success, repeat = TRUE;

	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		goto done;

	meta_backend = E_CAL_META_BACKEND (cal_backend);

	if (!e_backend_get_online (E_BACKEND (meta_backend)) ||
	    !ecmb_connect_wrapper_sync (meta_backend, cancellable, NULL)) {
		/* Ignore connection errors here */
		goto done;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache)
		goto done;

	success = ecmb_upload_local_changes_sync (meta_backend, cal_cache, E_CONFLICT_RESOLUTION_KEEP_LOCAL, cancellable, error);

	while (repeat && success &&
	       !g_cancellable_set_error_if_cancelled (cancellable, error)) {
		GSList *created_objects = NULL, *modified_objects = NULL, *removed_objects = NULL, *link;
		gchar *last_sync_tag, *new_sync_tag = NULL;

		repeat = FALSE;

		last_sync_tag = e_cache_dup_key (E_CACHE (cal_cache), ECMB_KEY_SYNC_TAG, NULL);
		if (last_sync_tag && !*last_sync_tag) {
			g_free (last_sync_tag);
			last_sync_tag = NULL;
		}

		success = e_cal_meta_backend_get_changes_sync (meta_backend, last_sync_tag, &new_sync_tag, &repeat,
			&created_objects, &modified_objects, &removed_objects, cancellable, error);

		if (success) {
			GHashTable *covered_uids;

			covered_uids = g_hash_table_new (g_str_hash, g_str_equal);

			/* Removed objects first */
			for (link = removed_objects; link && success; link = g_slist_next (link)) {
				ECalMetaBackendInfo *nfo = link->data;
				ECalComponentId id;

				if (!nfo) {
					g_warn_if_reached ();
					continue;
				}

				id.uid = nfo->uid;
				id.rid = nfo->rid;

				success = e_cal_cache_remove_component (cal_cache, id.uid, id.rid, offline_flag, cancellable, error);
				if (success)
					e_cal_backend_notify_component_removed (cal_backend, &id, NULL, NULL);
			}

			/* Then modified objects */
			for (link = modified_objects; link && success; link = g_slist_next (link)) {
				ECalMetaBackendInfo *nfo = link->data;

				if (!nfo || !nfo->uid) {
					g_warn_if_reached ();
					continue;
				}

				if (g_hash_table_contains (covered_uids, nfo->uid))
					continue;

				g_hash_table_insert (covered_uids, nfo->uid, NULL);

				success = ecmb_load_component_wrapper_sync (meta_backend, cal_cache, nfo->uid, cancellable, error);
			}

			g_hash_table_remove_all (covered_uids);

			/* Finally created objects */
			for (link = created_objects; link && success; link = g_slist_next (link)) {
				ECalMetaBackendInfo *nfo = link->data;

				if (!nfo || !nfo->uid) {
					g_warn_if_reached ();
					continue;
				}

				success = ecmb_load_component_wrapper_sync (meta_backend, cal_cache, nfo->uid, cancellable, error);
			}

			g_hash_table_destroy (covered_uids);
		}

		if (success) {
			e_cache_set_key (E_CACHE (cal_cache), ECMB_KEY_SYNC_TAG, new_sync_tag ? new_sync_tag : "", NULL);
		}

		g_slist_free_full (created_objects, e_cal_meta_backend_info_free);
		g_slist_free_full (modified_objects, e_cal_meta_backend_info_free);
		g_slist_free_full (removed_objects, e_cal_meta_backend_info_free);
		g_free (last_sync_tag);
		g_free (new_sync_tag);
	}

	g_object_unref (cal_cache);

 done:
	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->refresh_cancellable == cancellable)
		g_clear_object (&meta_backend->priv->refresh_cancellable);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	g_signal_emit (meta_backend, signals[REFRESH_COMPLETED], 0, NULL);
}

static void
ecmb_source_refresh_timeout_cb (ESource *source,
				gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	ECalMetaBackend *meta_backend;

	g_return_if_fail (weak_ref != NULL);

	meta_backend = g_weak_ref_get (weak_ref);
	if (meta_backend) {
		ecmb_schedule_refresh (meta_backend);
		g_object_unref (meta_backend);
	}
}

static void
ecmb_source_changed_thread_func (ECalBackend *cal_backend,
				 gpointer user_data,
				 GCancellable *cancellable,
				 GError **error)
{
	ECalMetaBackend *meta_backend;

	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	meta_backend = E_CAL_META_BACKEND (cal_backend);

	g_mutex_lock (&meta_backend->priv->property_lock);
	if (!meta_backend->priv->refresh_timeout_id) {
		ESource *source = e_backend_get_source (E_BACKEND (meta_backend));

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_REFRESH)) {
			meta_backend->priv->refresh_timeout_id = e_source_refresh_add_timeout (source, NULL,
				ecmb_source_refresh_timeout_cb, e_weak_ref_new (meta_backend), (GDestroyNotify) e_weak_ref_free);
		}
	}
	g_mutex_unlock (&meta_backend->priv->property_lock);

	g_signal_emit (meta_backend, signals[SOURCE_CHANGED], 0, NULL);

	if (e_backend_get_online (E_BACKEND (meta_backend)) &&
	    e_cal_meta_backend_disconnect_sync (meta_backend, cancellable, error)) {
		ecmb_schedule_refresh (meta_backend);
	}
}

static void
ecmb_go_offline_thread_func (ECalBackend *cal_backend,
			     gpointer user_data,
			     GCancellable *cancellable,
			     GError **error)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	e_cal_meta_backend_disconnect_sync (E_CAL_META_BACKEND (cal_backend), cancellable, error);
}

static ECalComponent *
ecmb_find_in_instances (const GSList *instances, /* ECalComponent * */
			const gchar *uid,
			const gchar *rid)
{
	GSList *link;

	for (link = (GSList *) instances; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id;

		if (!comp)
			continue;

		id = e_cal_component_get_id (comp);
		if (!id)
			continue;

		if (g_strcmp0 (id->uid, uid) == 0 &&
		    g_strcmp0 (id->rid, rid) == 0) {
			e_cal_component_free_id (id);
			return comp;
		}

		e_cal_component_free_id (id);
	}

	return NULL;
}

static gboolean
ecmb_maybe_remove_from_cache (ECalMetaBackend *meta_backend,
			      ECalCache *cal_cache,
			      ECacheOfflineFlag offline_flag,
			      const gchar *uid,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackend *cal_backend;
	GSList *comps = NULL, *link;
	GError *local_error = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);

	if (!e_cal_cache_get_components_by_uid (cal_cache, uid, &comps, cancellable, &local_error)) {
		if (g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
			g_clear_error (&local_error);
			return TRUE;
		}

		g_propagate_error (error, local_error);
		return FALSE;
	}

	cal_backend = E_CAL_BACKEND (meta_backend);

	for (link = comps; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		ECalComponentId *id;

		g_warn_if_fail (E_IS_CAL_COMPONENT (comp));

		if (!E_IS_CAL_COMPONENT (comp))
			continue;

		id = e_cal_component_get_id (comp);
		if (id) {
			if (!e_cal_cache_remove_component (cal_cache, id->uid, id->rid, offline_flag, cancellable, error)) {
				e_cal_component_free_id (id);
				g_slist_free_full (comps, g_object_unref);

				return FALSE;
			}

			e_cal_backend_notify_component_removed (cal_backend, id, comp, NULL);
			e_cal_component_free_id (id);
		}
	}

	g_slist_free_full (comps, g_object_unref);

	return TRUE;
}

static gboolean
ecmb_put_one_component (ECalMetaBackend *meta_backend,
			ECalCache *cal_cache,
			ECacheOfflineFlag offline_flag,
			ECalComponent *comp,
			const gchar *extra,
			GSList **inout_cache_instances,
			GCancellable *cancellable,
			GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (comp != NULL, FALSE);
	g_return_val_if_fail (inout_cache_instances != NULL, FALSE);

	if (e_cal_component_has_attachments (comp)) {
		success = e_cal_meta_backend_store_inline_attachments_sync (meta_backend,
			e_cal_component_get_icalcomponent (comp), cancellable, error);
		e_cal_component_rescan (comp);
	}

	success = success && e_cal_cache_put_component (cal_cache, comp, extra, offline_flag, cancellable, error);

	if (success) {
		ECalComponent *existing = NULL;
		ECalComponentId *id;

		id = e_cal_component_get_id (comp);
		if (id) {
			existing = ecmb_find_in_instances (*inout_cache_instances, id->uid, id->rid);

			e_cal_component_free_id (id);
		}

		if (existing) {
			e_cal_backend_notify_component_modified (E_CAL_BACKEND (meta_backend), existing, comp);
			*inout_cache_instances = g_slist_remove (*inout_cache_instances, existing);

			g_clear_object (&existing);
		} else {
			e_cal_backend_notify_component_created (E_CAL_BACKEND (meta_backend), comp);
		}
	}

	return success;
}

static gboolean
ecmb_put_instances (ECalMetaBackend *meta_backend,
		    ECalCache *cal_cache,
		    const gchar *uid,
		    ECacheOfflineFlag offline_flag,
		    const GSList *new_instances, /* ECalComponent * */
		    const gchar *extra,
		    GCancellable *cancellable,
		    GError **error)
{
	GSList *cache_instances = NULL, *link;
	gboolean success = TRUE;
	GError *local_error = NULL;

	if (!e_cal_cache_get_components_by_uid (cal_cache, uid, &cache_instances, cancellable, &local_error)) {
		if (g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
			g_clear_error (&local_error);
		} else {
			g_propagate_error (error, local_error);

			return FALSE;
		}
	}

	for (link = (GSList *) new_instances; link && success; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;

		success = ecmb_put_one_component (meta_backend, cal_cache, offline_flag, comp, extra, &cache_instances, cancellable, error);
	}

	/* What left got removed from the remote side, notify about it */
	if (success && cache_instances) {
		ECalBackend *cal_backend = E_CAL_BACKEND (meta_backend);
		GSList *link;

		for (link = cache_instances; link && success; link = g_slist_next (link)) {
			ECalComponent *comp = link->data;
			ECalComponentId *id;

			id = e_cal_component_get_id (comp);
			if (!id)
				continue;

			success = e_cal_cache_remove_component (cal_cache, id->uid, id->rid, offline_flag, cancellable, error);

			e_cal_backend_notify_component_removed (cal_backend, id, comp, NULL);

			e_cal_component_free_id (id);
		}
	}

	g_slist_free_full (cache_instances, g_object_unref);

	return success;
}

static void
ecmb_gather_timezones (ECalMetaBackend *meta_backend,
		       ETimezoneCache *timezone_cache,
		       icalcomponent *icalcomp)
{
	icalcomponent *subcomp;
	icaltimezone *zone;

	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));
	g_return_if_fail (E_IS_TIMEZONE_CACHE (timezone_cache));
	g_return_if_fail (icalcomp != NULL);

	zone = icaltimezone_new ();

	for (subcomp = icalcomponent_get_first_component (icalcomp, ICAL_VTIMEZONE_COMPONENT);
	     subcomp;
	     subcomp = icalcomponent_get_next_component (icalcomp, ICAL_VTIMEZONE_COMPONENT)) {
		icalcomponent *clone;

		clone = icalcomponent_new_clone (subcomp);

		if (icaltimezone_set_component (zone, clone)) {
			e_timezone_cache_add_timezone (timezone_cache, zone);
		} else {
			icalcomponent_free (clone);
		}
	}

	icaltimezone_free (zone, TRUE);
}

static gboolean
ecmb_load_component_wrapper_sync (ECalMetaBackend *meta_backend,
				  ECalCache *cal_cache,
				  const gchar *uid,
				  GCancellable *cancellable,
				  GError **error)
{
	ECacheOfflineFlag offline_flag = E_CACHE_IS_ONLINE;
	icalcomponent *icalcomp = NULL;
	GSList *new_instances = NULL;
	gchar *extra = NULL;
	gboolean success = TRUE;

	if (!e_cal_meta_backend_load_component_sync (meta_backend, uid, &icalcomp, &extra, cancellable, error) ||
	    !icalcomp) {
		g_free (extra);
		return FALSE;
	}

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_kind kind;
		icalcomponent *subcomp;

		ecmb_gather_timezones (meta_backend, E_TIMEZONE_CACHE (cal_cache), icalcomp);

		kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

		for (subcomp = icalcomponent_get_first_component (icalcomp, kind);
		     subcomp && success;
		     subcomp = icalcomponent_get_next_component (icalcomp, kind)) {
			ECalComponent *comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (subcomp));

			if (comp)
				new_instances = g_slist_prepend (new_instances, comp);
		}
	} else {
		ECalComponent *comp = e_cal_component_new_from_icalcomponent (icalcomp);

		icalcomp = NULL;

		if (comp)
			new_instances = g_slist_prepend (new_instances, comp);
	}

	if (new_instances) {
		new_instances = g_slist_reverse (new_instances);

		success = ecmb_put_instances (meta_backend, cal_cache, uid, offline_flag,
			new_instances, extra, cancellable, error);
	} else {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, _("Received object is invalid")));
		success = FALSE;
	}

	g_slist_free_full (new_instances, g_object_unref);
	if (icalcomp)
		icalcomponent_free (icalcomp);
	g_free (extra);

	return success;
}

static gboolean
ecmb_save_component_wrapper_sync (ECalMetaBackend *meta_backend,
				  ECalCache *cal_cache,
				  gboolean overwrite_existing,
				  EConflictResolution conflict_resolution,
				  const GSList *in_instances,
				  const gchar *extra,
				  const gchar *orig_uid,
				  gboolean *out_requires_put,
				  GCancellable *cancellable,
				  GError **error)
{
	GSList *link, *instances = NULL;
	gchar *new_uid = NULL;
	gboolean has_attachments = FALSE, success = TRUE;

	for (link = (GSList *) in_instances; link && !has_attachments; link = g_slist_next (link)) {
		has_attachments = e_cal_component_has_attachments (link->data);
	}

	if (has_attachments) {
		instances = g_slist_copy ((GSList *) in_instances);

		for (link = instances; link; link = g_slist_next (link)) {
			ECalComponent *comp = link->data;

			if (success && e_cal_component_has_attachments (comp)) {
				comp = e_cal_component_clone (comp);
				link->data = comp;

				success = e_cal_meta_backend_inline_local_attachments_sync (meta_backend,
					e_cal_component_get_icalcomponent (comp), cancellable, error);
				e_cal_component_rescan (comp);
			} else {
				g_object_ref (comp);
			}
		}
	}

	success = success && e_cal_meta_backend_save_component_sync (meta_backend, overwrite_existing, conflict_resolution,
		instances ? instances : in_instances, extra, &new_uid, cancellable, error);

	if (success && new_uid) {
		success = ecmb_load_component_wrapper_sync (meta_backend, cal_cache, new_uid, cancellable, error);

		if (success && g_strcmp0 (new_uid, orig_uid) != 0)
		    success = ecmb_maybe_remove_from_cache (meta_backend, cal_cache, E_CACHE_IS_ONLINE, orig_uid, cancellable, error);

		g_free (new_uid);

		if (out_requires_put)
			*out_requires_put = FALSE;
	}

	g_slist_free_full (instances, g_object_unref);

	return success;
}

static void
ecmb_open_sync (ECalBackendSync *sync_backend,
		EDataCal *cal,
		GCancellable *cancellable,
		gboolean only_if_exists,
		GError **error)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));

	/* Not much to do here, just confirm and schedule refresh */
	ecmb_schedule_refresh (E_CAL_META_BACKEND (sync_backend));
}

static void
ecmb_refresh_sync (ECalBackendSync *sync_backend,
		   EDataCal *cal,
		   GCancellable *cancellable,
		   GError **error)

{
	ECalMetaBackend *meta_backend;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));

	meta_backend = E_CAL_META_BACKEND (sync_backend);

	if (!e_backend_get_online (E_BACKEND (sync_backend)))
		return;

	if (ecmb_connect_wrapper_sync (meta_backend, cancellable, error))
		ecmb_schedule_refresh (meta_backend);
}

static void
ecmb_get_object_sync (ECalBackendSync *sync_backend,
		      EDataCal *cal,
		      GCancellable *cancellable,
		      const gchar *uid,
		      const gchar *rid,
		      gchar **calobj,
		      GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (calobj != NULL);

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	if (!e_cal_cache_get_component_as_string (cal_cache, uid, rid, calobj, cancellable, &local_error) &&
	    g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
		gboolean found = FALSE;

		g_clear_error (&local_error);

		/* Ignore errors here, just try whether it's on the remote side, but not in the local cache */
		if (e_backend_get_online (E_BACKEND (meta_backend)) &&
		    ecmb_connect_wrapper_sync (meta_backend, cancellable, NULL) &&
		    ecmb_load_component_wrapper_sync (meta_backend, cal_cache, uid, cancellable, NULL)) {
			found = e_cal_cache_get_component_as_string (cal_cache, uid, rid, calobj, cancellable, NULL);
		}

		if (!found)
			g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
	} else if (local_error) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, local_error->message));
		g_clear_error (&local_error);
	}

	g_object_unref (cal_cache);
}

static void
ecmb_get_object_list_sync (ECalBackendSync *sync_backend,
			   EDataCal *cal,
			   GCancellable *cancellable,
			   const gchar *sexp,
			   GSList **calobjs,
			   GError **error)
{
	ECalCache *cal_cache;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (calobjs != NULL);

	*calobjs = NULL;
	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (sync_backend));

	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	if (e_cal_cache_search (cal_cache, sexp, calobjs, cancellable, error)) {
		GSList *link;

		for (link = *calobjs; link; link = g_slist_next (link)) {
			ECalCacheSearchData *search_data = link->data;
			gchar *icalstring = NULL;

			if (search_data) {
				icalstring = g_strdup (search_data->object);
				e_cal_cache_search_data_free (search_data);
			}

			link->data = icalstring;
		}
	}

	g_object_unref (cal_cache);
}

static gboolean
ecmb_add_free_busy_instance_cb (icalcomponent *icalcomp,
				struct icaltimetype instance_start,
				struct icaltimetype instance_end,
				gpointer user_data,
				GCancellable *cancellable,
				GError **error)
{
	icalcomponent *vfreebusy = user_data;
	icalproperty *prop, *classification;
	icalparameter *param;
	struct icalperiodtype ipt;

	ipt.start = instance_start;
	ipt.end = instance_end;
	ipt.duration = icaldurationtype_null_duration ();

        /* Add busy information to the VFREEBUSY component */
	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	icalproperty_set_freebusy (prop, ipt);

	param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
	icalproperty_add_parameter (prop, param);

	classification = icalcomponent_get_first_property (icalcomp, ICAL_CLASS_PROPERTY);
	if (!classification || icalproperty_get_class (classification) == ICAL_CLASS_PUBLIC) {
		const gchar *str;

		str = icalcomponent_get_summary (icalcomp);
		if (str && *str) {
			param = icalparameter_new_x (str);
			icalparameter_set_xname (param, "X-SUMMARY");
			icalproperty_add_parameter (prop, param);
		}

		str = icalcomponent_get_location (icalcomp);
		if (str && *str) {
			param = icalparameter_new_x (str);
			icalparameter_set_xname (param, "X-LOCATION");
			icalproperty_add_parameter (prop, param);
		}
	}

	icalcomponent_add_property (vfreebusy, prop);

	return TRUE;
}

static void
ecmb_get_free_busy_sync (ECalBackendSync *sync_backend,
			 EDataCal *cal,
			 GCancellable *cancellable,
			 const GSList *users,
			 time_t start,
			 time_t end,
			 GSList **out_freebusy,
			 GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	GSList *link, *components = NULL;
	gchar *cal_email_address, *mailto;
	icalcomponent *vfreebusy, *icalcomp;
	icalproperty *prop;
	icaltimezone *utc_zone;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (out_freebusy != NULL);

	meta_backend = E_CAL_META_BACKEND (sync_backend);

	*out_freebusy = NULL;

	if (!users)
		return;

	cal_email_address = e_cal_backend_get_backend_property (E_CAL_BACKEND (meta_backend), CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS);
	if (!cal_email_address)
		return;

	for (link = (GSList *) users; link; link = g_slist_next (link)) {
		const gchar *user = link->data;

		if (user && g_ascii_strcasecmp (user, cal_email_address) == 0)
			break;
	}

	if (!link) {
		g_free (cal_email_address);
		return;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

	if (!cal_cache || !e_cal_cache_get_components_in_range (cal_cache, start, end, &components, cancellable, error)) {
		g_clear_object (&cal_cache);
		g_free (cal_email_address);
		return;
	}

	vfreebusy = icalcomponent_new_vfreebusy ();

	mailto = g_strconcat ("mailto:", cal_email_address, NULL);
	prop = icalproperty_new_organizer (mailto);
	g_free (mailto);

	if (prop)
		icalcomponent_add_property (vfreebusy, prop);

	utc_zone = icaltimezone_get_utc_timezone ();
	icalcomponent_set_dtstart (vfreebusy, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfreebusy, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	for (link = components; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;

		if (!E_IS_CAL_COMPONENT (comp)) {
			g_warn_if_reached ();
			continue;
		}

		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (!icalcomp)
			continue;

		/* If the event is TRANSPARENT, skip it. */
		prop = icalcomponent_get_first_property (icalcomp, ICAL_TRANSP_PROPERTY);
		if (prop) {
			icalproperty_transp transp_val = icalproperty_get_transp (prop);
			if (transp_val == ICAL_TRANSP_TRANSPARENT ||
			    transp_val == ICAL_TRANSP_TRANSPARENTNOCONFLICT)
				continue;
		}

		if (!e_cal_recur_generate_instances_sync (icalcomp,
			icaltime_from_timet_with_zone (start, FALSE, NULL),
			icaltime_from_timet_with_zone (end, FALSE, NULL),
			ecmb_add_free_busy_instance_cb, vfreebusy,
			e_cal_cache_resolve_timezone_cb, cal_cache,
			utc_zone, cancellable, error)) {
			break;
		}
	}

	*out_freebusy = g_slist_prepend (*out_freebusy, icalcomponent_as_ical_string_r (vfreebusy));

	g_slist_free_full (components, g_object_unref);
	icalcomponent_free (vfreebusy);
	g_object_unref (cal_cache);
	g_free (cal_email_address);
}

static gboolean
ecmb_create_object_sync (ECalMetaBackend *meta_backend,
			 ECalCache *cal_cache,
			 ECacheOfflineFlag *offline_flag,
			 EConflictResolution conflict_resolution,
			 ECalComponent *comp,
			 gchar **out_new_uid,
			 ECalComponent **out_new_comp,
			 GCancellable *cancellable,
			 GError **error)
{
	icalcomponent *icalcomp;
	struct icaltimetype itt;
	const gchar *uid;
	gboolean success, requires_put = TRUE;

	g_return_val_if_fail (comp != NULL, FALSE);

	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (!icalcomp) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		return FALSE;
	}

	uid = icalcomponent_get_uid (icalcomp);
	if (!uid) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		if (!new_uid) {
			g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			return FALSE;
		}

		icalcomponent_set_uid (icalcomp, new_uid);
		uid = icalcomponent_get_uid (icalcomp);

		g_free (new_uid);
	}

	if (e_cal_cache_contains (cal_cache, uid, NULL, E_CACHE_EXCLUDE_DELETED)) {
		g_propagate_error (error, e_data_cal_create_error (ObjectIdAlreadyExists, NULL));
		return FALSE;
	}

	/* Set the created and last modified times on the component */
	itt = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &itt);
	e_cal_component_set_last_modified (comp, &itt);

	if (*offline_flag == E_CACHE_OFFLINE_UNKNOWN) {
		if (e_backend_get_online (E_BACKEND (meta_backend)) &&
		    ecmb_connect_wrapper_sync (meta_backend, cancellable, NULL)) {
			*offline_flag = E_CACHE_IS_ONLINE;
		} else {
			*offline_flag = E_CACHE_IS_OFFLINE;
		}
	}

	if (*offline_flag == E_CACHE_IS_ONLINE) {
		GSList *instances;

		instances = g_slist_prepend (NULL, comp);

		if (!ecmb_save_component_wrapper_sync (meta_backend, cal_cache, FALSE, conflict_resolution, instances, NULL, uid, &requires_put, cancellable, error)) {
			g_slist_free (instances);
			return FALSE;
		}

		g_slist_free (instances);
	}

	if (requires_put) {
		success = e_cal_cache_put_component (cal_cache, comp, NULL, *offline_flag, cancellable, error);
		if (success && !out_new_comp) {
			e_cal_backend_notify_component_created (E_CAL_BACKEND (meta_backend), comp);
		}
	} else {
		success = TRUE;
	}

	if (success) {
		if (out_new_uid)
			*out_new_uid = g_strdup (icalcomponent_get_uid (e_cal_component_get_icalcomponent (comp)));
		if (out_new_comp)
			*out_new_comp = g_object_ref (comp);
	}

	return success;
}

static void
ecmb_create_objects_sync (ECalBackendSync *sync_backend,
			  EDataCal *cal,
			  GCancellable *cancellable,
			  const GSList *calobjs,
			  GSList **out_uids,
			  GSList **out_new_components,
			  GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	ECacheOfflineFlag offline_flag = E_CACHE_OFFLINE_UNKNOWN;
	EConflictResolution conflict_resolution = E_CONFLICT_RESOLUTION_KEEP_LOCAL;
	icalcomponent_kind backend_kind;
	GSList *link;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (calobjs != NULL);
	g_return_if_fail (out_uids != NULL);
	g_return_if_fail (out_new_components != NULL);

	if (!e_cal_backend_get_writable (E_CAL_BACKEND (sync_backend))) {
		g_propagate_error (error, e_data_cal_create_error (PermissionDenied, NULL));
		return;
	}

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	backend_kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

	for (link = (GSList *) calobjs; link && success; link = g_slist_next (link)) {
		ECalComponent *comp, *new_comp = NULL;
		gchar *new_uid = NULL;

		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			break;

		comp = e_cal_component_new_from_string (link->data);
		if (!comp ||
		    !e_cal_component_get_icalcomponent (comp) ||
		    backend_kind != icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			g_clear_object (&comp);

			g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			break;
		}

		success = ecmb_create_object_sync (meta_backend, cal_cache, &offline_flag, conflict_resolution,
			comp, &new_uid, &new_comp, cancellable, error);

		if (success) {
			*out_uids = g_slist_prepend (*out_uids, new_uid);
			*out_new_components = g_slist_prepend (*out_new_components, new_comp);
		}

		g_object_unref (comp);
	}

	*out_uids = g_slist_reverse (*out_uids);
	*out_new_components = g_slist_reverse (*out_new_components);

	g_object_unref (cal_cache);
}

static gboolean
ecmb_modify_object_sync (ECalMetaBackend *meta_backend,
			 ECalCache *cal_cache,
			 ECacheOfflineFlag *offline_flag,
			 EConflictResolution conflict_resolution,
			 ECalObjModType mod,
			 ECalComponent *comp,
			 ECalComponent **out_old_comp,
			 ECalComponent **out_new_comp,
			 GCancellable *cancellable,
			 GError **error)
{
	struct icaltimetype itt;
	ECalComponentId *id;
	ECalComponent *old_comp = NULL, *new_comp = NULL, *master_comp, *existing_comp = NULL;
	GSList *instances = NULL;
	gchar *extra = NULL;
	gboolean success = TRUE, requires_put = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (comp != NULL, FALSE);

	id = e_cal_component_get_id (comp);
	if (!id) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		return FALSE;
	}

	if (!e_cal_cache_get_components_by_uid (cal_cache, id->uid, &instances, cancellable, &local_error)) {
		if (g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
			g_clear_error (&local_error);
			local_error = e_data_cal_create_error (ObjectNotFound, NULL);
		}

		g_propagate_error (error, local_error);
		e_cal_component_free_id (id);

		return FALSE;
	}

	master_comp = ecmb_find_in_instances (instances, id->uid, NULL);
	if (e_cal_component_is_instance (comp)) {
		/* Set detached instance as the old object */
		existing_comp = ecmb_find_in_instances (instances, id->uid, id->rid);

		if (!existing_comp && mod == E_CAL_OBJ_MOD_ONLY_THIS) {
			g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));

			g_slist_free_full (instances, g_object_unref);
			e_cal_component_free_id (id);

			return FALSE;
		}
	}

	if (!existing_comp)
		existing_comp = master_comp;

	if (!e_cal_cache_get_component_extra (cal_cache, id->uid, id->rid, &extra, cancellable, NULL) && id->rid) {
		if (!e_cal_cache_get_component_extra (cal_cache, id->uid, NULL, &extra, cancellable, NULL))
			extra = NULL;
	}

	/* Set the last modified time on the component */
	itt = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &itt);

	/* Remember old and new components */
	if (out_old_comp && existing_comp)
		old_comp = e_cal_component_clone (existing_comp);

	if (out_new_comp)
		new_comp = e_cal_component_clone (comp);

	switch (mod) {
	case E_CAL_OBJ_MOD_ONLY_THIS:
	case E_CAL_OBJ_MOD_THIS:
		if (e_cal_component_is_instance (comp)) {
			if (existing_comp != master_comp) {
				instances = g_slist_remove (instances, existing_comp);
				g_clear_object (&existing_comp);
			}
		} else {
			instances = g_slist_remove (instances, master_comp);
			g_clear_object (&master_comp);
			existing_comp = NULL;
		}

		instances = g_slist_append (instances, e_cal_component_clone (comp));
		break;
	case E_CAL_OBJ_MOD_ALL:
		e_cal_recur_ensure_end_dates (comp, TRUE, e_cal_cache_resolve_timezone_simple_cb, cal_cache);

		/* Replace master object */
		instances = g_slist_remove (instances, master_comp);
		g_clear_object (&master_comp);
		existing_comp = NULL;

		instances = g_slist_prepend (instances, e_cal_component_clone (comp));
		break;
	case E_CAL_OBJ_MOD_THIS_AND_PRIOR:
	case E_CAL_OBJ_MOD_THIS_AND_FUTURE:
		if (e_cal_component_is_instance (comp) && master_comp) {
			struct icaltimetype rid, master_dtstart;
			icalcomponent *icalcomp = e_cal_component_get_icalcomponent (comp);
			icalcomponent *split_icalcomp;
			icalproperty *prop;

			rid = icalcomponent_get_recurrenceid (icalcomp);

			if (mod == E_CAL_OBJ_MOD_THIS_AND_FUTURE &&
			    e_cal_util_is_first_instance (master_comp, icalcomponent_get_recurrenceid (icalcomp),
				e_cal_cache_resolve_timezone_simple_cb, cal_cache)) {
				icalproperty *prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);

				if (prop)
					icalcomponent_remove_property (icalcomp, prop);

				e_cal_component_rescan (comp);

				/* Then do it like for "mod_all" */
				e_cal_recur_ensure_end_dates (comp, TRUE, e_cal_cache_resolve_timezone_simple_cb, cal_cache);

				/* Replace master */
				instances = g_slist_remove (instances, master_comp);
				g_clear_object (&master_comp);
				existing_comp = NULL;

				instances = g_slist_prepend (instances, e_cal_component_clone (comp));

				if (out_new_comp) {
					g_clear_object (&new_comp);
					new_comp = e_cal_component_clone (comp);
				}
				break;
			}

			prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);
			if (prop)
				icalcomponent_remove_property (icalcomp, prop);
			e_cal_component_rescan (comp);

			master_dtstart = icalcomponent_get_dtstart (e_cal_component_get_icalcomponent (master_comp));
			split_icalcomp = e_cal_util_split_at_instance (icalcomp, rid, master_dtstart);
			if (split_icalcomp) {
				rid = icaltime_convert_to_zone (rid, icaltimezone_get_utc_timezone ());
				e_cal_util_remove_instances (e_cal_component_get_icalcomponent (master_comp), rid, mod);
				e_cal_component_rescan (master_comp);
				e_cal_recur_ensure_end_dates (master_comp, TRUE, e_cal_cache_resolve_timezone_simple_cb, cal_cache);

				if (out_new_comp) {
					g_clear_object (&new_comp);
					new_comp = e_cal_component_clone (master_comp);
				}
			}

			if (split_icalcomp) {
				gchar *new_uid;

				new_uid = e_cal_component_gen_uid ();
				icalcomponent_set_uid (split_icalcomp, new_uid);
				g_free (new_uid);

				g_warn_if_fail (e_cal_component_set_icalcomponent (comp, split_icalcomp));

				e_cal_recur_ensure_end_dates (comp, TRUE, e_cal_cache_resolve_timezone_simple_cb, cal_cache);

				success = ecmb_create_object_sync (meta_backend, cal_cache, offline_flag, E_CONFLICT_RESOLUTION_FAIL,
					comp, NULL, NULL, cancellable, error);
			}
		} else {
			/* Replace master */
			instances = g_slist_remove (instances, master_comp);
			g_clear_object (&master_comp);
			existing_comp = NULL;

			instances = g_slist_prepend (instances, e_cal_component_clone (comp));
		}
		break;
	}

	if (success && *offline_flag == E_CACHE_OFFLINE_UNKNOWN) {
		if (e_backend_get_online (E_BACKEND (meta_backend)) &&
		    ecmb_connect_wrapper_sync (meta_backend, cancellable, NULL)) {
			*offline_flag = E_CACHE_IS_ONLINE;
		} else {
			*offline_flag = E_CACHE_IS_OFFLINE;
		}
	}

	if (success && *offline_flag == E_CACHE_IS_ONLINE) {
		success = ecmb_save_component_wrapper_sync (meta_backend, cal_cache, TRUE, conflict_resolution,
			instances, extra, id->uid, &requires_put, cancellable, error);
	}

	if (success && requires_put)
		success = ecmb_put_instances (meta_backend, cal_cache, id->uid, *offline_flag, instances, extra, cancellable, error);

	if (!success) {
		g_clear_object (&old_comp);
		g_clear_object (&new_comp);
	}

	if (out_old_comp)
		*out_old_comp = old_comp;
	if (out_new_comp)
		*out_new_comp = new_comp;

	g_slist_free_full (instances, g_object_unref);
	e_cal_component_free_id (id);
	g_free (extra);

	return success;
}

static void
ecmb_modify_objects_sync (ECalBackendSync *sync_backend,
			  EDataCal *cal,
			  GCancellable *cancellable,
			  const GSList *calobjs,
			  ECalObjModType mod,
			  GSList **out_old_components,
			  GSList **out_new_components,
			  GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	ECacheOfflineFlag offline_flag = E_CACHE_OFFLINE_UNKNOWN;
	EConflictResolution conflict_resolution = E_CONFLICT_RESOLUTION_KEEP_LOCAL;
	icalcomponent_kind backend_kind;
	GSList *link;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (calobjs != NULL);
	g_return_if_fail (out_old_components != NULL);
	g_return_if_fail (out_new_components != NULL);

	if (!e_cal_backend_get_writable (E_CAL_BACKEND (sync_backend))) {
		g_propagate_error (error, e_data_cal_create_error (PermissionDenied, NULL));
		return;
	}

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	backend_kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

	for (link = (GSList *) calobjs; link && success; link = g_slist_next (link)) {
		ECalComponent *comp, *old_comp = NULL, *new_comp = NULL;

		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			break;

		comp = e_cal_component_new_from_string (link->data);
		if (!comp ||
		    !e_cal_component_get_icalcomponent (comp) ||
		    backend_kind != icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			break;
		}

		success = ecmb_modify_object_sync (meta_backend, cal_cache, &offline_flag, conflict_resolution,
			mod, comp, &old_comp, &new_comp, cancellable, error);

		if (success) {
			*out_old_components = g_slist_prepend (*out_old_components, old_comp);
			*out_new_components = g_slist_prepend (*out_new_components, new_comp);
		}

		g_object_unref (comp);
	}

	*out_old_components = g_slist_reverse (*out_old_components);
	*out_new_components = g_slist_reverse (*out_new_components);

	g_object_unref (cal_cache);
}

static gboolean
ecmb_remove_object_sync (ECalMetaBackend *meta_backend,
			 ECalCache *cal_cache,
			 ECacheOfflineFlag *offline_flag,
			 EConflictResolution conflict_resolution,
			 ECalObjModType mod,
			 const gchar *uid,
			 const gchar *rid,
			 ECalComponent **out_old_comp,
			 ECalComponent **out_new_comp,
			 GCancellable *cancellable,
			 GError **error)
{
	struct icaltimetype itt;
	ECalComponent *old_comp = NULL, *new_comp = NULL, *master_comp, *existing_comp = NULL;
	GSList *instances = NULL;
	gboolean success = TRUE;
	GError *local_error = NULL;

	g_return_val_if_fail (uid != NULL, FALSE);

	if (rid && !*rid)
		rid = NULL;

	if ((mod == E_CAL_OBJ_MOD_THIS_AND_PRIOR ||
	    mod == E_CAL_OBJ_MOD_THIS_AND_FUTURE) && !rid) {
		/* Require Recurrence-ID for these types */
		g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
		return FALSE;
	}

	if (!e_cal_cache_get_components_by_uid (cal_cache, uid, &instances, cancellable, &local_error)) {
		if (g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
			g_clear_error (&local_error);
			local_error = e_data_cal_create_error (ObjectNotFound, NULL);
		}

		g_propagate_error (error, local_error);

		return FALSE;
	}

	master_comp = ecmb_find_in_instances (instances, uid, NULL);
	if (rid) {
		/* Set detached instance as the old object */
		existing_comp = ecmb_find_in_instances (instances, uid, rid);
	}

	if (!existing_comp)
		existing_comp = master_comp;

	/* Remember old and new components */
	if (out_old_comp && existing_comp)
		old_comp = e_cal_component_clone (existing_comp);

	if (*offline_flag == E_CACHE_OFFLINE_UNKNOWN) {
		if (e_backend_get_online (E_BACKEND (meta_backend)) &&
		    ecmb_connect_wrapper_sync (meta_backend, cancellable, NULL)) {
			*offline_flag = E_CACHE_IS_ONLINE;
		} else {
			*offline_flag = E_CACHE_IS_OFFLINE;
		}
	}

	switch (mod) {
	case E_CAL_OBJ_MOD_ALL:
		/* Will remove the whole component below */
		break;
	case E_CAL_OBJ_MOD_ONLY_THIS:
	case E_CAL_OBJ_MOD_THIS:
		if (rid) {
			if (existing_comp != master_comp) {
				instances = g_slist_remove (instances, existing_comp);
				g_clear_object (&existing_comp);
			} else if (mod == E_CAL_OBJ_MOD_ONLY_THIS) {
				success = FALSE;
				g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
			} else {
				itt = icaltime_from_string (rid);
				if (!itt.zone) {
					ECalComponentDateTime dt;

					e_cal_component_get_dtstart (master_comp, &dt);
					if (dt.value && dt.tzid) {
						icaltimezone *zone = e_cal_cache_resolve_timezone_simple_cb (dt.tzid, cal_cache);

						if (zone)
							itt = icaltime_convert_to_zone (itt, zone);
					}
					e_cal_component_free_datetime (&dt);

					itt = icaltime_convert_to_zone (itt, icaltimezone_get_utc_timezone ());
				}

				e_cal_util_remove_instances (e_cal_component_get_icalcomponent (master_comp), itt, mod);
			}

			if (success && out_new_comp)
				new_comp = e_cal_component_clone (master_comp);
		} else {
			mod = E_CAL_OBJ_MOD_ALL;
		}
		break;
	case E_CAL_OBJ_MOD_THIS_AND_PRIOR:
	case E_CAL_OBJ_MOD_THIS_AND_FUTURE:
		if (master_comp) {
			time_t fromtt, instancett;
			GSList *link, *previous = instances;

			itt = icaltime_from_string (rid);
			if (!itt.zone) {
				ECalComponentDateTime dt;

				e_cal_component_get_dtstart (master_comp, &dt);
				if (dt.value && dt.tzid) {
					icaltimezone *zone = e_cal_cache_resolve_timezone_simple_cb (dt.tzid, cal_cache);

					if (zone)
						itt = icaltime_convert_to_zone (itt, zone);
				}
				e_cal_component_free_datetime (&dt);

				itt = icaltime_convert_to_zone (itt, icaltimezone_get_utc_timezone ());
			}

			e_cal_util_remove_instances (e_cal_component_get_icalcomponent (master_comp), itt, mod);

			fromtt = icaltime_as_timet (itt);

			/* Remove detached instances */
			for (link = instances; link && fromtt > 0;) {
				ECalComponent *comp = link->data;
				ECalComponentRange range;

				if (!e_cal_component_is_instance (comp)) {
					previous = link;
					link = g_slist_next (link);
					continue;
				}

				e_cal_component_get_recurid (comp, &range);
				if (range.datetime.value)
					instancett = icaltime_as_timet (*range.datetime.value);
				else
					instancett = 0;
				e_cal_component_free_range (&range);

				if (instancett > 0 && (
				    (mod == E_CAL_OBJ_MOD_THIS_AND_PRIOR && instancett <= fromtt) ||
				    (mod == E_CAL_OBJ_MOD_THIS_AND_FUTURE && instancett >= fromtt))) {
					GSList *prev_instances = instances;

					instances = g_slist_remove (instances, comp);
					g_clear_object (&comp);

					/* Restart the lookup */
					if (previous == prev_instances)
						previous = instances;

					link = previous;
				} else {
					previous = link;
					link = g_slist_next (link);
				}
			}
		} else {
			mod = E_CAL_OBJ_MOD_ALL;
		}
		break;
	}

	if (success) {
		gchar *extra = NULL;

		if (!e_cal_cache_get_component_extra (cal_cache, uid, NULL, &extra, cancellable, NULL))
			extra = NULL;

		if (mod == E_CAL_OBJ_MOD_ALL) {
			if (*offline_flag == E_CACHE_IS_ONLINE) {
				success = e_cal_meta_backend_remove_component_sync (meta_backend, conflict_resolution, uid, extra, cancellable, error);
			}

			success = success && ecmb_maybe_remove_from_cache (meta_backend, cal_cache, *offline_flag, uid, cancellable, error);
		} else {
			gboolean requires_put = TRUE;

			if (master_comp) {
				icalcomponent *icalcomp = e_cal_component_get_icalcomponent (master_comp);

				icalcomponent_set_sequence (icalcomp, icalcomponent_get_sequence (icalcomp) + 1);

				e_cal_component_rescan (master_comp);

				/* Set the last modified time on the component */
				itt = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
				e_cal_component_set_last_modified (master_comp, &itt);
			}

			if (*offline_flag == E_CACHE_IS_ONLINE) {
				success = ecmb_save_component_wrapper_sync (meta_backend, cal_cache, TRUE, conflict_resolution,
					instances, extra, uid, &requires_put, cancellable, error);
			}

			if (success && requires_put)
				success = ecmb_put_instances (meta_backend, cal_cache, uid, *offline_flag, instances, extra, cancellable, error);
		}

		g_free (extra);
	}

	if (!success) {
		g_clear_object (&old_comp);
		g_clear_object (&new_comp);
	}

	if (out_old_comp)
		*out_old_comp = old_comp;
	if (out_new_comp)
		*out_new_comp = new_comp;

	g_slist_free_full (instances, g_object_unref);

	return success;
}

static void
ecmb_remove_objects_sync (ECalBackendSync *sync_backend,
			  EDataCal *cal,
			  GCancellable *cancellable,
			  const GSList *ids,
			  ECalObjModType mod,
			  GSList **out_old_components,
			  GSList **out_new_components,
			  GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	ECacheOfflineFlag offline_flag = E_CACHE_OFFLINE_UNKNOWN;
	EConflictResolution conflict_resolution = E_CONFLICT_RESOLUTION_KEEP_LOCAL;
	GSList *link;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (ids != NULL);
	g_return_if_fail (out_old_components != NULL);
	g_return_if_fail (out_new_components != NULL);

	if (!e_cal_backend_get_writable (E_CAL_BACKEND (sync_backend))) {
		g_propagate_error (error, e_data_cal_create_error (PermissionDenied, NULL));
		return;
	}

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	for (link = (GSList *) ids; link && success; link = g_slist_next (link)) {
		ECalComponent *old_comp = NULL, *new_comp = NULL;
		ECalComponentId *id = link->data;

		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			break;

		if (!id) {
			g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			break;
		}

		success = ecmb_remove_object_sync (meta_backend, cal_cache, &offline_flag, conflict_resolution,
			mod, id->uid, id->rid, &old_comp, &new_comp, cancellable, error);

		if (success) {
			*out_old_components = g_slist_prepend (*out_old_components, old_comp);
			*out_new_components = g_slist_prepend (*out_new_components, new_comp);
		}
	}

	*out_old_components = g_slist_reverse (*out_old_components);
	*out_new_components = g_slist_reverse (*out_new_components);

	g_object_unref (cal_cache);
}

static gboolean
ecmb_receive_object_sync (ECalMetaBackend *meta_backend,
			  ECalCache *cal_cache,
			  ECacheOfflineFlag *offline_flag,
			  EConflictResolution conflict_resolution,
			  ECalComponent *comp,
			  icalproperty_method method,
			  GCancellable *cancellable,
			  GError **error)
{
	ESourceRegistry *registry;
	ECalBackend *cal_backend;
	gboolean is_declined, is_in_cache;
	ECalObjModType mod;
	ECalComponentId *id;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	id = e_cal_component_get_id (comp);

	if (!id && method == ICAL_METHOD_PUBLISH) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		e_cal_component_set_uid (comp, new_uid);
		g_free (new_uid);

		id = e_cal_component_get_id (comp);
	}

	if (!id) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		return FALSE;
	}

	cal_backend = E_CAL_BACKEND (meta_backend);
	registry = e_cal_backend_get_registry (cal_backend);

	/* just to check whether component exists in a cache */
	is_in_cache = e_cal_cache_contains (cal_cache, id->uid, NULL, E_CACHE_EXCLUDE_DELETED) ||
		(id->rid && *id->rid && e_cal_cache_contains (cal_cache, id->uid, id->rid, E_CACHE_EXCLUDE_DELETED));

	mod = e_cal_component_is_instance (comp) ? E_CAL_OBJ_MOD_THIS : E_CAL_OBJ_MOD_ALL;

	switch (method) {
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_REQUEST:
	case ICAL_METHOD_REPLY:
		is_declined = e_cal_backend_user_declined (registry, e_cal_component_get_icalcomponent (comp));
		if (is_in_cache) {
			if (!is_declined) {
				success = ecmb_modify_object_sync (meta_backend, cal_cache, offline_flag, conflict_resolution,
					mod, comp, NULL, NULL, cancellable, error);
			} else {
				success = ecmb_remove_object_sync (meta_backend, cal_cache, offline_flag, conflict_resolution,
					mod, id->uid, id->rid, NULL, NULL, cancellable, error);
			}
		} else if (!is_declined) {
			success = ecmb_create_object_sync (meta_backend, cal_cache, offline_flag, conflict_resolution,
				comp, NULL, NULL, cancellable, error);
		}
		break;
	case ICAL_METHOD_CANCEL:
		if (is_in_cache) {
			success = ecmb_remove_object_sync (meta_backend, cal_cache, offline_flag, conflict_resolution,
				E_CAL_OBJ_MOD_THIS, id->uid, id->rid, NULL, NULL, cancellable, error);
		} else {
			g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
		}
		break;

	default:
		g_propagate_error (error, e_data_cal_create_error (UnsupportedMethod, NULL));
		break;
	}

	e_cal_component_free_id (id);

	return success;
}

static void
ecmb_receive_objects_sync (ECalBackendSync *sync_backend,
			   EDataCal *cal,
			   GCancellable *cancellable,
			   const gchar *calobj,
			   GError **error)
{
	ECalMetaBackend *meta_backend;
	ECacheOfflineFlag offline_flag = E_CACHE_OFFLINE_UNKNOWN;
	EConflictResolution conflict_resolution = E_CONFLICT_RESOLUTION_KEEP_LOCAL;
	ECalCache *cal_cache;
	ECalComponent *comp;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	icalproperty_method top_method;
	GSList *comps = NULL, *link;
	gboolean success = TRUE;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (calobj != NULL);

	if (!e_cal_backend_get_writable (E_CAL_BACKEND (sync_backend))) {
		g_propagate_error (error, e_data_cal_create_error (PermissionDenied, NULL));
		return;
	}

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		g_object_unref (cal_cache);
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		for (subcomp = icalcomponent_get_first_component (icalcomp, kind);
		     subcomp && success;
		     subcomp = icalcomponent_get_next_component (icalcomp, kind)) {
			comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (subcomp));

			if (comp)
				comps = g_slist_prepend (comps, comp);
		}
	} else if (icalcomponent_isa (icalcomp) == kind) {
		comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (icalcomp));

		if (comp)
			comps = g_slist_prepend (comps, comp);
	}

	if (!comps) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		icalcomponent_free (icalcomp);
		g_object_unref (cal_cache);
		return;
	}

	comps = g_slist_reverse (comps);

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT)
		ecmb_gather_timezones (meta_backend, E_TIMEZONE_CACHE (cal_cache), icalcomp);

	if (icalcomponent_get_first_property (icalcomp, ICAL_METHOD_PROPERTY))
		top_method = icalcomponent_get_method (icalcomp);
	else
		top_method = ICAL_METHOD_PUBLISH;

	for (link = comps; link && success; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		icalproperty_method method;

		subcomp = e_cal_component_get_icalcomponent (comp);

		if (icalcomponent_get_first_property (subcomp, ICAL_METHOD_PROPERTY)) {
			method = icalcomponent_get_method (subcomp);
		} else {
			method = top_method;
		}

		success = ecmb_receive_object_sync (meta_backend, cal_cache, &offline_flag, conflict_resolution,
			comp, method, cancellable, error);
	}

	g_slist_free_full (comps, g_object_unref);
	icalcomponent_free (icalcomp);
	g_object_unref (cal_cache);
}

static void
ecmb_send_objects_sync (ECalBackendSync *sync_backend,
			EDataCal *cal,
			GCancellable *cancellable,
			const gchar *calobj,
			GSList **out_users,
			gchar **out_modified_calobj,
			GError **error)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (calobj != NULL);
	g_return_if_fail (out_users != NULL);
	g_return_if_fail (out_modified_calobj != NULL);

	*out_users = NULL;
	*out_modified_calobj = g_strdup (calobj);
}

static void
ecmb_add_attachment_uris (ECalComponent *comp,
			  GSList **out_uris)
{
	icalcomponent *icalcomp;
	icalproperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (out_uris != NULL);

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icalcomp != NULL);

	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach = icalproperty_get_attach (prop);

		if (attach && icalattach_get_is_url (attach)) {
			const gchar *url;

			url = icalattach_get_url (attach);
			if (url) {
				gsize buf_size;
				gchar *buf;

				buf_size = strlen (url);
				buf = g_malloc0 (buf_size + 1);

				icalvalue_decode_ical_string (url, buf, buf_size);

				*out_uris = g_slist_prepend (*out_uris, g_strdup (buf));

				g_free (buf);
			}
		}
	}
}

static void
ecmb_get_attachment_uris_sync (ECalBackendSync *sync_backend,
			       EDataCal *cal,
			       GCancellable *cancellable,
			       const gchar *uid,
			       const gchar *rid,
			       GSList **out_uris,
			       GError **error)
{
	ECalMetaBackend *meta_backend;
	ECalCache *cal_cache;
	ECalComponent *comp;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (out_uris != NULL);

	*out_uris = NULL;

	meta_backend = E_CAL_META_BACKEND (sync_backend);
	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	if (!cal_cache) {
		g_propagate_error (error, e_data_cal_create_error (OtherError, NULL));
		return;
	}

	if (rid && *rid) {
		if (e_cal_cache_get_component (cal_cache, uid, rid, &comp, cancellable, &local_error) && comp) {
			ecmb_add_attachment_uris (comp, out_uris);
			g_object_unref (comp);
		}
	} else {
		GSList *comps = NULL, *link;

		if (e_cal_cache_get_components_by_uid (cal_cache, uid, &comps, cancellable, &local_error)) {
			for (link = comps; link; link = g_slist_next (link)) {
				comp = link->data;

				ecmb_add_attachment_uris (comp, out_uris);
			}

			g_slist_free_full (comps, g_object_unref);
		}
	}

	g_object_unref (cal_cache);

	*out_uris = g_slist_reverse (*out_uris);

	if (local_error) {
		if (g_error_matches (local_error, E_CACHE_ERROR, E_CACHE_ERROR_NOT_FOUND)) {
			g_clear_error (&local_error);
			local_error = e_data_cal_create_error (ObjectNotFound, NULL);
		}

		g_propagate_error (error, local_error);
	}
}

static void
ecmb_discard_alarm_sync (ECalBackendSync *sync_backend,
			 EDataCal *cal,
			 GCancellable *cancellable,
			 const gchar *uid,
			 const gchar *rid,
			 const gchar *auid,
			 GError **error)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (uid != NULL);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED,
		e_client_error_to_string (E_CLIENT_ERROR_NOT_SUPPORTED));
}

static void
ecmb_get_timezone_sync (ECalBackendSync *sync_backend,
			EDataCal *cal,
			GCancellable *cancellable,
			const gchar *tzid,
			gchar **tzobject,
			GError **error)
{
	ECalCache *cal_cache;
	gchar *timezone_str = NULL;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));
	g_return_if_fail (tzid != NULL);
	g_return_if_fail (tzobject != NULL);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (sync_backend));
	if (cal_cache) {
		icaltimezone *zone;

		zone = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (cal_cache), tzid);
		if (zone) {
			icalcomponent *icalcomp;

			icalcomp = icaltimezone_get_component (zone);

			if (!icalcomp) {
				local_error = e_data_cal_create_error (InvalidObject, NULL);
			} else {
				timezone_str = icalcomponent_as_ical_string_r (icalcomp);
			}
		}

		g_object_unref (cal_cache);
	}

	if (!local_error && !timezone_str)
		local_error = e_data_cal_create_error (ObjectNotFound, NULL);

	*tzobject = timezone_str;

	if (local_error)
		g_propagate_error (error, local_error);
}

static void
ecmb_add_timezone_sync (ECalBackendSync *sync_backend,
			EDataCal *cal,
			GCancellable *cancellable,
			const gchar *tzobject,
			GError **error)
{
	icalcomponent *tz_comp;

	g_return_if_fail (E_IS_CAL_META_BACKEND (sync_backend));

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return;

	if (!tzobject || !*tzobject) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
		return;
	}

	tz_comp = icalparser_parse_string (tzobject);
	if (!tz_comp ||
	    icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT) {
		g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
	} else {
		ECalCache *cal_cache;
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		tz_comp = NULL;

		cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (sync_backend));
		if (cal_cache) {
			e_timezone_cache_add_timezone (E_TIMEZONE_CACHE (cal_cache), zone);
			icaltimezone_free (zone, 1);
			g_object_unref (cal_cache);
		}
	}

	if (tz_comp)
		icalcomponent_free (tz_comp);
}

static gchar *
ecmb_get_backend_property (ECalBackend *cal_backend,
			   const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_CAL_META_BACKEND (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_REVISION)) {
		ECalCache *cal_cache;
		gchar *revision = NULL;

		cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cal_backend));
		if (cal_cache) {
			revision = e_cache_dup_revision (E_CACHE (cal_cache));
			g_object_unref (cal_cache);
		}

		return revision;
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		ECalComponent *comp;
		gchar *prop_value;

		comp = e_cal_component_new ();

		switch (e_cal_backend_get_kind (cal_backend)) {
		case ICAL_VEVENT_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
			break;
		case ICAL_VTODO_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
			break;
		case ICAL_VJOURNAL_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
			break;
		default:
			g_object_unref (comp);
			return NULL;
		}

		prop_value = e_cal_component_get_as_string (comp);

		g_object_unref (comp);

		return prop_value;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_meta_backend_parent_class)->get_backend_property (cal_backend, prop_name);
}

static void
ecmb_start_view (ECalBackend *cal_backend,
		 EDataCalView *view)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));

	cancellable = ecmb_create_view_cancellable (E_CAL_META_BACKEND (cal_backend), view);

	e_cal_backend_schedule_custom_operation (cal_backend, cancellable,
		ecmb_start_view_thread_func, g_object_ref (view), g_object_unref);

	g_object_unref (cancellable);
}

static void
ecmb_stop_view (ECalBackend *cal_backend,
		EDataCalView *view)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CAL_META_BACKEND (cal_backend));

	cancellable = ecmb_steal_view_cancellable (E_CAL_META_BACKEND (cal_backend), view);
	if (cancellable) {
		g_cancellable_cancel (cancellable);
		g_object_unref (cancellable);
	}
}

static ESourceAuthenticationResult
ecmb_authenticate_sync (EBackend *backend,
			const ENamedParameters *credentials,
			gchar **out_certificate_pem,
			GTlsCertificateFlags *out_certificate_errors,
			GCancellable *cancellable,
			GError **error)
{
	ECalMetaBackend *meta_backend;
	ESourceAuthenticationResult auth_result = E_SOURCE_AUTHENTICATION_UNKNOWN;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (backend), E_SOURCE_AUTHENTICATION_ERROR);

	meta_backend = E_CAL_META_BACKEND (backend);

	if (!e_backend_get_online (E_BACKEND (meta_backend))) {
		g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_REPOSITORY_OFFLINE,
			e_client_error_to_string (E_CLIENT_ERROR_REPOSITORY_OFFLINE));

		return E_SOURCE_AUTHENTICATION_ERROR;
	}

	success = e_cal_meta_backend_connect_sync (meta_backend, credentials, &auth_result,
		out_certificate_pem, out_certificate_errors, cancellable, error);

	if (success) {
		auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else {
		if (auth_result == E_SOURCE_AUTHENTICATION_UNKNOWN)
			auth_result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_mutex_lock (&meta_backend->priv->property_lock);

	e_named_parameters_free (meta_backend->priv->last_credentials);
	if (success)
		meta_backend->priv->last_credentials = e_named_parameters_new_clone (credentials);
	else
		meta_backend->priv->last_credentials = NULL;

	g_mutex_unlock (&meta_backend->priv->property_lock);

	return auth_result;
}

static void
ecmb_schedule_refresh (ECalMetaBackend *meta_backend)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->refresh_cancellable) {
		/* Already refreshing the content */
		g_mutex_unlock (&meta_backend->priv->property_lock);
		return;
	}

	cancellable = g_cancellable_new ();
	meta_backend->priv->refresh_cancellable = g_object_ref (cancellable);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	e_cal_backend_schedule_custom_operation (E_CAL_BACKEND (meta_backend), cancellable,
		ecmb_refresh_thread_func, NULL, NULL);

	g_object_unref (cancellable);
}

static void
ecmb_schedule_source_changed (ECalMetaBackend *meta_backend)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->source_changed_cancellable) {
		/* Already updating */
		g_mutex_unlock (&meta_backend->priv->property_lock);
		return;
	}

	cancellable = g_cancellable_new ();
	meta_backend->priv->source_changed_cancellable = g_object_ref (cancellable);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	e_cal_backend_schedule_custom_operation (E_CAL_BACKEND (meta_backend), cancellable,
		ecmb_source_changed_thread_func, NULL, NULL);

	g_object_unref (cancellable);
}

static void
ecmb_schedule_go_offline (ECalMetaBackend *meta_backend)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));

	g_mutex_lock (&meta_backend->priv->property_lock);

	/* Cancel anything ongoing now, but disconnect in a dedicated thread */
	if (meta_backend->priv->refresh_cancellable)
		g_cancellable_cancel (meta_backend->priv->refresh_cancellable);

	if (meta_backend->priv->source_changed_cancellable)
		g_cancellable_cancel (meta_backend->priv->source_changed_cancellable);

	if (meta_backend->priv->go_offline_cancellable) {
		/* Already going offline */
		g_mutex_unlock (&meta_backend->priv->property_lock);
		return;
	}

	cancellable = g_cancellable_new ();
	meta_backend->priv->go_offline_cancellable = g_object_ref (cancellable);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	e_cal_backend_schedule_custom_operation (E_CAL_BACKEND (meta_backend), cancellable,
		ecmb_go_offline_thread_func, NULL, NULL);

	g_object_unref (cancellable);
}

static void
ecmb_notify_online_cb (GObject *object,
		       GParamSpec *param,
		       gpointer user_data)
{
	ECalMetaBackend *meta_backend = user_data;
	gboolean new_value;

	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));

	new_value = e_backend_get_online (E_BACKEND (meta_backend));
	if (!new_value == !meta_backend->priv->current_online_state)
		return;

	meta_backend->priv->current_online_state = new_value;

	if (new_value)
		ecmb_schedule_refresh (meta_backend);
	else
		ecmb_schedule_go_offline (meta_backend);
}

static void
ecmb_cancel_view_cb (gpointer key,
		     gpointer value,
		     gpointer user_data)
{
	GCancellable *cancellable = value;

	g_return_if_fail (G_IS_CANCELLABLE (cancellable));

	g_cancellable_cancel (cancellable);
}

static void
e_cal_meta_backend_set_property (GObject *object,
				 guint property_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE:
			e_cal_meta_backend_set_cache (
				E_CAL_META_BACKEND (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cal_meta_backend_get_property (GObject *object,
				 guint property_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CACHE:
			g_value_take_object (
				value,
				e_cal_meta_backend_ref_cache (
				E_CAL_META_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_cal_meta_backend_constructed (GObject *object)
{
	ECalMetaBackend *meta_backend = E_CAL_META_BACKEND (object);
	ESource *source;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_parent_class)->constructed (object);

	meta_backend->priv->current_online_state = e_backend_get_online (E_BACKEND (meta_backend));

	source = e_backend_get_source (E_BACKEND (meta_backend));
	meta_backend->priv->source_changed_id = g_signal_connect_swapped (source, "changed",
		G_CALLBACK (ecmb_schedule_source_changed), meta_backend);

	meta_backend->priv->notify_online_id = g_signal_connect (meta_backend, "notify::online",
		G_CALLBACK (ecmb_notify_online_cb), meta_backend);
}

static void
e_cal_meta_backend_dispose (GObject *object)
{
	ECalMetaBackend *meta_backend = E_CAL_META_BACKEND (object);
	ESource *source = e_backend_get_source (E_BACKEND (meta_backend));

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->refresh_timeout_id) {
		if (source)
			e_source_refresh_remove_timeout (source, meta_backend->priv->refresh_timeout_id);
		meta_backend->priv->refresh_timeout_id = 0;
	}

	if (meta_backend->priv->source_changed_id) {
		if (source)
			g_signal_handler_disconnect (source, meta_backend->priv->source_changed_id);
		meta_backend->priv->source_changed_id = 0;
	}

	if (meta_backend->priv->notify_online_id) {
		g_signal_handler_disconnect (meta_backend, meta_backend->priv->notify_online_id);
		meta_backend->priv->notify_online_id = 0;
	}

	g_hash_table_foreach (meta_backend->priv->view_cancellables, ecmb_cancel_view_cb, NULL);

	if (meta_backend->priv->refresh_cancellable) {
		g_cancellable_cancel (meta_backend->priv->refresh_cancellable);
		g_clear_object (&meta_backend->priv->refresh_cancellable);
	}

	if (meta_backend->priv->source_changed_cancellable) {
		g_cancellable_cancel (meta_backend->priv->source_changed_cancellable);
		g_clear_object (&meta_backend->priv->source_changed_cancellable);
	}

	if (meta_backend->priv->go_offline_cancellable) {
		g_cancellable_cancel (meta_backend->priv->go_offline_cancellable);
		g_clear_object (&meta_backend->priv->go_offline_cancellable);
	}

	e_named_parameters_free (meta_backend->priv->last_credentials);
	meta_backend->priv->last_credentials = NULL;

	g_mutex_unlock (&meta_backend->priv->property_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_parent_class)->dispose (object);
}

static void
e_cal_meta_backend_finalize (GObject *object)
{
	ECalMetaBackend *meta_backend = E_CAL_META_BACKEND (object);

	g_clear_object (&meta_backend->priv->cache);
	g_clear_object (&meta_backend->priv->refresh_cancellable);
	g_clear_object (&meta_backend->priv->source_changed_cancellable);
	g_clear_object (&meta_backend->priv->go_offline_cancellable);

	g_mutex_clear (&meta_backend->priv->property_lock);
	g_hash_table_destroy (meta_backend->priv->view_cancellables);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_meta_backend_parent_class)->finalize (object);
}

static void
e_cal_meta_backend_class_init (ECalMetaBackendClass *klass)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *cal_backend_sync_class;

	g_type_class_add_private (klass, sizeof (ECalMetaBackendPrivate));

	klass->get_changes_sync = ecmb_get_changes_sync;

	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->open_sync = ecmb_open_sync;
	cal_backend_sync_class->refresh_sync = ecmb_refresh_sync;
	cal_backend_sync_class->get_object_sync = ecmb_get_object_sync;
	cal_backend_sync_class->get_object_list_sync = ecmb_get_object_list_sync;
	cal_backend_sync_class->get_free_busy_sync = ecmb_get_free_busy_sync;
	cal_backend_sync_class->create_objects_sync = ecmb_create_objects_sync;
	cal_backend_sync_class->modify_objects_sync = ecmb_modify_objects_sync;
	cal_backend_sync_class->remove_objects_sync = ecmb_remove_objects_sync;
	cal_backend_sync_class->receive_objects_sync = ecmb_receive_objects_sync;
	cal_backend_sync_class->send_objects_sync = ecmb_send_objects_sync;
	cal_backend_sync_class->get_attachment_uris_sync = ecmb_get_attachment_uris_sync;
	cal_backend_sync_class->discard_alarm_sync = ecmb_discard_alarm_sync;
	cal_backend_sync_class->get_timezone_sync = ecmb_get_timezone_sync;
	cal_backend_sync_class->add_timezone_sync = ecmb_add_timezone_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->get_backend_property = ecmb_get_backend_property;
	cal_backend_class->start_view = ecmb_start_view;
	cal_backend_class->stop_view = ecmb_stop_view;

	backend_class = E_BACKEND_CLASS (klass);
	backend_class->authenticate_sync = ecmb_authenticate_sync;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = e_cal_meta_backend_set_property;
	object_class->get_property = e_cal_meta_backend_get_property;
	object_class->constructed = e_cal_meta_backend_constructed;
	object_class->dispose = e_cal_meta_backend_dispose;
	object_class->finalize = e_cal_meta_backend_finalize;

	/**
	 * ECalMetaBackend:cache:
	 *
	 * The #ECalCache being used for this meta backend.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_CACHE,
		g_param_spec_object (
			"cache",
			"Cache",
			"Calendar Cache",
			E_TYPE_CAL_CACHE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/* This signal is meant for testing purposes mainly */
	signals[REFRESH_COMPLETED] = g_signal_new (
		"refresh-completed",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 0, G_TYPE_NONE);

	/**
	 * ECalMetaBackend::source-changed
	 *
	 * This signal is emitted whenever the underlying backend #ESource
	 * changes. Unlike the #ESource's 'changed' signal this one is
	 * tight to the #ECalMetaBackend itself and is emitted from
	 * a dedicated thread, thus it doesn't block the main thread.
	 *
	 * Since: 3.26
	 **/
	signals[SOURCE_CHANGED] = g_signal_new (
		"source-changed",
		G_OBJECT_CLASS_TYPE (klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ECalMetaBackendClass, source_changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 0, G_TYPE_NONE);
}

static void
e_cal_meta_backend_init (ECalMetaBackend *meta_backend)
{
	meta_backend->priv = G_TYPE_INSTANCE_GET_PRIVATE (meta_backend, E_TYPE_CAL_META_BACKEND, ECalMetaBackendPrivate);

	g_mutex_init (&meta_backend->priv->property_lock);

	meta_backend->priv->view_cancellables = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
	meta_backend->priv->current_online_state = FALSE;
}

/**
 * e_cal_meta_backend_get_capabilities:
 * @meta_backend: an #ECalMetaBackend
 *
 * Returns: an #ECalBackend::capabilities property to be used by
 *    the descendant in conjunction to the descendant's capabilities
 *    in the result of e_cal_backend_get_backend_property() with
 *    #CLIENT_BACKEND_PROPERTY_CAPABILITIES.
 *
 * Since: 3.26
 **/
const gchar *
e_cal_meta_backend_get_capabilities (ECalMetaBackend *meta_backend)
{
	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);

	return CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED ","
		CAL_STATIC_CAPABILITY_BULK_ADDS ","
		CAL_STATIC_CAPABILITY_BULK_MODIFIES ","
		CAL_STATIC_CAPABILITY_BULK_REMOVES;
}

/**
 * e_cal_meta_backend_set_cache:
 * @meta_backend: an #ECalMetaBackend
 * @cache: an #ECalCache to use
 *
 * Sets the @cache as the cache to be used by the @meta_backend.
 * This should be done as soon as possible, like at the end
 * of the constructed() method, thus the other places can
 * safely use it.
 *
 * Note the @meta_backend adds its own reference to the @cache.
 *
 * Since: 3.26
 **/
void
e_cal_meta_backend_set_cache (ECalMetaBackend *meta_backend,
			      ECalCache *cache)
{
	g_return_if_fail (E_IS_CAL_META_BACKEND (meta_backend));
	g_return_if_fail (E_IS_CAL_CACHE (cache));

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->cache == cache) {
		g_mutex_unlock (&meta_backend->priv->property_lock);
		return;
	}

	g_clear_object (&meta_backend->priv->cache);
	meta_backend->priv->cache = g_object_ref (cache);

	g_mutex_unlock (&meta_backend->priv->property_lock);

	g_object_notify (G_OBJECT (meta_backend), "cache");
}

/**
 * e_cal_meta_backend_ref_cache:
 * @meta_backend: an #ECalMetaBackend
 *
 * Returns: (transfer full): Referenced #ECalCache, which is used by @meta_backend.
 *    Unref it with g_object_unref() when no longer needed.
 *
 * Since: 3.26
 **/
ECalCache *
e_cal_meta_backend_ref_cache (ECalMetaBackend *meta_backend)
{
	ECalCache *cache;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);

	g_mutex_lock (&meta_backend->priv->property_lock);

	if (meta_backend->priv->cache)
		cache = g_object_ref (meta_backend->priv->cache);
	else
		cache = NULL;

	g_mutex_unlock (&meta_backend->priv->property_lock);

	return cache;
}

static gint
sort_master_first_cb (gconstpointer a,
		      gconstpointer b)
{
	icalcomponent *ca, *cb;

	ca = e_cal_component_get_icalcomponent ((ECalComponent *) a);
	cb = e_cal_component_get_icalcomponent ((ECalComponent *) b);

	if (!ca) {
		if (!cb)
			return 0;
		else
			return -1;
	} else if (!cb) {
		return 1;
	}

	return icaltime_compare (icalcomponent_get_recurrenceid (ca), icalcomponent_get_recurrenceid (cb));
}

typedef struct {
	ECalCache *cache;
	gboolean replace_tzid_with_location;
	icalcomponent *vcalendar;
	icalcomponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (icalparameter *param,
                 gpointer user_data)
{
	icaltimezone *tz;
	const gchar *tzid;
	icalcomponent *vtz_comp;
	ForeachTzidData *f_data = user_data;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	tz = icalcomponent_get_timezone (f_data->vcalendar, tzid);
	if (tz)
		return;

	tz = icalcomponent_get_timezone (f_data->icalcomp, tzid);
	if (!tz)
		tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!tz && f_data->cache)
		tz = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (f_data->cache), tzid);
	if (!tz)
		return;

	if (f_data->replace_tzid_with_location) {
		const gchar *location;

		location = icaltimezone_get_location (tz);
		if (location && *location) {
			icalparameter_set_tzid (param, location);
			tzid = location;

			if (icalcomponent_get_timezone (f_data->vcalendar, tzid))
				return;
		}
	}

	vtz_comp = icaltimezone_get_component (tz);

	if (vtz_comp) {
		icalcomponent *clone = icalcomponent_new_clone (vtz_comp);

		if (f_data->replace_tzid_with_location) {
			icalproperty *prop;

			prop = icalcomponent_get_first_property (clone, ICAL_TZID_PROPERTY);
			if (prop) {
				icalproperty_set_tzid (prop, tzid);
			}
		}

		icalcomponent_add_component (f_data->vcalendar, clone);
	}
}

/**
 * e_cal_meta_backend_merge_instances:
 * @meta_backend: an #ECalMetaBackend
 * @instances: (element-type ECalComponent): component instances to merge
 * @replace_tzid_with_location: whether to replace TZID-s with locations
 *
 * Merges all the instances provided in @instances list into one VCALENDAR
 * object, which would eventually contain also all the used timezones.
 * The @instances list should contain the master object and eventually all
 * the detached instances for one component (they all have the same UID).
 *
 * Any TZID property parameters can be replaced with corresponding timezone
 * location, which will not influence the timezone itself.
 *
 * Returns: (transfer full): an #icalcomponent containing a VCALENDAR
 *    component which consists of all the given instances. Free
 *    the returned pointer with icalcomponent_free() when no longer needed.
 *
 * See: e_cal_meta_backend_save_component_sync()
 *
 * Since: 3.26
 **/
icalcomponent *
e_cal_meta_backend_merge_instances (ECalMetaBackend *meta_backend,
				    const GSList *instances,
				    gboolean replace_tzid_with_location)
{
	ForeachTzidData f_data;
	icalcomponent *vcalendar;
	GSList *link, *sorted;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), NULL);
	g_return_val_if_fail (instances != NULL, NULL);

	sorted = g_slist_sort (g_slist_copy ((GSList *) instances), sort_master_first_cb);

	vcalendar = e_cal_util_new_top_level ();

	f_data.cache = e_cal_meta_backend_ref_cache (meta_backend);
	f_data.replace_tzid_with_location = replace_tzid_with_location;
	f_data.vcalendar = vcalendar;

	for (link = sorted; link; link = g_slist_next (link)) {
		ECalComponent *comp = link->data;
		icalcomponent *icalcomp;

		if (!E_IS_CAL_COMPONENT (comp)) {
			g_warn_if_reached ();
			continue;
		}

		icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
		icalcomponent_add_component (vcalendar, icalcomp);

		f_data.icalcomp = icalcomp;

		icalcomponent_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
	}

	g_clear_object (&f_data.cache);
	g_slist_free (sorted);

	return vcalendar;
}

static void
ecmb_remove_all_but_filename_parameter (icalproperty *prop)
{
	icalparameter *param;

	g_return_if_fail (prop != NULL);

	while (param = icalproperty_get_first_parameter (prop, ICAL_ANY_PARAMETER), param) {
		if (icalparameter_isa (param) == ICAL_FILENAME_PARAMETER) {
			param = icalproperty_get_next_parameter (prop, ICAL_ANY_PARAMETER);
			if (!param)
				break;
		}

		icalproperty_remove_parameter_by_ref (prop, param);
	}
}

/**
 * e_cal_meta_backend_inline_local_attachments_sync:
 * @meta_backend: an #ECalMetaBackend
 * @component: an icalcomponent to work with
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes all URL attachments which point to a local file in @component
 * to inline attachments, aka adds the file content into the @component.
 * It also populates FILENAME parameter on the attachment.
 * This is called automatically before e_cal_meta_backend_save_component_sync().
 *
 * The reverse operation is e_cal_meta_backend_store_inline_attachments_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_inline_local_attachments_sync (ECalMetaBackend *meta_backend,
						  icalcomponent *component,
						  GCancellable *cancellable,
						  GError **error)
{
	icalproperty *prop;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (component != NULL, FALSE);

	for (prop = icalcomponent_get_first_property (component, ICAL_ATTACH_PROPERTY);
	     prop && success;
	     prop = icalcomponent_get_next_property (component, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;

		attach = icalproperty_get_attach (prop);
		if (icalattach_get_is_url (attach)) {
			const gchar *url;

			url = icalattach_get_url (attach);
			if (g_str_has_prefix (url, LOCAL_PREFIX)) {
				GFile *file;
				gchar *basename;
				gchar *content;
				gsize len;

				file = g_file_new_for_uri (url);
				basename = g_file_get_basename (file);
				if (g_file_load_contents (file, cancellable, &content, &len, NULL, error)) {
					icalattach *new_attach;
					icalparameter *param;
					gchar *base64;

					base64 = g_base64_encode ((const guchar *) content, len);
					new_attach = icalattach_new_from_data (base64, NULL, NULL);
					g_free (content);
					g_free (base64);

					ecmb_remove_all_but_filename_parameter (prop);

					icalproperty_set_attach (prop, new_attach);
					icalattach_unref (new_attach);

					param = icalparameter_new_value (ICAL_VALUE_BINARY);
					icalproperty_add_parameter (prop, param);

					param = icalparameter_new_encoding (ICAL_ENCODING_BASE64);
					icalproperty_add_parameter (prop, param);

					/* Preserve existing FILENAME parameter */
					if (!icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER)) {
						param = icalparameter_new_filename (basename);
						icalproperty_add_parameter (prop, param);
					}
				} else {
					success = FALSE;
				}

				g_object_unref (file);
				g_free (basename);
			}
		}
	}

	return success;
}

/**
 * e_cal_meta_backend_store_inline_attachments_sync:
 * @meta_backend: an #ECalMetaBackend
 * @component: an icalcomponent to work with
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes all inline attachments to URL attachments in @component, which
 * will point to a local file instead. The function expects FILENAME parameter
 * to be set on the attachment as the file name of it.
 * This is called automatically after e_cal_meta_backend_load_component_sync().
 *
 * The reverse operation is e_cal_meta_backend_inline_local_attachments_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_store_inline_attachments_sync (ECalMetaBackend *meta_backend,
						  icalcomponent *component,
						  GCancellable *cancellable,
						  GError **error)
{
	gint fileindex;
	icalproperty *prop;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (component != NULL, FALSE);

	for (prop = icalcomponent_get_first_property (component, ICAL_ATTACH_PROPERTY), fileindex = 0;
	     prop && success;
	     prop = icalcomponent_get_next_property (component, ICAL_ATTACH_PROPERTY), fileindex++) {
		icalattach *attach;

		attach = icalproperty_get_attach (prop);
		if (!icalattach_get_is_url (attach)) {
			icalparameter *param;
			const gchar *basename;
			gsize len = -1;
			gchar *decoded = NULL;
			gchar *local_filename;

			param = icalproperty_get_first_parameter (prop, ICAL_FILENAME_PARAMETER);
			basename = param ? icalparameter_get_filename (param) : NULL;
			if (!basename || !*basename)
				basename = _("attachment.dat");

			local_filename = e_cal_backend_create_cache_filename (E_CAL_BACKEND (meta_backend), icalcomponent_get_uid (component), basename, fileindex);

			if (local_filename) {
				const gchar *content;

				content = (const gchar *) icalattach_get_data (attach);
				decoded = (gchar *) g_base64_decode (content, &len);

				if (g_file_set_contents (local_filename, decoded, len, error)) {
					icalattach *new_attach;
					gchar *url;

					ecmb_remove_all_but_filename_parameter (prop);

					url = g_filename_to_uri (local_filename, NULL, NULL);
					new_attach = icalattach_new_from_url (url);

					icalproperty_set_attach (prop, new_attach);

					icalattach_unref (new_attach);
					g_free (url);
				} else {
					success = FALSE;
				}

				g_free (decoded);
			}

			g_free (local_filename);
		}
	}

	return success;
}

/**
 * e_cal_meta_backend_connect_sync:
 * @meta_backend: an #ECalMetaBackend
 * @credentials: (nullable): an #ENamedParameters with previously used credentials, or %NULL
 * @out_auth_result: (out): an #ESourceAuthenticationResult with an authentication result
 * @out_certificate_pem: (out) (transfer full): a PEM encoded certificate on failure, or %NULL
 * @out_certificate_errors: (out): a #GTlsCertificateFlags on failure, or 0
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is called always before any operations which requires
 * a connection to the remote side. It can fail with
 * an #E_CLIENT_ERROR_REPOSITORY_OFFLINE error to indicate
 * that the remote side cannot be currently reached. Other
 * errors are propagated to the caller/client side.
 * This method is not called when the backend is not online.
 *
 * The @credentials parameter consists of the previously used credentials.
 * It's always %NULL with the first connection attempt. To get the credentials,
 * just set the @out_auth_result to %E_SOURCE_AUTHENTICATION_REQUIRED for
 * the first time and the function will be called again once the credentials
 * are available. See the documentation of #ESourceAuthenticationResult for
 * other available reasons.
 *
 * The out parameters are passed to e_backend_schedule_credentials_required()
 * and are ignored when the descendant returns %TRUE, aka they are used
 * only if the connection fails. The @out_certificate_pem and @out_certificate_errors
 * should be used together and they can be left untouched if the failure reason was
 * not related to certificate. Use @out_auth_result %E_SOURCE_AUTHENTICATION_UNKNOWN
 * to indicate other error than @credentials error, otherwise the @error is used
 * according to @out_auth_result value.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_connect_sync	(ECalMetaBackend *meta_backend,
				 const ENamedParameters *credentials,
				 ESourceAuthenticationResult *out_auth_result,
				 gchar **out_certificate_pem,
				 GTlsCertificateFlags *out_certificate_errors,
				 GCancellable *cancellable,
				 GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->connect_sync != NULL, FALSE);

	return klass->connect_sync (meta_backend, credentials, out_auth_result, out_certificate_pem, out_certificate_errors, cancellable, error);
}

/**
 * e_cal_meta_backend_disconnect_sync:
 * @meta_backend: an #ECalMetaBackend
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is called when the backend goes into offline mode or
 * when the disconnect is required. The implementation should
 * not report any error when it is called and the @meta_backend
 * is not connected.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_disconnect_sync (ECalMetaBackend *meta_backend,
				    GCancellable *cancellable,
				    GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->disconnect_sync != NULL, FALSE);

	return klass->disconnect_sync (meta_backend, cancellable, error);
}

/**
 * e_cal_meta_backend_get_changes_sync:
 * @meta_backend: an #ECalMetaBackend
 * @last_sync_tag: (nullable): optional sync tag from the last check
 * @out_new_sync_tag: (out) (transfer full): new sync tag to store on success
 * @out_repeat: (out): whether to repeat this call again; default is %FALSE
 * @out_created_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been created since
 *    the last check
 * @out_modified_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been modified since
 *    the last check
 * @out_removed_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which had been removed since
 *    the last check
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gathers the changes since the last check which had been done
 * on the remote side.
 *
 * The @last_sync_tag can be used as a tag of the last check. This can be %NULL,
 * when there was no previous call or when the descendant doesn't store any
 * such tags. The @out_new_sync_tag can be populated with a value to be stored
 * and used the next time.
 *
 * The @out_repeat can be set to %TRUE when the descendant didn't finish
 * read of all the changes. In that case the @meta_backend calls this
 * function again with the @out_new_sync_tag as the @last_sync_tag, but also
 * notifies about the found changes immediately.
 *
 * It is optional to implement this virtual method by the descendant.
 * The default implementation calls e_cal_meta_backend_list_existing_sync()
 * and then compares the list with the current content of the local cache
 * and populates the respective lists appropriately.
 *
 * Each output #GSList should be freed with
 * g_slist_free_full (objects, e_cal_meta_backend_info_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_get_changes_sync (ECalMetaBackend *meta_backend,
				     const gchar *last_sync_tag,
				     gchar **out_new_sync_tag,
				     gboolean *out_repeat,
				     GSList **out_created_objects,
				     GSList **out_modified_objects,
				     GSList **out_removed_objects,
				     GCancellable *cancellable,
				     GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_repeat != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->get_changes_sync != NULL, FALSE);

	return klass->get_changes_sync (meta_backend,
		last_sync_tag,
		out_new_sync_tag,
		out_repeat,
		out_created_objects,
		out_modified_objects,
		out_removed_objects,
		cancellable,
		error);
}

/**
 * e_cal_meta_backend_list_existing_sync:
 * @meta_backend: an #ECalMetaBackend
 * @out_new_sync_tag: (out) (transfer full): optional return location for a new sync tag
 * @out_existing_objects: (out) (element-type ECalMetaBackendInfo) (transfer full):
 *    a #GSList of #ECalMetaBackendInfo object infos which are stored on the remote side
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Used to get list of all existing objects on the remote side. The descendant
 * can optionally provide @out_new_sync_tag, which will be stored if not %NULL.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * The @out_existing_objects #GSList should be freed with
 * g_slist_free_full (objects, e_cal_meta_backend_info_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_list_existing_sync (ECalMetaBackend *meta_backend,
				       gchar **out_new_sync_tag,
				       GSList **out_existing_objects,
				       GCancellable *cancellable,
				       GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->list_existing_sync != NULL, FALSE);

	return klass->list_existing_sync (meta_backend, out_new_sync_tag, out_existing_objects, cancellable, error);
}

/**
 * e_cal_meta_backend_load_component_sync:
 * @meta_backend: an #ECalMetaBackend
 * @uid: a component UID
 * @out_component: (out) (transfer full): a loaded component, as icalcomponent
 * @out_extra: (out) (transfer full): an extra data to store to #ECalCache with this component
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Loads a component from the remote side. Any detached instances should be
 * returned together with the master object. The @out_component can be either
 * a VCALENDAR component, which would contain the master object and all of
 * its detached instances, eventually also used time zones, or the requested
 * component of type VEVENT, VJOURNAL or VTODO.
 *
 * It is mandatory to implement this virtual method by the descendant.
 *
 * The returned @out_component should be freed with icalcomponent_free()
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_load_component_sync (ECalMetaBackend *meta_backend,
					const gchar *uid,
					icalcomponent **out_component,
					gchar **out_extra,
					GCancellable *cancellable,
					GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);
	g_return_val_if_fail (klass->load_component_sync != NULL, FALSE);

	return klass->load_component_sync (meta_backend, uid, out_component, out_extra, cancellable, error);
}

/**
 * e_cal_meta_backend_save_component_sync:
 * @meta_backend: an #ECalMetaBackend
 * @overwrite_existing: %TRUE when can overwrite existing components, %FALSE otherwise
 * @conflict_resolution: one of #EConflictResolution, what to do on conflicts
 * @instances: (element-type ECalComponent): instances of the component to save
 * @extra: (nullable): extra data saved with the components in an #ECalCache
 * @out_new_uid: (out) (transfer full): return location for the UID of the saved component
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Saves one component into the remote side. The @instances contain the master
 * object and all the detached instances of the same component (all have the same UID).
 * When the @overwrite_existing is %TRUE, then the descendant can overwrite an object
 * with the same UID on the remote side (usually used for modify). The @conflict_resolution
 * defines what to do when the remote side had made any changes to the object since
 * the last update.
 *
 * The descendant can use e_cal_meta_backend_merge_instances() to merge
 * the instances into one VCALENDAR component, which will contain also
 * used time zones.
 *
 * The components in @instances have already converted locally stored attachments
 * into inline attachments, thus it's not needed to call
 * e_cal_meta_backend_inline_local_attachments_sync() by the descendant.
 *
 * The @out_new_uid can be populated with a UID of the saved component as the server
 * assigned it to it. This UID, if set, is loaded from the remote side afterwards,
 * also to see whether any changes had been made to the component by the remote side.
 *
 * The descendant can use an #E_CLIENT_ERROR_OUT_OF_SYNC error to indicate that
 * the save failed due to made changes on the remote side, and let the @meta_backend
 * to resolve this conflict based on the @conflict_resolution on its own.
 * The #E_CLIENT_ERROR_OUT_OF_SYNC error should not be used when the descendant
 * is able to resolve the conflicts itself.
 *
 * It is mandatory to implement this virtual method by the writable descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_save_component_sync (ECalMetaBackend *meta_backend,
					gboolean overwrite_existing,
					EConflictResolution conflict_resolution,
					const GSList *instances,
					const gchar *extra,
					gchar **out_new_uid,
					GCancellable *cancellable,
					GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (!klass->save_component_sync) {
		g_propagate_error (error, e_data_cal_create_error (NotSupported, NULL));
		return FALSE;
	}

	return klass->save_component_sync (meta_backend, overwrite_existing, conflict_resolution, instances, extra, out_new_uid, cancellable, error);
}

/**
 * e_cal_meta_backend_remove_component_sync:
 * @meta_backend: an #ECalMetaBackend
 * @conflict_resolution: an #EConflictResolution to use
 * @uid: a component UID
 * @extra: (nullable): extra data being saved with the component in the local cache, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Removes a component from the remote side, with all its detached instances.
 *
 * It is mandatory to implement this virtual method by the writable descendant.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_cal_meta_backend_remove_component_sync (ECalMetaBackend *meta_backend,
					  EConflictResolution conflict_resolution,
					  const gchar *uid,
					  const gchar *extra,
					  GCancellable *cancellable,
					  GError **error)
{
	ECalMetaBackendClass *klass;

	g_return_val_if_fail (E_IS_CAL_META_BACKEND (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	klass = E_CAL_META_BACKEND_GET_CLASS (meta_backend);
	g_return_val_if_fail (klass != NULL, FALSE);

	if (!klass->remove_component_sync) {
		g_propagate_error (error, e_data_cal_create_error (NotSupported, NULL));
		return FALSE;
	}

	return klass->remove_component_sync (meta_backend, conflict_resolution, uid, extra, cancellable, error);
}

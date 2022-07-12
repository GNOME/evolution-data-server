/*
 * Copyright (C) 2014 Red Hat, Inc. (www.redhat.com)
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
 * Authors: Milan Crha <mcrha@redhat.com>
 */

#include "evolution-data-server-config.h"

#include "libedataserver/libedataserver.h"

#include "e-cal-backend-gtasks.h"

#define d(x)

#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

#define GTASKS_DEFAULT_TASKLIST_NAME "@default"
#define X_EVO_GTASKS_POSITION	"X-EVOLUTION-GTASKS-POSITION"

/* Current data version; when doesn't match with the stored,
   then fetches everything again. */
#define GTASKS_DATA_VERSION	2
#define GTASKS_DATA_VERSION_KEY	"gtasks-data-version"

struct _ECalBackendGTasksPrivate {
	EGDataSession *gdata;
	gchar *tasklist_id;
	GRecMutex conn_lock;
	GHashTable *preloaded; /* gchar *uid ~> ECalComponent * */
	gboolean bad_request_for_timed_query;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendGTasks, e_cal_backend_gtasks, E_TYPE_CAL_META_BACKEND)

static gboolean
ecb_gtasks_check_data_version (ECalCache *cal_cache)
{
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	return GTASKS_DATA_VERSION == e_cache_get_key_int (E_CACHE (cal_cache), GTASKS_DATA_VERSION_KEY, NULL);
}

static void
ecb_gtasks_store_data_version (ECalCache *cal_cache)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CACHE (cal_cache));

	if (!e_cache_set_key_int (E_CACHE (cal_cache), GTASKS_DATA_VERSION_KEY, GTASKS_DATA_VERSION, &local_error)) {
		g_warning ("%s: Failed to store data version: %s\n", G_STRFUNC, local_error ? local_error->message : "Unknown error");
	}
}

static void
ecb_gtasks_update_ical_time_property (ICalComponent *icomp,
				      ICalPropertyKind kind,
				      ICalProperty * (* prop_new_func) (ICalTime *v),
				      void (* prop_set_func) (ICalProperty *prop, ICalTime *v),
				      ICalTime *tt)
{
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (icomp, kind);
	if (prop) {
		prop_set_func (prop, tt);
		g_object_unref (prop);
	} else {
		prop = prop_new_func (tt);
		i_cal_component_take_property (icomp, prop);
	}
}

static ECalComponent *
ecb_gtasks_gdata_to_comp (JsonObject *task)
{
	ECalComponent *comp;
	ICalComponent *icomp;
	ICalTime *tt;
	ICalTimezone *utc_zone;
	const gchar *position;
	const gchar *parent;
	const gchar *text;

	g_return_val_if_fail (task, NULL);

	icomp = i_cal_component_new (I_CAL_VTODO_COMPONENT);

	i_cal_component_set_uid (icomp, e_gdata_task_get_id (task));

	utc_zone = i_cal_timezone_get_utc_timezone ();

	tt = i_cal_time_new_from_timet_with_zone (e_gdata_task_get_updated (task), 0, utc_zone);
	if (!tt || !i_cal_time_is_valid_time (tt) || i_cal_time_is_null_time (tt)) {
		g_clear_object (&tt);
		tt = i_cal_time_new_current_with_zone (utc_zone);
	}

	ecb_gtasks_update_ical_time_property (icomp, I_CAL_CREATED_PROPERTY,
		i_cal_property_new_created,
		i_cal_property_set_created,
		tt);

	ecb_gtasks_update_ical_time_property (icomp, I_CAL_LASTMODIFIED_PROPERTY,
		i_cal_property_new_lastmodified,
		i_cal_property_set_lastmodified,
		tt);

	i_cal_component_set_dtstamp (icomp, tt);

	g_clear_object (&tt);

	if (e_gdata_task_get_due (task) > 0) {
		tt = i_cal_time_new_from_timet_with_zone (e_gdata_task_get_due (task), 1, NULL);
		if (tt && i_cal_time_is_valid_time (tt) && !i_cal_time_is_null_time (tt))
			i_cal_component_set_due (icomp, tt);
		g_clear_object (&tt);
	}

	if (e_gdata_task_get_completed (task) > 0) {
		tt = i_cal_time_new_from_timet_with_zone (e_gdata_task_get_completed (task), 0, utc_zone);
		if (tt && i_cal_time_is_valid_time (tt) && !i_cal_time_is_null_time (tt)) {
			ecb_gtasks_update_ical_time_property (icomp, I_CAL_COMPLETED_PROPERTY,
				i_cal_property_new_completed,
				i_cal_property_set_completed,
				tt);
		}
		g_clear_object (&tt);
	}

	text = e_gdata_task_get_title (task);
	if (text && *text)
		i_cal_component_set_summary (icomp, text);

	text = e_gdata_task_get_notes (task);
	if (text && *text)
		i_cal_component_set_description (icomp, text);

	switch (e_gdata_task_get_status (task)) {
	case E_GDATA_TASK_STATUS_COMPLETED:
		i_cal_component_set_status (icomp, I_CAL_STATUS_COMPLETED);
		break;
	case E_GDATA_TASK_STATUS_NEEDS_ACTION:
		i_cal_component_set_status (icomp, I_CAL_STATUS_NEEDSACTION);
		break;
	default:
		break;
	}

	position = e_gdata_task_get_position (task);
	if (position)
		e_cal_util_component_set_x_property (icomp, X_EVO_GTASKS_POSITION, position);

	parent = e_gdata_task_get_parent (task);
	if (parent)
		i_cal_component_take_property (icomp, i_cal_property_new_relatedto (parent));

	comp = e_cal_component_new_from_icalcomponent (icomp);
	g_warn_if_fail (comp != NULL);

	return comp;
}

static JsonBuilder *
ecb_gtasks_comp_to_gdata (ECalComponent *comp,
			  ECalComponent *cached_comp,
			  gboolean ignore_uid,
			  gchar **out_parent,
			  gchar **out_position)
{
	JsonBuilder *task_builder;
	ICalComponent *icomp;
	ICalProperty *prop;
	ICalTime *tt;
	ICalTimezone *utc_zone;
	const gchar *text;
	gchar *position;
	gboolean has_status = FALSE;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	task_builder = json_builder_new_immutable ();
	e_json_begin_object_member (task_builder, NULL);

	if (!ignore_uid) {
		text = i_cal_component_get_uid (icomp);
		if (text && *text)
			e_gdata_task_add_id (task_builder, text);
	}

	utc_zone = i_cal_timezone_get_utc_timezone ();

	tt = i_cal_component_get_due (icomp);
	if (tt && i_cal_time_is_valid_time (tt) && !i_cal_time_is_null_time (tt)) {
		gint64 due;

		due = (gint64) i_cal_time_as_timet_with_zone (tt, utc_zone);
		e_gdata_task_add_due (task_builder, due);
	}
	g_clear_object (&tt);

	prop = i_cal_component_get_first_property (icomp, I_CAL_COMPLETED_PROPERTY);
	if (prop) {
		tt = i_cal_property_get_completed (prop);

		if (tt && i_cal_time_is_valid_time (tt) && !i_cal_time_is_null_time (tt)) {
			gint64 completed;

			completed = (gint64) i_cal_time_as_timet_with_zone (tt, utc_zone);
			e_gdata_task_add_completed (task_builder, completed);
			e_gdata_task_add_status (task_builder, E_GDATA_TASK_STATUS_COMPLETED);
			has_status = TRUE;
		}
		g_clear_object (&tt);
		g_object_unref (prop);
	}

	text = i_cal_component_get_summary (icomp);
	if (text && *text)
		e_gdata_task_add_title (task_builder, text);

	text = i_cal_component_get_description (icomp);
	if (text && *text)
		e_gdata_task_add_notes (task_builder, text);

	if (!has_status) {
		if (i_cal_component_get_status (icomp) == I_CAL_STATUS_COMPLETED)
			e_gdata_task_add_status (task_builder, E_GDATA_TASK_STATUS_COMPLETED);
		else if (i_cal_component_get_status (icomp) == I_CAL_STATUS_NEEDSACTION)
			e_gdata_task_add_status (task_builder, E_GDATA_TASK_STATUS_NEEDS_ACTION);
	}

	/* Position */
	position = e_cal_util_component_dup_x_property (icomp, X_EVO_GTASKS_POSITION);
	if (!position || !*position) {
		g_free (position);
		position = NULL;

		/* If the passed-in component doesn't contain the GData position,
		   then get it from the cached comp */
		if (cached_comp) {
			position = e_cal_util_component_dup_x_property (
				e_cal_component_get_icalcomponent (cached_comp),
				X_EVO_GTASKS_POSITION);
		}
	}

	if (position && *position)
		*out_position = g_steal_pointer (&position);

	g_free (position);

	/* Parent */
	prop = i_cal_component_get_first_property (icomp, I_CAL_RELATEDTO_PROPERTY);
	if (!prop && cached_comp) {
		prop = i_cal_component_get_first_property (
			e_cal_component_get_icalcomponent (cached_comp),
			I_CAL_RELATEDTO_PROPERTY);
	}

	if (prop) {
		const gchar *parent;

		parent = i_cal_property_get_relatedto (prop);

		if (parent && *parent)
			*out_parent = g_strdup (parent);

		g_object_unref (prop);
	}

	e_json_end_object_member (task_builder);

	return task_builder;
}

static gboolean
ecb_gtasks_get_first_tasklist_cb (EGDataSession *gdata,
				  JsonObject *object,
				  gpointer user_data)
{
	JsonObject **ptasklist = user_data;

	*ptasklist = json_object_ref (object);

	return FALSE;
}

static gboolean
ecb_gtasks_prepare_tasklist_locked (ECalBackendGTasks *cbgtasks,
				    GCancellable *cancellable,
				    GError **error)
{
	ESourceResource *resource;
	ESource *source;
	EGDataQuery *query;
	JsonObject *tasklist = NULL;
	gchar *id;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cbgtasks), FALSE);
	g_return_val_if_fail (cbgtasks->priv->gdata != NULL, FALSE);

	source = e_backend_get_source (E_BACKEND (cbgtasks));
	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	id = e_source_resource_dup_identity (resource);

	query = e_gdata_query_new ();
	e_gdata_query_set_max_results (query, 1);

	/* This also verifies that the service can connect to the server with given credentials */
	if (e_gdata_session_tasklists_list_sync (cbgtasks->priv->gdata, query, ecb_gtasks_get_first_tasklist_cb, &tasklist, cancellable, &local_error)) {
		/* If the tasklist ID is not set, then pick the first from the list, most likely the "Default List" */
		if (!id || !*id) {
			g_free (id);
			id = g_strdup (e_gdata_tasklist_get_id (tasklist));
		}
	}

	g_clear_pointer (&tasklist, json_object_unref);
	e_gdata_query_unref (query);

	if (!id || !*id) {
		/* But the tests for change will not work */
		g_free (id);
		id = g_strdup (GTASKS_DEFAULT_TASKLIST_NAME);
	}

	g_clear_pointer (&cbgtasks->priv->tasklist_id, g_free);
	if (g_str_has_prefix (id, "gtasks::"))
		cbgtasks->priv->tasklist_id = g_strdup (id + 8);
	else
		cbgtasks->priv->tasklist_id = g_steal_pointer (&id);

	g_free (id);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gchar *
ecb_gtasks_get_backend_property (ECalBackend *cal_backend,
				 const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cal_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			E_CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			E_CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			E_CAL_STATIC_CAPABILITY_TASK_NO_ALARM,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cal_backend)),
			NULL);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		ESourceAuthentication *authentication;
		ESource *source;
		const gchar *user;

		source = e_backend_get_source (E_BACKEND (cal_backend));
		authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		user = e_source_authentication_get_user (authentication);

		if (!user || !*user || !strchr (user, '@'))
			return NULL;

		return g_strdup (user);
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_gtasks_parent_class)->impl_get_backend_property (cal_backend, prop_name);
}

static gboolean
ecb_gtasks_connect_sync (ECalMetaBackend *meta_backend,
			 const ENamedParameters *credentials,
			 ESourceAuthenticationResult *out_auth_result,
			 gchar **out_certificate_pem,
			 GTlsCertificateFlags *out_certificate_errors,
			 GCancellable *cancellable,
			 GError **error)
{
	ECalBackendGTasks *cbgtasks;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (cbgtasks->priv->gdata &&
	    cbgtasks->priv->tasklist_id) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	g_clear_object (&cbgtasks->priv->gdata);
	g_clear_pointer (&cbgtasks->priv->tasklist_id, g_free);

	cbgtasks->priv->gdata = e_gdata_session_new (e_backend_get_source (E_BACKEND (cbgtasks)));

	e_binding_bind_property (
		cbgtasks, "proxy-resolver",
		cbgtasks->priv->gdata, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	success = ecb_gtasks_prepare_tasklist_locked (cbgtasks, cancellable, &local_error);

	if (success) {
		e_source_set_connection_status (e_backend_get_source (E_BACKEND (cbgtasks)), E_SOURCE_CONNECTION_STATUS_CONNECTED);
	} else {
		e_soup_session_handle_authentication_failure (E_SOUP_SESSION (cbgtasks->priv->gdata), credentials,
			local_error, out_auth_result, out_certificate_pem, out_certificate_errors, error);
	}

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	g_clear_error (&local_error);

	return success;
}

static gboolean
ecb_gtasks_disconnect_sync (ECalMetaBackend *meta_backend,
			    GCancellable *cancellable,
			    GError **error)
{
	ECalBackendGTasks *cbgtasks;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	g_clear_object (&cbgtasks->priv->gdata);
	g_clear_pointer (&cbgtasks->priv->tasklist_id, g_free);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	return TRUE;
}

static gboolean
ecb_gtasks_check_tasklist_changed_locked_sync (ECalBackendGTasks *cbgtasks,
					       const gchar *last_sync_tag,
					       gboolean *out_changed,
					       gint64 *out_tasklist_time,
					       GCancellable *cancellable,
					       GError **error)
{
	JsonObject *tasklist = NULL;
	ECalCache *cal_cache;
	gint64 tasklist_time;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cbgtasks), FALSE);
	g_return_val_if_fail (out_changed != NULL, FALSE);
	g_return_val_if_fail (out_tasklist_time != NULL, FALSE);

	*out_changed = TRUE;
	*out_tasklist_time = 0;

	if (!e_gdata_session_tasklists_get_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, &tasklist, cancellable, error))
		return FALSE;

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbgtasks));
	tasklist_time = e_gdata_tasklist_get_updated (tasklist);

	if (tasklist_time > 0 && last_sync_tag && ecb_gtasks_check_data_version (cal_cache)) {
			*out_changed = tasklist_time != e_json_util_decode_iso8601 (last_sync_tag, 0);
			*out_tasklist_time = tasklist_time;
	}

	g_clear_object (&cal_cache);
	g_clear_pointer (&tasklist, json_object_unref);

	return TRUE;
}

typedef struct _TasklistChangesData {
	ECalCache *cal_cache;
	GSList **out_created_objects; /* ECalMetaBackendInfo * */
	GSList **out_modified_objects; /* ECalMetaBackendInfo * */
	GSList **out_removed_objects; /* ECalMetaBackendInfo * */
	GCancellable *cancellable;
} TasklistChangesData;

static gboolean
ecb_gtasks_list_tasks_cb (EGDataSession *gdata,
			  JsonObject *task,
			  gpointer user_data)
{
	TasklistChangesData *tcd = user_data;
	ECalComponent *cached_comp = NULL;
	const gchar *uid;

	uid = e_gdata_task_get_id (task);

	if (!e_cal_cache_get_component (tcd->cal_cache, uid, NULL, &cached_comp, tcd->cancellable, NULL))
		cached_comp = NULL;

	if (e_gdata_task_get_deleted (task)) {
		*tcd->out_removed_objects = g_slist_prepend (*tcd->out_removed_objects,
			e_cal_meta_backend_info_new (uid, NULL, NULL, NULL));
	} else {
		ECalComponent *new_comp;

		new_comp = ecb_gtasks_gdata_to_comp (task);
		if (new_comp) {
			gchar *revision, *object;

			revision = e_cal_cache_dup_component_revision (tcd->cal_cache, e_cal_component_get_icalcomponent (new_comp));
			object = e_cal_component_get_as_string (new_comp);

			if (cached_comp) {
				ICalTime *cached_tt, *new_tt;

				cached_tt = e_cal_component_get_last_modified (cached_comp);
				new_tt = e_cal_component_get_last_modified (new_comp);

				if (!cached_tt || !new_tt ||
				    i_cal_time_compare (cached_tt, new_tt) != 0) {
					/* Google doesn't store/provide 'created', thus use 'created,
					   as first seen by the backend' */
					if (cached_tt)
						e_cal_component_set_created (new_comp, cached_tt);

					*tcd->out_modified_objects = g_slist_prepend (*tcd->out_modified_objects,
						e_cal_meta_backend_info_new (uid, revision, object, NULL));
				}

				g_clear_object (&cached_tt);
				g_clear_object (&new_tt);
			} else {
				*tcd->out_created_objects = g_slist_prepend (*tcd->out_created_objects,
					e_cal_meta_backend_info_new (uid, revision, object, NULL));
			}

			g_free (revision);
			g_free (object);
		}

		g_clear_object (&new_comp);
	}

	g_clear_object (&cached_comp);

	return TRUE;
}

static gboolean
ecb_gtasks_get_changes_sync (ECalMetaBackend *meta_backend,
			     const gchar *last_sync_tag,
			     gboolean is_repeat,
			     gchar **out_new_sync_tag,
			     gboolean *out_repeat,
			     GSList **out_created_objects, /* ECalMetaBackendInfo * */
			     GSList **out_modified_objects, /* ECalMetaBackendInfo * */
			     GSList **out_removed_objects, /* ECalMetaBackendInfo * */
			     GCancellable *cancellable,
			     GError **error)
{
	ECalBackendGTasks *cbgtasks;
	ECalCache *cal_cache;
	EGDataQuery *query;
	TasklistChangesData tcd;
	gint64 tasklist_time = 0, last_updated;
	gboolean changed = TRUE;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (!ecb_gtasks_check_tasklist_changed_locked_sync (cbgtasks, last_sync_tag, &changed, &tasklist_time, cancellable, error)) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return FALSE;
	}

	if (!changed) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

	if (ecb_gtasks_check_data_version (cal_cache) &&
	    last_sync_tag && !cbgtasks->priv->bad_request_for_timed_query)
		last_updated = e_json_util_decode_iso8601 (last_sync_tag, 0);
	else
		last_updated = 0;

	query = e_gdata_query_new ();
	e_gdata_query_set_max_results (query, 100);
	e_gdata_query_set_show_completed (query, TRUE);
	e_gdata_query_set_show_hidden (query, TRUE);

	if (last_updated > 0) {
		e_gdata_query_set_updated_min (query, last_updated);
		e_gdata_query_set_show_deleted (query, TRUE);
	}

	tcd.cal_cache = cal_cache;
	tcd.out_created_objects = out_created_objects;
	tcd.out_modified_objects = out_modified_objects;
	tcd.out_removed_objects = out_removed_objects;
	tcd.cancellable = cancellable;

	success = e_gdata_session_tasks_list_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, query,
		ecb_gtasks_list_tasks_cb, &tcd, cancellable, &local_error);

	if (last_updated > 0 && (
	    g_error_matches (local_error, G_URI_ERROR, G_URI_ERROR_BAD_QUERY) ||
	    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_BAD_REQUEST))) {
		g_clear_error (&local_error);

		/* To not repeat with broken time format ad infinity;
		   it changes only with updated GData, or change on the server. */
		cbgtasks->priv->bad_request_for_timed_query = TRUE;

		e_gdata_query_unref (query);

		query = e_gdata_query_new ();
		e_gdata_query_set_max_results (query, 100);
		e_gdata_query_set_show_completed (query, TRUE);
		e_gdata_query_set_show_hidden (query, TRUE);

		success = e_gdata_session_tasks_list_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, query,
			ecb_gtasks_list_tasks_cb, &tcd, cancellable, &local_error);
	}

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
	g_clear_pointer (&query, e_gdata_query_unref);

	if (!g_cancellable_is_cancelled (cancellable) && !local_error) {
		*out_new_sync_tag = e_json_util_encode_iso8601 (tasklist_time);
		ecb_gtasks_store_data_version (cal_cache);
	}

	g_clear_object (&cal_cache);

	if (local_error)
		g_propagate_error (error, local_error);

	return success;
}

static gboolean
ecb_gtasks_load_component_sync (ECalMetaBackend *meta_backend,
				const gchar *uid,
				const gchar *extra,
				ICalComponent **out_instances,
				gchar **out_extra,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendGTasks *cbgtasks;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_instances != NULL, FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	/* Only "load" preloaded during save, otherwise fail with an error,
	   because the backend provides objects within get_changes_sync() */

	if (cbgtasks->priv->preloaded) {
		ECalComponent *comp;

		comp = g_hash_table_lookup (cbgtasks->priv->preloaded, uid);
		if (comp) {
			ICalComponent *icomp;

			icomp = e_cal_component_get_icalcomponent (comp);
			if (icomp)
				*out_instances = i_cal_component_clone (icomp);

			g_hash_table_remove (cbgtasks->priv->preloaded, uid);

			if (icomp)
				return TRUE;
		}
	}

	g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));

	return FALSE;
}

static gboolean
ecb_gtasks_save_component_sync (ECalMetaBackend *meta_backend,
				gboolean overwrite_existing,
				EConflictResolution conflict_resolution,
				const GSList *instances, /* ECalComponent * */
				const gchar *extra,
				ECalOperationFlags opflags,
				gchar **out_new_uid,
				gchar **out_new_extra,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendGTasks *cbgtasks;
	ECalCache *cal_cache;
	JsonBuilder *comp_task;
	JsonObject *new_task = NULL;
	ECalComponent *comp, *cached_comp = NULL;
	gchar *parent = NULL, *position = NULL;
	const gchar *uid;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (cal_cache != NULL, FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	if (g_slist_length ((GSList *) instances) != 1) {
		g_propagate_error (error, e_client_error_create (E_CLIENT_ERROR_INVALID_ARG, NULL));
		g_clear_object (&cal_cache);
		return FALSE;
	}

	comp = instances->data;

	if (!comp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		g_clear_object (&cal_cache);
		return FALSE;
	}

	uid = e_cal_component_get_uid (comp);

	if (!overwrite_existing ||
	    !e_cal_cache_get_component (cal_cache, uid, NULL, &cached_comp, cancellable, NULL)) {
		cached_comp = NULL;
	}

	comp_task = ecb_gtasks_comp_to_gdata (comp, cached_comp, !overwrite_existing, &parent, &position);

	g_clear_object (&cached_comp);
	g_clear_object (&cal_cache);

	if (!comp_task) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (overwrite_existing) {
		success = e_gdata_session_tasks_update_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, uid,
			comp_task, &new_task, cancellable, error);

		if (success && (
		    (parent && g_strcmp0 (e_gdata_task_get_parent (new_task), parent) != 0) /*||
		    (position && g_strcmp0 (e_gdata_task_get_position (new_task), position) != 0)*/)) {
			/* TODO: Position is an ordering string, not a task id */
			success = e_gdata_session_tasks_move_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id,
				e_gdata_task_get_id (new_task), parent, NULL /*position*/, cancellable, error);
		}
	} else {
		success = e_gdata_session_tasks_insert_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, comp_task,
			parent, position, &new_task, cancellable, error);
	}

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	g_free (parent);
	g_free (position);
	g_object_unref (comp_task);

	if (!new_task || !success) {
		g_clear_pointer (&new_task, json_object_unref);
		return FALSE;
	}

	comp = ecb_gtasks_gdata_to_comp (new_task);
	g_clear_pointer (&new_task, json_object_unref);

	if (!comp) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	uid = e_cal_component_get_uid (comp);

	if (!uid) {
		g_object_unref (comp);
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	if (cbgtasks->priv->preloaded) {
		*out_new_uid = g_strdup (uid);
		g_hash_table_insert (cbgtasks->priv->preloaded, g_strdup (uid), comp);
	} else {
		g_object_unref (comp);
	}

	return TRUE;
}

static gboolean
ecb_gtasks_remove_component_sync (ECalMetaBackend *meta_backend,
				  EConflictResolution conflict_resolution,
				  const gchar *uid,
				  const gchar *extra,
				  const gchar *object,
				  ECalOperationFlags opflags,
				  GCancellable *cancellable,
				  GError **error)
{
	ECalBackendGTasks *cbgtasks;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	success = e_gdata_session_tasks_delete_sync (cbgtasks->priv->gdata, cbgtasks->priv->tasklist_id, uid, cancellable, error);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	return success;
}

static gboolean
ecb_gtasks_requires_reconnect (ECalMetaBackend *meta_backend)
{
	ESource *source;
	ESourceResource *resource;
	gchar *id;
	ECalBackendGTasks *cbgtasks;
	gboolean changed;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (!cbgtasks->priv->gdata) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	source = e_backend_get_source (E_BACKEND (cbgtasks));
	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	id = e_source_resource_dup_identity (resource);

	changed = id && *id && g_strcmp0 (id, cbgtasks->priv->tasklist_id) != 0 &&
		g_strcmp0 (GTASKS_DEFAULT_TASKLIST_NAME, cbgtasks->priv->tasklist_id) != 0;

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
	g_free (id);

	return changed;
}

static gchar *
ecb_gtasks_dup_component_revision (ECalCache *cal_cache,
				   ICalComponent *icomp,
				   gpointer user_data)
{
	ICalProperty *prop;
	gchar *revision = NULL;

	g_return_val_if_fail (icomp != NULL, NULL);

	prop = i_cal_component_get_first_property (icomp, I_CAL_LASTMODIFIED_PROPERTY);
	if (prop) {
		ICalTime *itt;

		itt = i_cal_property_get_lastmodified (prop);
		revision = i_cal_time_as_ical_string (itt);
		g_clear_object (&itt);
		g_object_unref (prop);
	}

	return revision;
}

static void
e_cal_backend_gtasks_init (ECalBackendGTasks *cbgtasks)
{
	cbgtasks->priv = e_cal_backend_gtasks_get_instance_private (cbgtasks);
	cbgtasks->priv->preloaded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	cbgtasks->priv->bad_request_for_timed_query = FALSE;

	g_rec_mutex_init (&cbgtasks->priv->conn_lock);
}

static void
ecb_gtasks_constructed (GObject *object)
{
	ECalBackendGTasks *cbgtasks = E_CAL_BACKEND_GTASKS (object);
	ECalCache *cal_cache;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->constructed (object);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbgtasks));
	g_return_if_fail (cal_cache != NULL);

	g_signal_connect (cal_cache, "dup-component-revision", G_CALLBACK (ecb_gtasks_dup_component_revision), NULL);

	g_clear_object (&cal_cache);

	/* Set it as always writable, regardless online/offline state */
	e_cal_backend_set_writable (E_CAL_BACKEND (cbgtasks), TRUE);
}

static void
ecb_gtasks_dispose (GObject *object)
{
	ECalBackendGTasks *cbgtasks = E_CAL_BACKEND_GTASKS (object);

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	g_clear_object (&cbgtasks->priv->gdata);
	g_clear_pointer (&cbgtasks->priv->tasklist_id, g_free);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	g_hash_table_destroy (cbgtasks->priv->preloaded);
	cbgtasks->priv->preloaded = NULL;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->dispose (object);
}

static void
ecb_gtasks_finalize (GObject *object)
{
	ECalBackendGTasks *cbgtasks = E_CAL_BACKEND_GTASKS (object);

	g_rec_mutex_clear (&cbgtasks->priv->conn_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->finalize (object);
}

static void
e_cal_backend_gtasks_class_init (ECalBackendGTasksClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *cal_backend_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_gtasks_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_gtasks_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_gtasks_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_gtasks_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_gtasks_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_gtasks_remove_component_sync;
	cal_meta_backend_class->requires_reconnect = ecb_gtasks_requires_reconnect;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_gtasks_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_gtasks_constructed;
	object_class->dispose = ecb_gtasks_dispose;
	object_class->finalize = ecb_gtasks_finalize;
}

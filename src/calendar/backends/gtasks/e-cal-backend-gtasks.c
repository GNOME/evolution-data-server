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

#include <glib/gi18n-lib.h>
#include <gdata/gdata.h>

#include "libedataserver/libedataserver.h"

#include "e-cal-backend-gtasks.h"

#define d(x)

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

#define GTASKS_DEFAULT_TASKLIST_NAME "@default"
#define X_EVO_GTASKS_SELF_LINK	"X-EVOLUTION-GTASKS-SELF-LINK"

/* Current data version; when doesn't match with the stored,
   then fetches everything again. */
#define GTASKS_DATA_VERSION	1
#define GTASKS_DATA_VERSION_KEY	"gtasks-data-version"

struct _ECalBackendGTasksPrivate {
	GDataAuthorizer *authorizer;
	GDataTasksService *service;
	GDataTasksTasklist *tasklist;
	GRecMutex conn_lock;
	GHashTable *preloaded; /* gchar *uid ~> ECalComponent * */
	gboolean bad_request_for_timed_query;
};

G_DEFINE_TYPE (ECalBackendGTasks, e_cal_backend_gtasks, E_TYPE_CAL_META_BACKEND)

static gboolean
ecb_gtasks_check_data_version (ECalCache *cal_cache)
{
#ifdef HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS
	g_return_val_if_fail (E_IS_CAL_CACHE (cal_cache), FALSE);

	return GTASKS_DATA_VERSION == e_cache_get_key_int (E_CACHE (cal_cache), GTASKS_DATA_VERSION_KEY, NULL);
#else
	return TRUE;
#endif
}

static void
ecb_gtasks_store_data_version (ECalCache *cal_cache)
{
#ifdef HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_CACHE (cal_cache));

	if (!e_cache_set_key_int (E_CACHE (cal_cache), GTASKS_DATA_VERSION_KEY, GTASKS_DATA_VERSION, &local_error)) {
		g_warning ("%s: Failed to store data version: %s\n", G_STRFUNC, local_error ? local_error->message : "Unknown error");
	}
#endif
}

static void
ecb_gtasks_update_ical_time_property (icalcomponent *icomp,
				      icalproperty_kind kind,
				      icalproperty * (* prop_new_func) (struct icaltimetype v),
				      void (* prop_set_func) (icalproperty *prop, struct icaltimetype v),
				      struct icaltimetype t)
{
	icalproperty *prop;

	prop = icalcomponent_get_first_property (icomp, kind);
	if (prop) {
		prop_set_func (prop, t);
	} else {
		prop = prop_new_func (t);
		icalcomponent_add_property (icomp, prop);
	}
}

static ECalComponent *
ecb_gtasks_gdata_to_comp (GDataTasksTask *task)
{
	GDataEntry *entry;
	GDataLink *data_link;
	ECalComponent *comp;
	icalcomponent *icomp;
	const gchar *text;
	struct icaltimetype tt;

	g_return_val_if_fail (GDATA_IS_TASKS_TASK (task), NULL);

	entry = GDATA_ENTRY (task);
	icomp = icalcomponent_new (ICAL_VTODO_COMPONENT);

	icalcomponent_set_uid (icomp, gdata_entry_get_id (entry));

	tt = icaltime_from_timet_with_zone (gdata_entry_get_published (entry), 0, icaltimezone_get_utc_timezone ());
	if (!icaltime_is_valid_time (tt) || icaltime_is_null_time (tt))
		tt = icaltime_from_timet_with_zone (gdata_entry_get_updated (entry), 0, icaltimezone_get_utc_timezone ());
	if (!icaltime_is_valid_time (tt) || icaltime_is_null_time (tt))
		tt = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());

	ecb_gtasks_update_ical_time_property (icomp, ICAL_CREATED_PROPERTY,
		icalproperty_new_created,
		icalproperty_set_created,
		tt);

	tt = icaltime_from_timet_with_zone (gdata_entry_get_updated (entry), 0, icaltimezone_get_utc_timezone ());
	if (!icaltime_is_valid_time (tt) || icaltime_is_null_time (tt))
		tt = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomponent_set_dtstamp (icomp, tt);

	ecb_gtasks_update_ical_time_property (icomp, ICAL_LASTMODIFIED_PROPERTY,
		icalproperty_new_lastmodified,
		icalproperty_set_lastmodified,
		tt);

	if (gdata_tasks_task_get_due (task) > 0) {
		tt = icaltime_from_timet_with_zone (gdata_tasks_task_get_due (task), 1, NULL);
		if (icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt))
			icalcomponent_set_due (icomp, tt);
	}

	if (gdata_tasks_task_get_completed (task) > 0) {
		tt = icaltime_from_timet_with_zone (gdata_tasks_task_get_completed (task), 0, icaltimezone_get_utc_timezone ());
		if (icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt))
			ecb_gtasks_update_ical_time_property (icomp, ICAL_COMPLETED_PROPERTY,
				icalproperty_new_completed,
				icalproperty_set_completed,
				tt);
	}

	text = gdata_entry_get_title (entry);
	if (text && *text)
		icalcomponent_set_summary (icomp, text);

	text = gdata_tasks_task_get_notes (task);
	if (text && *text)
		icalcomponent_set_description (icomp, text);

	/* "needsAction" or "completed" */
	text = gdata_tasks_task_get_status (task);
	if (g_strcmp0 (text, "completed") == 0)
		icalcomponent_set_status (icomp, ICAL_STATUS_COMPLETED);
	else if (g_strcmp0 (text, "needsAction") == 0)
		icalcomponent_set_status (icomp, ICAL_STATUS_NEEDSACTION);

	data_link = gdata_entry_look_up_link (entry, GDATA_LINK_SELF);
	if (data_link)
		e_cal_util_set_x_property (icomp, X_EVO_GTASKS_SELF_LINK, gdata_link_get_uri (data_link));

	comp = e_cal_component_new_from_icalcomponent (icomp);
	g_warn_if_fail (comp != NULL);

	return comp;
}

static GDataTasksTask *
ecb_gtasks_comp_to_gdata (ECalComponent *comp,
			  ECalComponent *cached_comp,
			  gboolean ignore_uid)
{
	GDataEntry *entry;
	GDataTasksTask *task;
	icalcomponent *icomp;
	icalproperty *prop;
	const gchar *text;
	gchar *tmp;
	struct icaltimetype tt;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	text = icalcomponent_get_uid (icomp);
	task = gdata_tasks_task_new ((!ignore_uid && text && *text) ? text : NULL);
	entry = GDATA_ENTRY (task);

	tt = icalcomponent_get_due (icomp);
	if (icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt)) {
		gint64 due;

		due = (gint64) icaltime_as_timet_with_zone (tt, icaltimezone_get_utc_timezone ());
		gdata_tasks_task_set_due (task, due);
	}

	prop = icalcomponent_get_first_property (icomp, ICAL_COMPLETED_PROPERTY);
	if (prop) {
		tt = icalproperty_get_completed (prop);

		if (icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt)) {
			gint64 completed;

			completed = (gint64) icaltime_as_timet_with_zone (tt, icaltimezone_get_utc_timezone ());
			gdata_tasks_task_set_completed (task, completed);
			gdata_tasks_task_set_status (task, "completed");
		}
	}

	text = icalcomponent_get_summary (icomp);
	if (text && *text)
		gdata_entry_set_title (entry, text);

	text = icalcomponent_get_description (icomp);
	if (text && *text)
		gdata_tasks_task_set_notes (task, text);

	/* "needsAction" or "completed" */
	if (icalcomponent_get_status (icomp) == ICAL_STATUS_COMPLETED)
		gdata_tasks_task_set_status (task, "completed");
	else if (icalcomponent_get_status (icomp) == ICAL_STATUS_NEEDSACTION)
		gdata_tasks_task_set_status (task, "needsAction");

	tmp = e_cal_util_dup_x_property (icomp, X_EVO_GTASKS_SELF_LINK);
	if (!tmp || !*tmp) {
		g_free (tmp);
		tmp = NULL;

		/* If the passed-in component doesn't contain the libgdata self link,
		   then get it from the cached comp */
		if (cached_comp) {
			tmp = e_cal_util_dup_x_property (
				e_cal_component_get_icalcomponent (cached_comp),
				X_EVO_GTASKS_SELF_LINK);
		}
	}

	if (tmp && *tmp) {
		GDataLink *data_link;

		data_link = gdata_link_new (tmp, GDATA_LINK_SELF);
		gdata_entry_add_link (entry, data_link);
		g_object_unref (data_link);
	}

	g_free (tmp);

	return task;
}

static gboolean
ecb_gtasks_is_authorized_locked (ECalBackendGTasks *cbgtasks)
{
	gboolean res;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cbgtasks), FALSE);

	if (!cbgtasks->priv->service ||
	    !cbgtasks->priv->tasklist)
		return FALSE;

	res = gdata_service_is_authorized (GDATA_SERVICE (cbgtasks->priv->service));

	return res;
}

static gboolean
ecb_gtasks_request_authorization_locked (ECalBackendGTasks *cbgtasks,
					 const ENamedParameters *credentials,
					 GCancellable *cancellable,
					 GError **error)
{
	/* Make sure we have the GDataService configured
	 * before requesting authorization. */

	if (!cbgtasks->priv->authorizer) {
		ESource *source;
		EGDataOAuth2Authorizer *authorizer;

		source = e_backend_get_source (E_BACKEND (cbgtasks));

		/* Only OAuth2 is supported with Google Tasks */
		authorizer = e_gdata_oauth2_authorizer_new (source, GDATA_TYPE_TASKS_SERVICE);
		cbgtasks->priv->authorizer = GDATA_AUTHORIZER (authorizer);
	}

	if (E_IS_GDATA_OAUTH2_AUTHORIZER (cbgtasks->priv->authorizer)) {
		e_gdata_oauth2_authorizer_set_credentials (E_GDATA_OAUTH2_AUTHORIZER (cbgtasks->priv->authorizer), credentials);
	}

	if (!cbgtasks->priv->service) {
		GDataTasksService *tasks_service;

		tasks_service = gdata_tasks_service_new (cbgtasks->priv->authorizer);
		cbgtasks->priv->service = tasks_service;

		e_binding_bind_property (
			cbgtasks, "proxy-resolver",
			cbgtasks->priv->service, "proxy-resolver",
			G_BINDING_SYNC_CREATE);
	}

	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background. */
	if (!GDATA_IS_CLIENT_LOGIN_AUTHORIZER (cbgtasks->priv->authorizer))
		return TRUE;

	/* Otherwise it's up to us to obtain a login secret, but
	   there is currently no way to do it, thus simply fail. */
	return FALSE;
}

static gboolean
ecb_gtasks_prepare_tasklist_locked (ECalBackendGTasks *cbgtasks,
				    GCancellable *cancellable,
				    GError **error)
{
	ESourceResource *resource;
	ESource *source;
	GDataFeed *feed;
	GDataQuery *query;
	gchar *id;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cbgtasks), FALSE);
	g_return_val_if_fail (cbgtasks->priv->service != NULL, FALSE);
	g_return_val_if_fail (gdata_service_is_authorized (GDATA_SERVICE (cbgtasks->priv->service)), FALSE);

	source = e_backend_get_source (E_BACKEND (cbgtasks));
	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	id = e_source_resource_dup_identity (resource);

	query = gdata_query_new_with_limits (NULL, 0, 1);

	/* This also verifies that the service can connect to the server with given credentials */
	feed = gdata_tasks_service_query_all_tasklists (cbgtasks->priv->service, query, cancellable, NULL, NULL, &local_error);
	if (feed) {
		/* If the tasklist ID is not set, then pick the first from the list, most likely the "Default List" */
		if (!id || !*id) {
			GList *entries;

			entries = gdata_feed_get_entries (feed);
			if (entries) {
				GDataEntry *entry = entries->data;
				if (entry) {
					g_free (id);
					id = g_strdup (gdata_entry_get_id (entry));
				}
			}
		}
	}
	g_clear_object (&feed);
	g_object_unref (query);

	if (!id || !*id) {
		/* But the tests for change will not work */
		g_free (id);
		id = g_strdup (GTASKS_DEFAULT_TASKLIST_NAME);
	}

	g_clear_object (&cbgtasks->priv->tasklist);
	if (g_str_has_prefix (id, "gtasks::"))
		cbgtasks->priv->tasklist = gdata_tasks_tasklist_new (id + 8);
	else
		cbgtasks->priv->tasklist = gdata_tasks_tasklist_new (id);

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
			CAL_STATIC_CAPABILITY_NO_THISANDFUTURE,
			CAL_STATIC_CAPABILITY_NO_THISANDPRIOR,
			CAL_STATIC_CAPABILITY_TASK_DATE_ONLY,
			e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (cal_backend)),
			NULL);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
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
	return E_CAL_BACKEND_CLASS (e_cal_backend_gtasks_parent_class)->get_backend_property (cal_backend, prop_name);
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

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (ecb_gtasks_is_authorized_locked (cbgtasks)) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	success = ecb_gtasks_request_authorization_locked (cbgtasks, credentials, cancellable, &local_error);
	if (success)
		success = gdata_authorizer_refresh_authorization (cbgtasks->priv->authorizer, cancellable, &local_error);
	if (success)
		success = ecb_gtasks_prepare_tasklist_locked (cbgtasks, cancellable, &local_error);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	if (!success) {
		if (g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED)) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
			g_clear_error (&local_error);
		} else if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
			   g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
			g_propagate_error (error, local_error);
		} else {
			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, local_error);
		}
	}

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

	g_clear_object (&cbgtasks->priv->service);
	g_clear_object (&cbgtasks->priv->authorizer);
	g_clear_object (&cbgtasks->priv->tasklist);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	return TRUE;
}

static gboolean
ecb_gtasks_check_tasklist_changed_locked_sync (ECalBackendGTasks *cbgtasks,
					       const gchar *last_sync_tag,
					       gboolean *out_changed,
					       gint64 *out_taskslist_time,
					       GCancellable *cancellable,
					       GError **error)
{
	GDataFeed *feed;
	gchar *id = NULL;
	gint64 taskslist_time = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (cbgtasks), FALSE);
	g_return_val_if_fail (out_changed != NULL, FALSE);
	g_return_val_if_fail (out_taskslist_time != NULL, FALSE);

	*out_changed = TRUE;
	*out_taskslist_time = 0;

	g_object_get (cbgtasks->priv->tasklist, "id", &id, NULL);
	g_return_val_if_fail (id != NULL, FALSE);

	/* Check whether the tasklist changed */
	feed = gdata_tasks_service_query_all_tasklists (cbgtasks->priv->service, NULL, cancellable, NULL, NULL, &local_error);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (feed) {
		GList *link;

		for (link = gdata_feed_get_entries (feed); link; link = g_list_next (link)) {
			GDataEntry *entry = link->data;

			if (entry && g_strcmp0 (id, gdata_entry_get_id (entry)) == 0) {
				ECalCache *cal_cache;

				cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbgtasks));
				taskslist_time = gdata_entry_get_updated (entry);

				if (taskslist_time > 0 && last_sync_tag && ecb_gtasks_check_data_version (cal_cache)) {
					GTimeVal stored;

					if (g_time_val_from_iso8601 (last_sync_tag, &stored))
						*out_changed = taskslist_time != stored.tv_sec;
				}

				g_clear_object (&cal_cache);

				break;
			}
		}

		g_clear_object (&feed);
	}

	g_free (id);

	*out_taskslist_time = taskslist_time;

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
	gint64 taskslist_time = 0;
	GTimeVal last_updated;
	GDataFeed *feed;
	GDataTasksQuery *tasks_query;
	gboolean changed = TRUE;
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

	if (!ecb_gtasks_check_tasklist_changed_locked_sync (cbgtasks, last_sync_tag, &changed, &taskslist_time, cancellable, error)) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return FALSE;
	}

	if (!changed) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

	if (!ecb_gtasks_check_data_version (cal_cache) ||
	    !last_sync_tag || cbgtasks->priv->bad_request_for_timed_query ||
	    !g_time_val_from_iso8601 (last_sync_tag, &last_updated)) {
		last_updated.tv_sec = 0;
	}

	tasks_query = gdata_tasks_query_new (NULL);
	gdata_query_set_max_results (GDATA_QUERY (tasks_query), 100);
	gdata_tasks_query_set_show_completed (tasks_query, TRUE);
	gdata_tasks_query_set_show_hidden (tasks_query, TRUE);

	if (last_updated.tv_sec > 0) {
		gdata_query_set_updated_min (GDATA_QUERY (tasks_query), last_updated.tv_sec);
		gdata_tasks_query_set_show_deleted (tasks_query, TRUE);
	}

	feed = gdata_tasks_service_query_tasks (cbgtasks->priv->service, cbgtasks->priv->tasklist,
		GDATA_QUERY (tasks_query), cancellable, NULL, NULL, &local_error);

	if (last_updated.tv_sec > 0 && (
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER) ||
	    g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR))) {
		g_clear_error (&local_error);

		/* To not repeat with broken time format ad infinity;
		   it changes only with updated libgdata, or change on the server. */
		cbgtasks->priv->bad_request_for_timed_query = TRUE;

		gdata_query_set_updated_min (GDATA_QUERY (tasks_query), -1);

		feed = gdata_tasks_service_query_tasks (cbgtasks->priv->service, cbgtasks->priv->tasklist,
			GDATA_QUERY (tasks_query), cancellable, NULL, NULL, &local_error);
	}

#ifdef HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS
	while (feed && !g_cancellable_is_cancelled (cancellable) && !local_error) {
#else
	if (feed) {
#endif
		GList *link;

		for (link = gdata_feed_get_entries (feed); link && !g_cancellable_is_cancelled (cancellable); link = g_list_next (link)) {
			GDataTasksTask *task = link->data;
			ECalComponent *cached_comp = NULL;
			gchar *uid;

			if (!GDATA_IS_TASKS_TASK (task))
				continue;

			uid = g_strdup (gdata_entry_get_id (GDATA_ENTRY (task)));
			if (!uid || !*uid) {
				g_free (uid);
				continue;
			}

			if (!e_cal_cache_get_component (cal_cache, uid, NULL, &cached_comp, cancellable, NULL))
				cached_comp = NULL;

			if (gdata_tasks_task_is_deleted (task)) {
				*out_removed_objects = g_slist_prepend (*out_removed_objects,
					e_cal_meta_backend_info_new (uid, NULL, NULL, NULL));
			} else {
				ECalComponent *new_comp;

				new_comp = ecb_gtasks_gdata_to_comp (task);
				if (new_comp) {
					gchar *revision, *object;

					revision = e_cal_cache_dup_component_revision (cal_cache, e_cal_component_get_icalcomponent (new_comp));
					object = e_cal_component_get_as_string (new_comp);

					if (cached_comp) {
						struct icaltimetype *cached_tt = NULL, *new_tt = NULL;

						e_cal_component_get_last_modified (cached_comp, &cached_tt);
						e_cal_component_get_last_modified (new_comp, &new_tt);

						if (!cached_tt || !new_tt ||
						    icaltime_compare (*cached_tt, *new_tt) != 0) {
							/* Google doesn't store/provide 'created', thus use 'created,
							   as first seen by the backend' */
							if (cached_tt)
								e_cal_component_set_created (new_comp, cached_tt);

							*out_modified_objects = g_slist_prepend (*out_modified_objects,
								e_cal_meta_backend_info_new (uid, revision, object, NULL));
						}

						if (cached_tt)
							e_cal_component_free_icaltimetype (cached_tt);
						if (new_tt)
							e_cal_component_free_icaltimetype (new_tt);
					} else {
						*out_created_objects = g_slist_prepend (*out_created_objects,
							e_cal_meta_backend_info_new (uid, revision, object, NULL));
					}

					g_free (revision);
					g_free (object);
				}

				g_clear_object (&new_comp);
			}

			g_clear_object (&cached_comp);
			g_free (uid);
		}

#ifdef HAVE_LIBGDATA_TASKS_PAGINATION_FUNCTIONS
		if (!gdata_feed_get_entries (feed))
			break;

		gdata_query_next_page (GDATA_QUERY (tasks_query));

		g_clear_object (&feed);

		feed = gdata_tasks_service_query_tasks (cbgtasks->priv->service, cbgtasks->priv->tasklist,
			GDATA_QUERY (tasks_query), cancellable, NULL, NULL, &local_error);
#endif
	}

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
	g_clear_object (&tasks_query);
	g_clear_object (&feed);

	if (!g_cancellable_is_cancelled (cancellable) && !local_error) {
		last_updated.tv_sec = taskslist_time;
		last_updated.tv_usec = 0;

		*out_new_sync_tag = g_time_val_to_iso8601 (&last_updated);

		ecb_gtasks_store_data_version (cal_cache);
	}

	g_clear_object (&cal_cache);

	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
ecb_gtasks_load_component_sync (ECalMetaBackend *meta_backend,
				const gchar *uid,
				const gchar *extra,
				icalcomponent **out_instances,
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
			icalcomponent *icalcomp;

			icalcomp = e_cal_component_get_icalcomponent (comp);
			if (icalcomp)
				*out_instances = icalcomponent_new_clone (icalcomp);

			g_hash_table_remove (cbgtasks->priv->preloaded, uid);

			if (icalcomp)
				return TRUE;
		}
	}

	g_propagate_error (error, EDC_ERROR (ObjectNotFound));

	return FALSE;
}

static gboolean
ecb_gtasks_save_component_sync (ECalMetaBackend *meta_backend,
				gboolean overwrite_existing,
				EConflictResolution conflict_resolution,
				const GSList *instances, /* ECalComponent * */
				const gchar *extra,
				gchar **out_new_uid,
				gchar **out_new_extra,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendGTasks *cbgtasks;
	ECalCache *cal_cache;
	GDataTasksTask *new_task, *comp_task;
	ECalComponent *comp, *cached_comp = NULL;
	icalcomponent *icalcomp;
	const gchar *uid;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (out_new_uid != NULL, FALSE);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (cal_cache != NULL, FALSE);

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	if (g_slist_length ((GSList *) instances) != 1) {
		g_propagate_error (error, EDC_ERROR (InvalidArg));
		g_clear_object (&cal_cache);
		return FALSE;
	}

	comp = instances->data;

	if (!comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		g_clear_object (&cal_cache);
		return FALSE;
	}

	if (!overwrite_existing || !e_cal_cache_get_component (cal_cache,
		icalcomponent_get_uid (e_cal_component_get_icalcomponent (comp)),
		NULL, &cached_comp, cancellable, NULL)) {
		cached_comp = NULL;
	}

	comp_task = ecb_gtasks_comp_to_gdata (comp, cached_comp, !overwrite_existing);

	g_clear_object (&cached_comp);
	g_clear_object (&cal_cache);

	if (!comp_task) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	if (overwrite_existing)
		new_task = gdata_tasks_service_update_task (cbgtasks->priv->service, comp_task, cancellable, error);
	else
		new_task = gdata_tasks_service_insert_task (cbgtasks->priv->service, comp_task, cbgtasks->priv->tasklist, cancellable, error);

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);

	g_object_unref (comp_task);

	if (!new_task)
		return FALSE;

	comp = ecb_gtasks_gdata_to_comp (new_task);
	g_object_unref (new_task);

	if (!comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	icalcomp = e_cal_component_get_icalcomponent (comp);
	uid = icalcomp ? icalcomponent_get_uid (icalcomp) : NULL;

	if (!icalcomp || !uid) {
		g_object_unref (comp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
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
				  GCancellable *cancellable,
				  GError **error)
{
	ECalBackendGTasks *cbgtasks;
	GDataTasksTask *task;
	ECalComponent *cached_comp = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cached_comp = e_cal_component_new_from_string (object);
	if (!cached_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	cbgtasks = E_CAL_BACKEND_GTASKS (meta_backend);

	task = ecb_gtasks_comp_to_gdata (cached_comp, NULL, FALSE);
	if (!task) {
		g_object_unref (cached_comp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));

		return FALSE;
	}

	g_rec_mutex_lock (&cbgtasks->priv->conn_lock);

	/* Ignore protocol errors here, libgdata 0.15.1 results with "Error code 204 when deleting an entry: No Content",
	   while the delete succeeded */
	if (!gdata_tasks_service_delete_task (cbgtasks->priv->service, task, cancellable, &local_error) &&
	    !g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR)) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		g_object_unref (cached_comp);
		g_object_unref (task);
		g_propagate_error (error, local_error);

		return FALSE;
	} else {
		g_clear_error (&local_error);
	}

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
	g_object_unref (cached_comp);
	g_object_unref (task);

	return TRUE;
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

	if (!cbgtasks->priv->tasklist) {
		g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
		return TRUE;
	}

	source = e_backend_get_source (E_BACKEND (cbgtasks));
	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	id = e_source_resource_dup_identity (resource);

	changed = id && *id && g_strcmp0 (id, gdata_entry_get_id (GDATA_ENTRY (cbgtasks->priv->tasklist))) != 0 &&
		g_strcmp0 (GTASKS_DEFAULT_TASKLIST_NAME, gdata_entry_get_id (GDATA_ENTRY (cbgtasks->priv->tasklist))) != 0;

	g_rec_mutex_unlock (&cbgtasks->priv->conn_lock);
	g_free (id);

	return changed;
}

static gchar *
ecb_gtasks_dup_component_revision (ECalCache *cal_cache,
				   icalcomponent *icalcomp,
				   gpointer user_data)
{
	icalproperty *prop;
	gchar *revision = NULL;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	prop = icalcomponent_get_first_property (icalcomp, ICAL_LASTMODIFIED_PROPERTY);
	if (prop) {
		struct icaltimetype itt;

		itt = icalproperty_get_lastmodified (prop);
		revision = icaltime_as_ical_string_r (itt);
	}

	return revision;
}

static void
e_cal_backend_gtasks_init (ECalBackendGTasks *cbgtasks)
{
	cbgtasks->priv = G_TYPE_INSTANCE_GET_PRIVATE (cbgtasks, E_TYPE_CAL_BACKEND_GTASKS, ECalBackendGTasksPrivate);
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

	g_clear_object (&cbgtasks->priv->service);
	g_clear_object (&cbgtasks->priv->authorizer);
	g_clear_object (&cbgtasks->priv->tasklist);

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

	g_type_class_add_private (klass, sizeof (ECalBackendGTasksPrivate));

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_gtasks_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_gtasks_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_gtasks_get_changes_sync;
	cal_meta_backend_class->load_component_sync = ecb_gtasks_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_gtasks_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_gtasks_remove_component_sync;
	cal_meta_backend_class->requires_reconnect = ecb_gtasks_requires_reconnect;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->get_backend_property = ecb_gtasks_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = ecb_gtasks_constructed;
	object_class->dispose = ecb_gtasks_dispose;
	object_class->finalize = ecb_gtasks_finalize;
}

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gdata/gdata.h>

#include "e-gdata-oauth2-authorizer.h"
#include "e-cal-backend-gtasks.h"

#define d(x)

#define E_CAL_BACKEND_GTASKS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_GTASKS, ECalBackendGTasksPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

#define GTASKS_KEY_LAST_UPDATED "last-updated"
#define X_EVO_GTASKS_SELF_LINK	"X-EVOLUTION-GTASKS-SELF-LINK"

#define PROPERTY_LOCK(_gtasks) g_mutex_lock (&(_gtasks)->priv->property_mutex)
#define PROPERTY_UNLOCK(_gtasks) g_mutex_unlock (&(_gtasks)->priv->property_mutex)

struct _ECalBackendGTasksPrivate {
	GDataAuthorizer *authorizer;
	GDataTasksService *service;
	GDataTasksTasklist *tasklist;

	ECalBackendStore *store;
	GCancellable *cancellable;
	GMutex property_mutex;

	guint refresh_id;
};

G_DEFINE_TYPE (ECalBackendGTasks, e_cal_backend_gtasks, E_TYPE_CAL_BACKEND)

static GCancellable *
ecb_gtasks_ref_cancellable (ECalBackendGTasks *gtasks)
{
	GCancellable *cancellable = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks), NULL);

	PROPERTY_LOCK (gtasks);

	if (gtasks->priv->cancellable)
		cancellable = g_object_ref (gtasks->priv->cancellable);

	PROPERTY_UNLOCK (gtasks);

	return cancellable;
}

static void
ecb_gtasks_take_cancellable (ECalBackendGTasks *gtasks,
			     GCancellable *cancellable)
{
	GCancellable *old_cancellable;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks));

	PROPERTY_LOCK (gtasks);

	old_cancellable = gtasks->priv->cancellable;
	gtasks->priv->cancellable = cancellable;

	PROPERTY_UNLOCK (gtasks);

	if (old_cancellable) {
		g_cancellable_cancel (old_cancellable);
		g_clear_object (&old_cancellable);
	}
}

static void
ecb_gtasks_icomp_x_prop_set (icalcomponent *comp,
			     const gchar *key,
			     const gchar *value)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const gchar *str = icalproperty_get_x_name (xprop);

		if (!strcmp (str, key)) {
			if (value) {
				icalproperty_set_value_from_string (xprop, value, "NO");
			} else {
				icalcomponent_remove_property (comp, xprop);
				icalproperty_free (xprop);
			}
			break;
		}

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	if (!xprop && value) {
		xprop = icalproperty_new_x (value);
		icalproperty_set_x_name (xprop, key);
		icalcomponent_add_property (comp, xprop);
	}
}

static gchar *
ecb_gtasks_icomp_x_prop_get (icalcomponent *comp,
			     const gchar *key)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const gchar *str = icalproperty_get_x_name (xprop);

		if (!strcmp (str, key)) {
			break;
		}

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	if (xprop) {
		return icalproperty_get_value_as_string_r (xprop);
	}

	return NULL;
}

/* May hold PROPERTY_LOCK() when calling this */
static ECalComponent *
ecb_gtasks_get_cached_comp (ECalBackendGTasks *gtasks,
			    const gchar *uid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return e_cal_backend_store_get_component (gtasks->priv->store, uid, NULL);
}

static gboolean
ecb_gtasks_is_authorized (ECalBackend *backend)
{
	ECalBackendGTasks *gtasks = E_CAL_BACKEND_GTASKS (backend);

	if (!gtasks->priv->service)
		return FALSE;

	return gdata_service_is_authorized (GDATA_SERVICE (gtasks->priv->service));
}

static void
ecb_gtasks_prepare_tasklist (ECalBackendGTasks *gtasks,
			     GCancellable *cancellable,
			     GError **error)
{
	ESourceResource *resource;
	ESource *source;
	gchar *id;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks));
	g_return_if_fail (ecb_gtasks_is_authorized (E_CAL_BACKEND (gtasks)));

	source = e_backend_get_source (E_BACKEND (gtasks));
	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	id = e_source_resource_dup_identity (resource);

	/* If the tasklist ID is not set, then pick the first from the list, most likely the "Default List" */
	if (!id || !*id) {
		GDataFeed *feed;
		GDataQuery *query;

		query = gdata_query_new_with_limits (NULL, 0, 1);
		feed = gdata_tasks_service_query_all_tasklists (gtasks->priv->service, query, cancellable, NULL, NULL, error);
		if (feed) {
			GList *entries;

			entries = gdata_feed_get_entries (feed);
			if (entries) {
				GDataEntry *entry = entries->data;
				if (entry) {
					g_free (id);
					id = g_strdup (gdata_entry_get_id (entry));
				}
			}
			g_clear_object (&feed);
		}
		g_object_unref (query);
	}

	if (!id || !*id) {
		/* But the tests for change will not work */
		g_free (id);
		id = g_strdup ("@default");
	}

	g_clear_object (&gtasks->priv->tasklist);
	gtasks->priv->tasklist = gdata_tasks_tasklist_new (id);

	g_free (id);
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
		tt = icaltime_from_timet (gdata_tasks_task_get_due (task), 1);
		if (icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt))
			icalcomponent_set_due (icomp, tt);
	}

	if (gdata_tasks_task_get_completed (task) > 0) {
		tt = icaltime_from_timet (gdata_tasks_task_get_completed (task), 1);
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
		ecb_gtasks_icomp_x_prop_set (icomp, X_EVO_GTASKS_SELF_LINK, gdata_link_get_uri (data_link));

	comp = e_cal_component_new_from_icalcomponent (icomp);
	g_warn_if_fail (comp != NULL);

	return comp;
}

static GDataTasksTask *
ecb_gtasks_comp_to_gdata (ECalComponent *comp)
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
	task = gdata_tasks_task_new (text && *text ? text : NULL);
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

	tmp = ecb_gtasks_icomp_x_prop_get (icomp, X_EVO_GTASKS_SELF_LINK);
	if (tmp && *tmp) {
		GDataLink *data_link;

		data_link = gdata_link_new (tmp, GDATA_LINK_SELF);
		gdata_entry_add_link (entry, data_link);
		g_object_unref (data_link);
	}

	g_free (tmp);

	return task;
}

struct EGTasksUpdateData
{
	ECalBackendGTasks *gtasks;
	gint64 taskslist_time;
};

static gpointer
ecb_gtasks_update_thread (gpointer user_data)
{
	struct EGTasksUpdateData *update_data = user_data;
	ECalBackendGTasks *gtasks;
	GTimeVal last_updated;
	GDataFeed *feed;
	GDataTasksQuery *tasks_query;
	const gchar *key;
	GCancellable *cancellable;
	GError *local_error = NULL;

	g_return_val_if_fail (update_data != NULL, NULL);

	gtasks = update_data->gtasks;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks), NULL);

	if (!ecb_gtasks_is_authorized (E_CAL_BACKEND (gtasks))) {
		g_clear_object (&gtasks);
		g_free (update_data);
		return NULL;
	}

	PROPERTY_LOCK (gtasks);

	key = e_cal_backend_store_get_key_value (gtasks->priv->store, GTASKS_KEY_LAST_UPDATED);
	if (!key || !g_time_val_from_iso8601 (key, &last_updated))
		last_updated.tv_sec = 0;

	PROPERTY_UNLOCK (gtasks);

	cancellable = ecb_gtasks_ref_cancellable (gtasks);

	tasks_query = gdata_tasks_query_new (NULL);
	gdata_query_set_start_index (GDATA_QUERY (tasks_query), 0);
	gdata_query_set_max_results (GDATA_QUERY (tasks_query), G_MAXINT);
	gdata_tasks_query_set_show_completed (tasks_query, TRUE);
	gdata_tasks_query_set_show_hidden (tasks_query, TRUE);

	if (last_updated.tv_sec > 0) {
		gdata_query_set_updated_min (GDATA_QUERY (tasks_query), last_updated.tv_sec);
		gdata_tasks_query_set_show_deleted (tasks_query, TRUE);
	}

	feed = gdata_tasks_service_query_tasks (gtasks->priv->service, gtasks->priv->tasklist,
		GDATA_QUERY (tasks_query), cancellable, NULL, NULL, &local_error);
	if (feed) {
		GList *link;
		const gchar *uid;

		PROPERTY_LOCK (gtasks);

		e_cal_backend_store_freeze_changes (gtasks->priv->store);

		for (link = gdata_feed_get_entries (feed); link; link = g_list_next (link)) {
			GDataTasksTask *task = link->data;
			ECalComponent *cached_comp;

			if (!GDATA_IS_TASKS_TASK (task))
				continue;

			uid = gdata_entry_get_id (GDATA_ENTRY (task));
			if (!uid || !*uid)
				continue;

			cached_comp = ecb_gtasks_get_cached_comp (gtasks, uid);

			if (gdata_tasks_task_is_deleted (task)) {
				ECalComponentId id;

				id.uid = (gchar *) uid;
				id.rid = NULL;

				e_cal_backend_notify_component_removed ((ECalBackend *) gtasks, &id, cached_comp, NULL);
				if (cached_comp)
					e_cal_backend_store_remove_component (gtasks->priv->store, uid, NULL);
			} else {
				ECalComponent *new_comp;

				new_comp = ecb_gtasks_gdata_to_comp (task);
				if (new_comp) {
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

							e_cal_backend_store_put_component (gtasks->priv->store, new_comp);
							e_cal_backend_notify_component_modified ((ECalBackend *) gtasks, cached_comp, new_comp);
						}

						if (cached_tt)
							e_cal_component_free_icaltimetype (cached_tt);
						if (new_tt)
							e_cal_component_free_icaltimetype (new_tt);
					} else {
						e_cal_backend_store_put_component (gtasks->priv->store, new_comp);
						e_cal_backend_notify_component_created ((ECalBackend *) gtasks, new_comp);
					}
				}

				g_clear_object (&new_comp);
			}

			g_clear_object (&cached_comp);
		}

		e_cal_backend_store_thaw_changes (gtasks->priv->store);

		PROPERTY_UNLOCK (gtasks);
	}

	g_clear_object (&tasks_query);
	g_clear_object (&feed);

	if (!g_cancellable_is_cancelled (cancellable) && !local_error) {
		gchar *strtm;

		PROPERTY_LOCK (gtasks);

		last_updated.tv_sec = update_data->taskslist_time;
		last_updated.tv_usec = 0;

		strtm = g_time_val_to_iso8601 (&last_updated);
		e_cal_backend_store_put_key_value (gtasks->priv->store, GTASKS_KEY_LAST_UPDATED, strtm);
		g_free (strtm);

		PROPERTY_UNLOCK (gtasks);
	}

	g_clear_object (&cancellable);
	g_clear_object (&gtasks);
	g_clear_error (&local_error);
	g_free (update_data);

	return NULL;
}

static void
ecb_gtasks_start_update (ECalBackendGTasks *gtasks)
{
	GDataFeed *feed;
	GCancellable *cancellable;
	GError *local_error = NULL;
	gchar *id = NULL;
	gint64 taskslist_time = 0;
	gboolean changed = TRUE;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks));

	if (!ecb_gtasks_is_authorized ((ECalBackend *) gtasks))
		return;

	cancellable = ecb_gtasks_ref_cancellable (gtasks);
	g_return_if_fail (cancellable != NULL);

	g_object_get (gtasks->priv->tasklist, "id", &id, NULL);
	g_return_if_fail (id != NULL);

	/* Check whether the tasklist changed */
	feed = gdata_tasks_service_query_all_tasklists (gtasks->priv->service, NULL, cancellable, NULL, NULL, &local_error);
	if (feed) {
		GList *link;

		for (link = gdata_feed_get_entries (feed); link; link = g_list_next (link)) {
			GDataEntry *entry = link->data;

			if (entry && g_strcmp0 (id, gdata_entry_get_id (entry)) == 0) {
				taskslist_time = gdata_entry_get_updated (entry);

				if (taskslist_time > 0) {
					GTimeVal stored;
					const gchar *key;

					PROPERTY_LOCK (gtasks);

					key = e_cal_backend_store_get_key_value (gtasks->priv->store, GTASKS_KEY_LAST_UPDATED);
					if (key && g_time_val_from_iso8601 (key, &stored))
						changed = taskslist_time != stored.tv_sec;

					PROPERTY_UNLOCK (gtasks);
				}

				break;
			}
		}

		g_clear_object (&feed);
	}

	if (changed && !g_cancellable_is_cancelled (cancellable)) {
		GThread *thread;
		struct EGTasksUpdateData *data;

		data = g_new0 (struct EGTasksUpdateData, 1);
		data->gtasks = g_object_ref (gtasks);
		data->taskslist_time = taskslist_time;

		thread = g_thread_new (NULL, ecb_gtasks_update_thread, data);
		g_thread_unref (thread);
	}

	if (local_error) {
		g_debug ("%s: Failed to get all tasklists: %s", G_STRFUNC, local_error->message);
		g_clear_error (&local_error);
	}

	g_clear_object (&cancellable);
	g_free (id);
}

static void
ecb_gtasks_time_to_refresh_data_cb (ESource *source,
				    gpointer user_data)
{
	ECalBackendGTasks *gtasks = user_data;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (gtasks));

	if (!ecb_gtasks_is_authorized (E_CAL_BACKEND (gtasks)) ||
	    !e_backend_get_online (E_BACKEND (gtasks))) {
		return;
	}

	ecb_gtasks_start_update (gtasks);
}

static gboolean
ecb_gtasks_request_authorization (ECalBackend *backend,
				  GCancellable *cancellable,
				  GError **error)
{
	ECalBackendGTasks *gtasks = E_CAL_BACKEND_GTASKS (backend);

	/* Make sure we have the GDataService configured
	 * before requesting authorization. */

	if (!gtasks->priv->authorizer) {
		ESource *source;
		ESourceAuthentication *extension;
		EGDataOAuth2Authorizer *authorizer;
		const gchar *extension_name;
		gchar *method;

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		source = e_backend_get_source (E_BACKEND (backend));
		extension = e_source_get_extension (source, extension_name);
		method = e_source_authentication_dup_method (extension);

		/* Only OAuth2 is supported with Google Tasks */
		authorizer = e_gdata_oauth2_authorizer_new (source);
		gtasks->priv->authorizer = GDATA_AUTHORIZER (authorizer);

		g_free (method);
	}

	if (!gtasks->priv->service) {
		GDataTasksService *tasks_service;

		tasks_service = gdata_tasks_service_new (gtasks->priv->authorizer);
		gtasks->priv->service = tasks_service;

		g_object_bind_property (
			backend, "proxy-resolver",
			gtasks->priv->service, "proxy-resolver",
			G_BINDING_SYNC_CREATE);
	}

	/* If we're using OAuth tokens, then as far as the backend
	 * is concerned it's always authorized.  The GDataAuthorizer
	 * will take care of everything in the background. */
	if (!GDATA_IS_CLIENT_LOGIN_AUTHORIZER (gtasks->priv->authorizer))
		return TRUE;

	/* Otherwise it's up to us to obtain a login secret. */
	return e_backend_authenticate_sync (
		E_BACKEND (backend),
		E_SOURCE_AUTHENTICATOR (backend),
		cancellable, error);
}

static gchar *
ecb_gtasks_get_backend_property (ECalBackend *backend,
				 const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_GTASKS (backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		GString *caps;

		caps = g_string_new (
			CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
			CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
			CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);

		return g_string_free (caps, FALSE);

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		ESourceAuthentication *authentication;
		ESource *source;
		const gchar *user;

		source = e_backend_get_source (E_BACKEND (backend));
		authentication = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		user = e_source_authentication_get_user (authentication);

		if (!user || !*user || !strchr (user, '@'))
			return NULL;

		return g_strdup (user);

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		ECalComponent *comp;
		gchar *prop_value;

		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);

		prop_value = e_cal_component_get_as_string (comp);

		g_object_unref (comp);

		return prop_value;
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_gtasks_parent_class)->get_backend_property (backend, prop_name);
}

static void
ecb_gtasks_open (ECalBackend *backend,
		 EDataCal *cal,
		 guint32 opid,
		 GCancellable *cancellable,
		 gboolean only_if_exists)
{
	ECalBackendGTasks *gtasks;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	if (ecb_gtasks_is_authorized (backend)) {
		e_data_cal_respond_open (cal, opid, NULL);
		return;
	}

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	e_cal_backend_set_writable (backend, FALSE);

	ecb_gtasks_take_cancellable (gtasks, g_cancellable_new ());

	if (e_backend_get_online (E_BACKEND (backend))) {
		gboolean success;

		success = ecb_gtasks_request_authorization (backend, cancellable, &local_error);
		if (success)
			success = gdata_authorizer_refresh_authorization (gtasks->priv->authorizer, cancellable, &local_error);

		if (success) {
			e_cal_backend_set_writable (backend, TRUE);

			ecb_gtasks_prepare_tasklist (gtasks, cancellable, &local_error);
			if (!local_error)
				ecb_gtasks_start_update (gtasks);
		}
	}

	e_data_cal_respond_open (cal, opid, local_error);
}

static void
ecb_gtasks_refresh (ECalBackend *backend,
		    EDataCal *cal,
		    guint32 opid,
		    GCancellable *cancellable)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	if (!ecb_gtasks_is_authorized (backend) ||
	    !e_backend_get_online (E_BACKEND (backend))) {
		e_data_cal_respond_refresh (cal, opid, EDC_ERROR (RepositoryOffline));
		return;
	}

	ecb_gtasks_start_update (E_CAL_BACKEND_GTASKS (backend));

	e_data_cal_respond_refresh (cal, opid, NULL);
}

static void
ecb_gtasks_get_object (ECalBackend *backend,
		       EDataCal *cal,
		       guint32 opid,
		       GCancellable *cancellable,
		       const gchar *uid,
		       const gchar *rid)
{
	ECalBackendGTasks *gtasks;
	ECalComponent *cached_comp;
	gchar *comp_str = NULL;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	PROPERTY_LOCK (gtasks);

	cached_comp = ecb_gtasks_get_cached_comp (gtasks, uid);
	if (cached_comp)
		comp_str = e_cal_component_get_as_string (cached_comp);
	else
		local_error = EDC_ERROR (ObjectNotFound);

	PROPERTY_UNLOCK (gtasks);

	e_data_cal_respond_get_object (cal, opid, local_error, comp_str);

	g_clear_object (&cached_comp);
	g_free (comp_str);
}

static void
ecb_gtasks_get_object_list (ECalBackend *backend,
			    EDataCal *cal,
			    guint32 opid,
			    GCancellable *cancellable,
			    const gchar *sexp_str)
{
	ECalBackendGTasks *gtasks;
	ECalBackendSExp *sexp;
	ETimezoneCache *cache;
	gboolean do_search;
	GSList *list, *iter, *calobjs = NULL;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	sexp = e_cal_backend_sexp_new (sexp_str);
	if (sexp == NULL) {
		e_data_cal_respond_get_object_list (cal, opid, EDC_ERROR (InvalidQuery), NULL);
		return;
	}

	do_search = !g_str_equal (sexp_str, "#t");
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);

	cache = E_TIMEZONE_CACHE (backend);

	PROPERTY_LOCK (gtasks);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (gtasks->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (gtasks->priv->store);

	PROPERTY_UNLOCK (gtasks);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search || e_cal_backend_sexp_match_comp (sexp, comp, cache)) {
			calobjs = g_slist_prepend (calobjs, e_cal_component_get_as_string (comp));
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);

	e_data_cal_respond_get_object_list (cal, opid, NULL, calobjs);

	g_slist_foreach (calobjs, (GFunc) g_free, NULL);
	g_slist_free (calobjs);
}

static void
ecb_gtasks_get_free_busy (ECalBackend *backend,
			  EDataCal *cal,
			  guint32 opid,
			  GCancellable *cancellable,
			  const GSList *users,
			  time_t start,
			  time_t end)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_data_cal_respond_get_free_busy (cal, opid, EDC_ERROR (NotSupported));
}

static void
ecb_gtasks_create_objects (ECalBackend *backend,
			   EDataCal *cal,
			   guint32 opid,
			   GCancellable *cancellable,
			   const GSList *calobjs)
{
	ECalBackendGTasks *gtasks;
	GSList *new_uids = NULL, *new_calcomps = NULL;
	const GSList *link;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	if (!ecb_gtasks_is_authorized (backend) ||
	    !e_backend_get_online (E_BACKEND (backend))) {
		e_data_cal_respond_create_objects (cal, opid, EDC_ERROR (RepositoryOffline), NULL, NULL);
		return;
	}

	for (link = calobjs; link && !local_error; link = link->next) {
		const gchar *icalstr = link->data;
		ECalComponent *comp;
		icalcomponent *icomp;
		const gchar *uid;
		GDataTasksTask *new_task, *comp_task;

		if (!icalstr) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		comp = e_cal_component_new_from_string (icalstr);
		if (comp == NULL) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		icomp = e_cal_component_get_icalcomponent (comp);
		if (!icomp) {
			g_object_unref (comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		uid = icalcomponent_get_uid (icomp);
		if (uid) {
			PROPERTY_LOCK (gtasks);

			if (e_cal_backend_store_has_component (gtasks->priv->store, uid, NULL)) {
				PROPERTY_UNLOCK (gtasks);
				g_object_unref (comp);
				local_error = EDC_ERROR (ObjectIdAlreadyExists);
				break;
			}

			PROPERTY_UNLOCK (gtasks);

			icalcomponent_set_uid (icomp, "");
		}

		comp_task = ecb_gtasks_comp_to_gdata (comp);
		if (!comp_task) {
			g_object_unref (comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		new_task = gdata_tasks_service_insert_task (gtasks->priv->service, comp_task, gtasks->priv->tasklist, cancellable, &local_error);

		g_object_unref (comp_task);
		g_object_unref (comp);

		if (!new_task)
			break;

		comp = ecb_gtasks_gdata_to_comp (new_task);
		g_object_unref (new_task);

		if (!comp) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		icomp = e_cal_component_get_icalcomponent (comp);
		uid = icalcomponent_get_uid (icomp);

		if (!uid) {
			g_object_unref (comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		PROPERTY_LOCK (gtasks);
		e_cal_backend_store_put_component (gtasks->priv->store, comp);
		PROPERTY_UNLOCK (gtasks);

		e_cal_backend_notify_component_created (backend, comp);

		new_uids = g_slist_prepend (new_uids, g_strdup (uid));
		new_calcomps = g_slist_prepend (new_calcomps, comp);
	}

	new_uids = g_slist_reverse (new_uids);
	new_calcomps = g_slist_reverse (new_calcomps);

	e_data_cal_respond_create_objects (cal, opid, local_error, new_uids, new_calcomps);

	g_slist_free_full (new_uids, g_free);
	e_util_free_nullable_object_slist (new_calcomps);
}

static void
ecb_gtasks_modify_objects (ECalBackend *backend,
			   EDataCal *cal,
			   guint32 opid,
			   GCancellable *cancellable,
			   const GSList *calobjs,
			   ECalObjModType mod)
{
	ECalBackendGTasks *gtasks;
	GSList *old_calcomps = NULL, *new_calcomps = NULL;
	const GSList *link;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	if (!ecb_gtasks_is_authorized (backend) ||
	    !e_backend_get_online (E_BACKEND (backend))) {
		e_data_cal_respond_modify_objects (cal, opid, EDC_ERROR (RepositoryOffline), NULL, NULL);
		return;
	}

	for (link = calobjs; link && !local_error; link = link->next) {
		const gchar *icalstr = link->data;
		ECalComponent *comp, *cached_comp;
		icalcomponent *icomp;
		const gchar *uid;
		GDataTasksTask *new_task, *comp_task;

		if (!icalstr) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		comp = e_cal_component_new_from_string (icalstr);
		if (comp == NULL) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		icomp = e_cal_component_get_icalcomponent (comp);
		if (!icomp) {
			g_object_unref (comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		uid = icalcomponent_get_uid (icomp);
		if (!uid) {
			g_object_unref (comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		PROPERTY_LOCK (gtasks);

		cached_comp = ecb_gtasks_get_cached_comp (gtasks, uid);

		PROPERTY_UNLOCK (gtasks);

		if (!cached_comp) {
			g_object_unref (comp);
			local_error = EDC_ERROR (ObjectNotFound);
			break;
		}

		comp_task = ecb_gtasks_comp_to_gdata (comp);
		g_object_unref (comp);

		if (!comp_task) {
			g_object_unref (cached_comp);
			local_error = EDC_ERROR (ObjectNotFound);
			break;
		}

		new_task = gdata_tasks_service_update_task (gtasks->priv->service, comp_task, cancellable, &local_error);
		g_object_unref (comp_task);

		if (!new_task) {
			g_object_unref (cached_comp);
			break;
		}

		comp = ecb_gtasks_gdata_to_comp (new_task);
		g_object_unref (new_task);

		PROPERTY_LOCK (gtasks);
		e_cal_backend_store_put_component (gtasks->priv->store, comp);
		PROPERTY_UNLOCK (gtasks);

		e_cal_backend_notify_component_modified (backend, cached_comp, comp);

		old_calcomps = g_slist_prepend (old_calcomps, cached_comp);
		new_calcomps = g_slist_prepend (new_calcomps, comp);
	}

	old_calcomps = g_slist_reverse (old_calcomps);
	new_calcomps = g_slist_reverse (new_calcomps);

	e_data_cal_respond_modify_objects (cal, opid, local_error, old_calcomps, new_calcomps);

	e_util_free_nullable_object_slist (old_calcomps);
	e_util_free_nullable_object_slist (new_calcomps);
}

static void
ecb_gtasks_remove_objects (ECalBackend *backend,
			   EDataCal *cal,
			   guint32 opid,
			   GCancellable *cancellable,
			   const GSList *ids,
			   ECalObjModType mod)
{
	ECalBackendGTasks *gtasks;
	GSList *old_calcomps = NULL, *removed_ids = NULL;
	const GSList *link;
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	if (!ecb_gtasks_is_authorized (backend) ||
	    !e_backend_get_online (E_BACKEND (backend))) {
		e_data_cal_respond_remove_objects (cal, opid, EDC_ERROR (RepositoryOffline), NULL, NULL, NULL);
		return;
	}

	for (link = ids; link; link = link->next) {
		const ECalComponentId *id = link->data;
		ECalComponentId *tmp_id;
		ECalComponent *cached_comp;
		GDataTasksTask *task;

		if (!id || !id->uid) {
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		PROPERTY_LOCK (gtasks);
		cached_comp = ecb_gtasks_get_cached_comp (gtasks, id->uid);
		PROPERTY_UNLOCK (gtasks);

		if (!cached_comp) {
			local_error = EDC_ERROR (ObjectNotFound);
			break;
		}

		task = ecb_gtasks_comp_to_gdata (cached_comp);
		if (!task) {
			g_object_unref (cached_comp);
			local_error = EDC_ERROR (InvalidObject);
			break;
		}

		/* Ignore protocol errors here, libgdata 0.15.1 results with "Error code 204 when deleting an entry: No Content",
		   while the delete succeeded */
		if (!gdata_tasks_service_delete_task (gtasks->priv->service, task, cancellable, &local_error) &&
		    !g_error_matches (local_error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR)) {
			g_object_unref (cached_comp);
			g_object_unref (task);
			break;
		}

		g_clear_error (&local_error);

		g_object_unref (task);

		PROPERTY_LOCK (gtasks);
		e_cal_backend_store_remove_component (gtasks->priv->store, id->uid, NULL);
		PROPERTY_UNLOCK (gtasks);

		tmp_id = e_cal_component_id_new (id->uid, NULL);
		e_cal_backend_notify_component_removed (backend, tmp_id, cached_comp, NULL);

		old_calcomps = g_slist_prepend (old_calcomps, cached_comp);
		removed_ids = g_slist_prepend (removed_ids, tmp_id);
	}

	old_calcomps = g_slist_reverse (old_calcomps);
	removed_ids = g_slist_reverse (removed_ids);

	e_data_cal_respond_remove_objects (cal, opid, local_error, removed_ids, old_calcomps, NULL);

	g_slist_free_full (removed_ids, (GDestroyNotify) e_cal_component_free_id);
	e_util_free_nullable_object_slist (old_calcomps);
}

static void
ecb_gtasks_receive_objects (ECalBackend *backend,
			    EDataCal *cal,
			    guint32 opid,
			    GCancellable *cancellable,
			    const gchar *calobj)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_data_cal_respond_receive_objects (cal, opid, EDC_ERROR (NotSupported));
}

static void
ecb_gtasks_send_objects (ECalBackend *backend,
			 EDataCal *cal,
			 guint32 opid,
			 GCancellable *cancellable,
			 const gchar *calobj)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_data_cal_respond_send_objects (cal, opid, EDC_ERROR (NotSupported), NULL, NULL);
}

static void
ecb_gtasks_get_attachment_uris (ECalBackend *backend,
				EDataCal *cal,
				guint32 opid,
				GCancellable *cancellable,
				const gchar *uid,
				const gchar *rid)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_data_cal_respond_get_attachment_uris (cal, opid, EDC_ERROR (NotSupported), NULL);
}

static void
ecb_gtasks_discard_alarm (ECalBackend *backend,
			  EDataCal *cal,
			  guint32 opid,
			  GCancellable *cancellable,
			  const gchar *uid,
			  const gchar *rid,
			  const gchar *auid)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL (cal));

	e_data_cal_respond_discard_alarm (cal, opid, EDC_ERROR (NotSupported));
}

static void
ecb_gtasks_start_view (ECalBackend *backend,
		       EDataCalView *view)
{
	ECalBackendGTasks *gtasks;
	ECalBackendSExp *sexp;
	ETimezoneCache *cache;
	const gchar *sexp_str;
	gboolean do_search;
	GSList *list, *iter;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL_VIEW (view));

	g_object_ref (view);

	gtasks = E_CAL_BACKEND_GTASKS (backend);
	sexp = e_data_cal_view_get_sexp (view);
	sexp_str = e_cal_backend_sexp_text (sexp);
	do_search = !g_str_equal (sexp_str, "#t");
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);

	cache = E_TIMEZONE_CACHE (backend);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (gtasks->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (gtasks->priv->store);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search || e_cal_backend_sexp_match_comp (sexp, comp, cache)) {
			e_data_cal_view_notify_components_added_1 (view, comp);
		}

		g_object_unref (comp);
	}

	g_slist_free (list);

	e_data_cal_view_notify_complete (view, NULL /* Success */);

	g_object_unref (view);
}

static void
ecb_gtasks_stop_view (ECalBackend *backend,
		      EDataCalView *view)
{
	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));
	g_return_if_fail (E_IS_DATA_CAL_VIEW (view));
}

static void
ecb_gtasks_add_timezone (ECalBackend *backend,
			 EDataCal *cal,
			 guint32 opid,
			 GCancellable *cancellable,
			 const gchar *tzobject)
{
	/* Nothing to do, times are in UTC */
	e_data_cal_respond_add_timezone (cal, opid, NULL);
}

static void
ecb_gtasks_shutdown (ECalBackend *backend)
{
	ECalBackendGTasks *gtasks;

	g_return_if_fail (E_IS_CAL_BACKEND_GTASKS (backend));

	gtasks = E_CAL_BACKEND_GTASKS (backend);

	ecb_gtasks_take_cancellable (gtasks, NULL);

	if (gtasks->priv->refresh_id) {
		ESource *source = e_backend_get_source (E_BACKEND (backend));
		if (source)
			e_source_refresh_remove_timeout (source, gtasks->priv->refresh_id);

		gtasks->priv->refresh_id = 0;
	}

	/* Chain up to parent's method. */
	E_CAL_BACKEND_CLASS (e_cal_backend_gtasks_parent_class)->shutdown (backend);
}

static void
e_cal_backend_gtasks_init (ECalBackendGTasks *gtasks)
{
	gtasks->priv = E_CAL_BACKEND_GTASKS_GET_PRIVATE (gtasks);
	gtasks->priv->cancellable = NULL;

	g_mutex_init (&gtasks->priv->property_mutex);
}

static void
ecb_gtasks_constructed (GObject *object)
{
	ECalBackendGTasks *gtasks = E_CAL_BACKEND_GTASKS (object);
	ESource *source;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->constructed (object);

	gtasks->priv->store = e_cal_backend_store_new (
		e_cal_backend_get_cache_dir (E_CAL_BACKEND (gtasks)),
		E_TIMEZONE_CACHE (gtasks));
	e_cal_backend_store_load (gtasks->priv->store);

	source = e_backend_get_source (E_BACKEND (gtasks));
	gtasks->priv->refresh_id = e_source_refresh_add_timeout (
		source, NULL, ecb_gtasks_time_to_refresh_data_cb, gtasks, NULL);
}

static void
ecb_gtasks_dispose (GObject *object)
{
	ECalBackendGTasks *gtasks = E_CAL_BACKEND_GTASKS (object);

	ecb_gtasks_take_cancellable (gtasks, NULL);

	g_clear_object (&gtasks->priv->cancellable);
	g_clear_object (&gtasks->priv->service);
	g_clear_object (&gtasks->priv->authorizer);
	g_clear_object (&gtasks->priv->tasklist);
	g_clear_object (&gtasks->priv->store);

	if (gtasks->priv->refresh_id) {
		ESource *source = e_backend_get_source (E_BACKEND (object));
		if (source)
			e_source_refresh_remove_timeout (source, gtasks->priv->refresh_id);

		gtasks->priv->refresh_id = 0;
	}

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->dispose (object);
}

static void
ecb_gtasks_finalize (GObject *object)
{
	ECalBackendGTasks *gtasks = E_CAL_BACKEND_GTASKS (object);

	g_mutex_clear (&gtasks->priv->property_mutex);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_gtasks_parent_class)->finalize (object);
}

static void
e_cal_backend_gtasks_class_init (ECalBackendGTasksClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;

	g_type_class_add_private (class, sizeof (ECalBackendGTasksPrivate));

	object_class->constructed = ecb_gtasks_constructed;
	object_class->dispose = ecb_gtasks_dispose;
	object_class->finalize = ecb_gtasks_finalize;

	backend_class->get_backend_property = ecb_gtasks_get_backend_property;
	backend_class->open = ecb_gtasks_open;
	backend_class->refresh = ecb_gtasks_refresh;
	backend_class->get_object = ecb_gtasks_get_object;
	backend_class->get_object_list = ecb_gtasks_get_object_list;
	backend_class->get_free_busy = ecb_gtasks_get_free_busy;
	backend_class->create_objects = ecb_gtasks_create_objects;
	backend_class->modify_objects = ecb_gtasks_modify_objects;
	backend_class->remove_objects = ecb_gtasks_remove_objects;
	backend_class->receive_objects = ecb_gtasks_receive_objects;
	backend_class->send_objects = ecb_gtasks_send_objects;
	backend_class->get_attachment_uris = ecb_gtasks_get_attachment_uris;
	backend_class->discard_alarm = ecb_gtasks_discard_alarm;
	backend_class->start_view = ecb_gtasks_start_view;
	backend_class->stop_view = ecb_gtasks_stop_view;
	backend_class->add_timezone = ecb_gtasks_add_timezone;
	backend_class->shutdown = ecb_gtasks_shutdown;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <libecal/e-cal-time-util.h>

#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-utils.h"

#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)

static ECalBackendClass *parent_class = NULL;

/* Private part of the CalBackendMAPI structure */
struct _ECalBackendMAPIPrivate {
	mapi_id_t 		fid;
	uint32_t 		olFolder;

	/* A mutex to control access to the private structure */
	GMutex			*mutex;
	ECalBackendCache	*cache;
	gboolean		read_only;
	char			*uri;
	char			*username;
	char			*password;
	int			timeout_id;
	CalMode			mode;
	gboolean		mode_changed;
	icaltimezone		*default_zone;
	GHashTable		*categories_by_id;
	GHashTable		*categories_by_name;

	/* number of calendar items in the folder */
	guint32			total_count;

	/* timeout handler for syncing sendoptions */
	guint			sendoptions_sync_timeout;
	
	char			*user_email;
	char			*local_attachments_store;

	/* used exclusively for delta fetching */
	GSList 			*cache_keys;
};

#define CACHE_REFRESH_INTERVAL 600000


/***** OBJECT CLASS FUNCTIONS *****/
static void 
e_cal_backend_mapi_dispose (GObject *object)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (object);
	priv = cbmapi->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void 
e_cal_backend_mapi_finalize (GObject *object)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_MAPI (object));

	cbmapi = E_CAL_BACKEND_MAPI (object);
	priv = cbmapi->priv;

	/* Clean up */
	if (priv->mutex) {
		g_mutex_free (priv->mutex);
		priv->mutex = NULL;
	}

	if (priv->cache) {
		g_object_unref (priv->cache);
		priv->cache = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->user_email) {
		g_free (priv->user_email);
		priv->user_email = NULL;
	}

	if (priv->local_attachments_store) {
		g_free (priv->local_attachments_store);
		priv->local_attachments_store = NULL;
	}

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	if (priv->sendoptions_sync_timeout) {
		g_source_remove (priv->sendoptions_sync_timeout);
		priv->sendoptions_sync_timeout = 0;
	}

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	g_free (priv);
	cbmapi->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/***** SYNC CLASS FUNCTIONS *****/
static ECalBackendSyncStatus 
e_cal_backend_mapi_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	*read_only = priv->read_only;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	*address = g_strdup (priv->user_email);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/* We don't support email alarms (?). This should not have been called. */

	*address = NULL;

	/* return Success OR OtherError ? */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	/* This is just a hack for SunONE */
	*attribute = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	/* FIXME: what else ? */

	*capabilities = g_strdup (
//				CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT ","
//				CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS ","
//				CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS ","
				CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
				CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS ","
//				CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
				CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
				CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
//				CAL_STATIC_CAPABILITY_NO_TRANSPARENCY ","
//				CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY ","
				CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND ","
//				CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS ","
//				CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
//				CAL_STATIC_CAPABILITY_SAVE_SCHEDULES ","
//				CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
//				CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
//				CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS ","
//				CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS ","
//				CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER ","
				CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT ","
//				CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
//				CAL_STATIC_CAPABILITY_NO_ORGANIZER ","
//				CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY ","
				CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING
				  );

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	gboolean authenticated = FALSE, status = FALSE;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	if (priv->mode == CAL_MODE_LOCAL)
		return GNOME_Evolution_Calendar_RepositoryOffline;
#if 0
	if (exchange_mapi_connection_exists ())
		authenticated = TRUE;
	else if (exchange_mapi_connection_new (priv->user_email, NULL)) 
		authenticated = TRUE;
	else { 
		authenticated = FALSE;
		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Authentication failed"));
		return GNOME_Evolution_Calendar_AuthenticationFailed;
	}
#endif
	status = exchange_mapi_remove_folder (priv->olFolder, priv->fid);
	if (!status)
		return GNOME_Evolution_Calendar_OtherError;

	g_mutex_lock (priv->mutex);

	/* remove the cache */
	if (priv->cache)
		e_file_cache_remove (E_FILE_CACHE (priv->cache));

	g_mutex_unlock (priv->mutex);

	/* anything else ? */

	return GNOME_Evolution_Calendar_Success;
}

static gboolean
get_changes_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
{
	ECalBackendMAPI *cbmapi	= data;
	ECalBackendMAPIPrivate *priv = cbmapi->priv;
        ECalComponent *comp = NULL;
	gchar *tmp = NULL;
	GSList *cache_comp_uid = NULL;

	tmp = exchange_mapi_util_mapi_id_to_string (mid);
	cache_comp_uid = g_slist_find_custom (priv->cache_keys, tmp, (GCompareFunc) (g_ascii_strcasecmp));
printf ("\n******* mid [%s] *******\n", tmp);

	if (cache_comp_uid == NULL) {
		ECalComponentVType type = E_CAL_COMPONENT_NO_TYPE;
		icalcomponent_kind kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));

		switch (kind) {
			case ICAL_VEVENT_COMPONENT:
				type = E_CAL_COMPONENT_EVENT;
				break;
			case ICAL_VTODO_COMPONENT:
				type = E_CAL_COMPONENT_TODO;
				break;
			case ICAL_VJOURNAL_COMPONENT:
				type = E_CAL_COMPONENT_JOURNAL;
				break;
			default:
				return FALSE;
		}

		comp = e_cal_component_new ();
		e_cal_component_set_new_vtype (comp, type);

		e_cal_backend_mapi_props_to_comp (cbmapi, array, comp, recipients, attachments, priv->default_zone);

		e_cal_component_set_uid (comp, tmp);

		if (E_IS_CAL_COMPONENT (comp)) {
			char *comp_str;

			e_cal_component_commit_sequence (comp);
			comp_str = e_cal_component_get_as_string (comp);	

			e_cal_backend_notify_object_created (E_CAL_BACKEND (cbmapi), (const char *) comp_str);
			e_cal_backend_cache_put_component (priv->cache, comp);

			g_free (comp_str);
		}
		g_object_unref (comp);
	} else {
		struct timeval t;

		if (get_mapi_SPropValue_array_date_timeval (&t, array, PR_LAST_MODIFICATION_TIME) == MAPI_E_SUCCESS) {
			struct icaltimetype itt, *cache_comp_lm = NULL;
			itt = icaltime_from_timet_with_zone (t.tv_sec, 0, 0);
			icaltime_set_timezone (&itt, icaltimezone_get_utc_timezone ());

			comp = e_cal_backend_cache_get_component (priv->cache, (const char *) cache_comp_uid->data, NULL);
			e_cal_component_get_last_modified (comp, &cache_comp_lm);
			if (icaltime_compare (itt, *cache_comp_lm) != 0) {
				char *old_comp_str = NULL, *new_comp_str = NULL;

				e_cal_component_commit_sequence (comp);
				old_comp_str = e_cal_component_get_as_string (comp);

				e_cal_backend_mapi_props_to_comp (cbmapi, array, comp, recipients, attachments, priv->default_zone);

				e_cal_component_commit_sequence (comp);
				new_comp_str = e_cal_component_get_as_string (comp);

				e_cal_backend_notify_object_modified (E_CAL_BACKEND (cbmapi), old_comp_str, new_comp_str);
				e_cal_backend_cache_put_component (priv->cache, comp);

				g_free (old_comp_str); g_free (new_comp_str);
			}
			g_object_unref (comp);
		}
		priv->cache_keys = g_slist_remove_link (priv->cache_keys, cache_comp_uid);
	}

	g_free (tmp);
	return TRUE;
}

static gboolean
get_deltas (gpointer handle)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	ECalBackendCache *cache; 
	icalcomponent_kind kind;
	char *time_string = NULL;
	char t_str [26]; 
	const char *serv_time;
	static GStaticMutex updating = G_STATIC_MUTEX_INIT;
	const char *time_interval_string;
	int time_interval;
	icaltimetype temp;
	struct tm tm;
	time_t current_time;
	GSList *ls = NULL;

	if (!handle)
		return FALSE;

	cbmapi = (ECalBackendMAPI *) handle;
	priv= cbmapi->priv;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));
	cache = priv->cache; 

	if (priv->mode == CAL_MODE_LOCAL)
		return FALSE;

	g_static_mutex_lock (&updating);

	serv_time = e_cal_backend_cache_get_server_utc_time (cache);
	if (serv_time) {
		icaltimetype tmp;
		g_strlcpy (t_str, e_cal_backend_cache_get_server_utc_time (cache), 26);
		if (!*t_str || !strcmp (t_str, "")) {
			/* FIXME: When time-stamp is crashed, getting changes from current time */
			g_warning ("Could not get a valid time stamp.");
			tmp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
			current_time = icaltime_as_timet_with_zone (tmp, icaltimezone_get_utc_timezone ());
			gmtime_r (&current_time, &tm);
			strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
		}
	} else {
		icaltimetype tmp;
		/* FIXME: When time-stamp is crashed, getting changes from current time */
		g_warning ("Could not get a valid time stamp.");
		tmp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
		current_time = icaltime_as_timet_with_zone (tmp, icaltimezone_get_utc_timezone ());
		gmtime_r (&current_time, &tm);
		strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
	}
	time_string = g_strdup (t_str);

	 /* e_cal_backend_cache_get_keys returns a list of all the keys. 
	  * The items in the list are pointers to internal data, 
	  * so should not be freed, only the list should. */
	priv->cache_keys = e_cal_backend_cache_get_keys (cache);

//	e_file_cache_freeze_changes (E_FILE_CACHE (cache));
	if (!exchange_mapi_connection_fetch_items (priv->olFolder, NULL, NULL, get_changes_cb, priv->fid, cbmapi)) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create cache file"));
		e_file_cache_thaw_changes (E_FILE_CACHE (cache));
		priv->cache_keys = NULL;
		return GNOME_Evolution_Calendar_OtherError;
	}
//	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

//	e_file_cache_freeze_changes (E_FILE_CACHE (cache));
	for (ls = priv->cache_keys; ls ; ls = g_slist_next (ls)) {
		ECalComponent *comp = NULL;
		icalcomponent *icalcomp = NULL;

		comp = e_cal_backend_cache_get_component (cache, (const char *) ls->data, NULL);

		if (!comp)
			continue;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (kind == icalcomponent_isa (icalcomp)) {
			char *comp_str = NULL;
			ECalComponentId *id = e_cal_component_get_id (comp);
			
			comp_str = e_cal_component_get_as_string (comp);
			e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbmapi), 
					id, comp_str, NULL);
			e_cal_backend_cache_remove_component (cache, (const char *) id->uid, id->rid);

			e_cal_component_free_id (id);
			g_free (comp_str);
		}
		g_object_unref (comp);
	}
//	e_file_cache_thaw_changes (E_FILE_CACHE (cache));

//	g_slist_free (priv->cache_keys); 
	priv->cache_keys = NULL;

#if 0
	filter = e_gw_filter_new ();
	/* Items modified after the time-stamp */
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_GREATERTHAN, "modified", time_string);
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "@type", get_element_type (kind));
	e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_AND, 2);

	status = e_gw_connection_get_items (cnc, cbmapi->priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);
	if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
		status = e_gw_connection_get_items (cnc, cbmapi->priv->container_id, "attachments recipients message recipientStatus default peek", filter, &item_list);
	g_object_unref (filter);

	if (status != E_GW_CONNECTION_STATUS_OK) {

		const char *msg = NULL;

		if (!attempts) {
			e_cal_backend_cache_put_key_value (cache, key, "2");
		} else {
			int failures;
			failures = g_ascii_strtod(attempts, NULL) + 1;
			e_cal_backend_cache_put_key_value (cache, key, GINT_TO_POINTER (failures));
		}

		if (status == E_GW_CONNECTION_STATUS_NO_RESPONSE) { 
			g_static_mutex_unlock (&updating);
			return TRUE;
		}

		msg = e_gw_connection_get_error_message (status);

		g_static_mutex_unlock (&updating);
		return TRUE;
	}
#endif
	temp = icaltime_from_string (time_string);
	current_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	gmtime_r (&current_time, &tm);

	time_interval = (CACHE_REFRESH_INTERVAL / 60000);
	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");
	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
	} 

	tm.tm_min += time_interval;

	strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
	time_string = g_strdup (t_str);

	e_cal_backend_cache_put_server_utc_time (cache, time_string);

	g_free (time_string);
	time_string = NULL;

	g_static_mutex_unlock (&updating);
	return TRUE;        
}

static gboolean
get_deltas_timeout (gpointer cbmapi)
{
	GThread *thread;
	GError *error = NULL;

	if (!cbmapi)
		return FALSE;

	thread = g_thread_create ((GThreadFunc) get_deltas, cbmapi, FALSE, &error);
	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);
	}

	return TRUE;
}

static GMutex *mutex = NULL;

static gboolean
cache_create_cb (struct mapi_SPropValue_array *properties, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
{
	ECalBackendMAPI *cbmapi	= data;
	ECalBackendMAPIPrivate *priv = cbmapi->priv;
        ECalComponent *comp = NULL;
	ECalComponentVType type = E_CAL_COMPONENT_NO_TYPE;
	int i;
	gchar *tmp = NULL;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi))) {
		case ICAL_VEVENT_COMPONENT:
			type = E_CAL_COMPONENT_EVENT;
			break;
		case ICAL_VTODO_COMPONENT:
			type = E_CAL_COMPONENT_TODO;
			break;
		case ICAL_VJOURNAL_COMPONENT:
			type = E_CAL_COMPONENT_JOURNAL;
			break;
		default:
			return FALSE; 
	}
	
	comp = e_cal_component_new ();
	e_cal_component_set_new_vtype (comp, type);
	tmp = exchange_mapi_util_mapi_id_to_string (mid);
printf ("\n******* mid [%s] *******\n", tmp);
	e_cal_component_set_uid (comp, tmp);
	e_cal_backend_mapi_props_to_comp (cbmapi, properties, comp, recipients, attachments, priv->default_zone);
//set_attachments_to_cal_component (cbmapi, comp, attachments);
	g_free (tmp);
#if 0
printf ("\n******* start of item *******\n");
for (i = 0; i < properties->cValues; i++) { 
	for (i = 0; i < properties->cValues; i++) {
		struct mapi_SPropValue *lpProp = &properties->lpProps[i];
		const char *tmp =  get_proptag_name (lpProp->ulPropTag);
		struct timeval t;
		if (tmp && *tmp)
			printf("\n%s \t",tmp);
		else
			printf("\n%x \t", lpProp->ulPropTag);
		switch(lpProp->ulPropTag & 0xFFFF) {
			case PT_BOOLEAN:
				printf(" (bool) - %d", lpProp->value.b);
				break;
			case PT_I2:
				printf(" (uint16_t) - %d", lpProp->value.i);
				break;
			case PT_LONG:
				printf(" (long) - %d", lpProp->value.l);
				break;
			case PT_DOUBLE:
				printf (" (double) -  %lf", lpProp->value.dbl);
				break;
			case PT_I8:
				printf (" (int) - %lld", lpProp->value.d);
				break;
			case PT_SYSTIME:
				get_mapi_SPropValue_array_date_timeval (&t, properties, lpProp->ulPropTag);
				printf (" (struct FILETIME *) - %p\t[%s]\t", &lpProp->value.ft, icaltime_as_ical_string (icaltime_from_timet_with_zone (t.tv_sec, 0, 0)));
				break;
			case PT_ERROR:
//				printf (" (error) - %p", lpProp->value.err);
				break;
			case PT_STRING8:
				printf(" (string) - %s", lpProp->value.lpszA ? lpProp->value.lpszA : "null" );
				break;
			case PT_UNICODE:
				printf(" (unicodestring) - %s", lpProp->value.lpszW ? lpProp->value.lpszW : "null");
				break;
			case PT_BINARY:
				printf(" (struct SBinary_short *) - %p", &lpProp->value.bin);
//				{ gchar *str; GByteArray *ba = g_byte_array_sized_new (lpProp->value.bin.cb); g_byte_array_append (ba, lpProp->value.bin.lpb, lpProp->value.bin.cb); str = g_strndup (ba->data, ba->len); printf ("\n%s\n", str); g_free (str); }
				break;
			case PT_MV_STRING8:
// 				printf(" (struct mapi_SLPSTRArray *) - %p", &lpProp->value.MVszA);
				break;
			default:
				printf(" - NONE NULL");
				break;
		}
	}
}
printf ("\n****** end of item *******\n");
#endif
	if (E_IS_CAL_COMPONENT (comp)) {
		char *comp_str;

		e_cal_component_commit_sequence (comp);
		comp_str = e_cal_component_get_as_string (comp);	
		e_cal_backend_notify_object_created (E_CAL_BACKEND (cbmapi), (const char *) comp_str);
		g_free (comp_str);
		e_cal_backend_cache_put_component (priv->cache, comp);
		g_object_unref (comp);
	}

	return TRUE;
}

static ECalBackendSyncStatus
e_cal_backend_mapi_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	ECalComponent *comp;

        comp = e_cal_component_new ();

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
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
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	ECalComponent *comp;

	cbmapi = E_CAL_BACKEND_MAPI (backend);	
	priv = cbmapi->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_OtherError);

	g_mutex_lock (priv->mutex);

	/* search the object in the cache */
	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);

	if (comp) {
		g_mutex_unlock (priv->mutex);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)))
			*object = e_cal_component_get_as_string (comp);
		else
			*object = NULL;

		g_object_unref (comp);

		return *object ? GNOME_Evolution_Calendar_Success : GNOME_Evolution_Calendar_ObjectNotFound;
	}

	g_mutex_unlock (priv->mutex);

	/* callers will never have a uid that is in server but not in cache */
	return GNOME_Evolution_Calendar_ObjectNotFound;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	GList *components, *l;
	ECalBackendSExp *cbsexp;
	gboolean search_needed = TRUE;

	cbmapi = E_CAL_BACKEND_MAPI (backend);	
	priv = cbmapi->priv;

	g_mutex_lock (priv->mutex);

	/* do I have to check the incoming sexp ? */
	search_needed = FALSE;
	cbsexp = e_cal_backend_sexp_new (sexp);

	if (!cbsexp) {
		g_mutex_unlock (priv->mutex);
		return GNOME_Evolution_Calendar_InvalidQuery;
	}

	*objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);

	for (l = components; l != NULL; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
				icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
					(e_cal_backend_sexp_match_comp (cbsexp, comp, E_CAL_BACKEND (backend)))) {
				*objects = g_list_append (*objects, e_cal_component_get_as_string (comp));
			} 
		}  
	}	

	g_object_unref (cbsexp);
	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_mutex_unlock (priv->mutex);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, GSList **list)
{
	/* TODO implement the function */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
populate_cache (ECalBackendMAPI *cbmapi)
{
	ECalBackendMAPIPrivate *priv;
	ESource *source = NULL;
        GList *list = NULL, *l;
	gboolean done = FALSE,  forward = FALSE;
	int cursor = 0;
	guint32	total, num = 0;
	int percent = 0, i;
	icalcomponent_kind kind;
//	const char *type;
//	EGwFilter* filter[3];
	char l_str[26]; 
	char h_str[26];
	icaltimetype temp;
	struct tm tm;
	time_t h_time, l_time;
	gchar *progress_string = NULL;

	priv = cbmapi->priv;
	source = e_cal_backend_get_source (E_CAL_BACKEND (cbmapi));
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));
	total = priv->total_count;

	if (!mutex) {
		mutex = g_mutex_new ();
	}

	g_mutex_lock (mutex);

//	type = get_element_type (kind);	

	/* Fetch the data with a bias to present, near past/future */
	temp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	i = g_ascii_strtod (g_getenv ("PRELOAD_WINDOW_DAYS")? g_getenv ("PRELOAD_WINDOW_DAYS"):"15", NULL);
	temp.day -= i;
	icaltime_normalize (temp);
	l_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	gmtime_r (&l_time, &tm);
	strftime (l_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
	temp.day += (2*i);
	icaltime_normalize (temp);
	h_time = icaltime_as_timet_with_zone (temp, icaltimezone_get_utc_timezone ());
	gmtime_r (&h_time, &tm);
	strftime (h_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
#if 0
	filter[0] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_GREATERTHAN_OR_EQUAL, "startDate", l_str);
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_LESSTHAN_OR_EQUAL, "startDate", h_str);
	e_gw_filter_add_filter_component (filter[0], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[0], E_GW_FILTER_OP_AND, 3);
	filter[1] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[1], E_GW_FILTER_OP_GREATERTHAN, "startDate", h_str);
	e_gw_filter_add_filter_component (filter[1], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[1], E_GW_FILTER_OP_AND, 2);
	filter[2] = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter[2], E_GW_FILTER_OP_LESSTHAN, "startDate", l_str);
	e_gw_filter_add_filter_component (filter[2], E_GW_FILTER_OP_EQUAL, "@type", type);
	e_gw_filter_group_conditions (filter[2], E_GW_FILTER_OP_AND, 2);

	for (i = 0; i < 3; i++) {
		status = e_gw_connection_create_cursor (priv->cnc,
				priv->container_id, 
				"recipients message recipientStatus attachments default peek", filter[i], &cursor);
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_cal_backend_groupwise_notify_error_code (cbmapi, status);
			g_mutex_unlock (mutex);
			return status;
		}
		done = FALSE;
		if (i == 1) {
			position = E_GW_CURSOR_POSITION_START;
			forward = TRUE;

		} else {
			position = E_GW_CURSOR_POSITION_END;
			forward = FALSE;
		}
			
		while (!done) {
			status = e_gw_connection_read_cursor (priv->cnc, priv->container_id, cursor, forward, CURSOR_ITEM_LIMIT, position, &list);
			if (status != E_GW_CONNECTION_STATUS_OK) {
				e_cal_backend_groupwise_notify_error_code (cbmapi, status);
				g_mutex_unlock (mutex);
				return status;
			}
			for (l = list; l != NULL; l = g_list_next(l)) {
				EGwItem *item;
				char *progress_string = NULL;

				item = E_GW_ITEM (l->data);
				comp = e_gw_item_to_cal_component (item, cbmapi);
				g_object_unref (item);
				
				/* Show the progress information */
				num++;
				percent = ((float) num/total) * 100;

				/* FIXME The total obtained from the server is wrong. Sometimes the num can 
				be greater than the total. The following makes sure that the percentage is not >= 100 */

				if (percent > 100)
					percent = 99; 

				progress_string = g_strdup_printf (_("Loading %s items"), e_source_peek_name (source));
				e_cal_backend_notify_view_progress (E_CAL_BACKEND (cbmapi), progress_string, percent);

				if (E_IS_CAL_COMPONENT (comp)) {
					char *comp_str;

					e_cal_component_commit_sequence (comp);
					comp_str = e_cal_component_get_as_string (comp);	
					e_cal_backend_notify_object_created (E_CAL_BACKEND (cbmapi), (const char *) comp_str);
					g_free (comp_str);
					e_cal_backend_cache_put_component (priv->cache, comp);
					g_object_unref (comp);
				}
				g_free (progress_string);
			}

			if (!list  || g_list_length (list) == 0)
				done = TRUE;
			g_list_free (list);
			list = NULL;
			position = E_GW_CURSOR_POSITION_CURRENT;
		}
		e_gw_connection_destroy_cursor (priv->cnc, priv->container_id, cursor);
		g_object_unref (filter[i]);
	}
#endif

	progress_string = g_strdup_printf (_("Loading %s items"), e_source_peek_name (source));
	/*  FIXME: Is there a way to update progress within the callback that follows ? */
	e_cal_backend_notify_view_progress (E_CAL_BACKEND (cbmapi), progress_string, 99);

	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
	if (!exchange_mapi_connection_fetch_items (priv->olFolder, NULL, NULL, cache_create_cb, priv->fid, cbmapi)) {
		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create cache file"));
		e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
		g_free (progress_string);
		return GNOME_Evolution_Calendar_OtherError;
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));

	e_cal_backend_notify_view_done (E_CAL_BACKEND (cbmapi), GNOME_Evolution_Calendar_Success);

	g_free (progress_string);
	g_mutex_unlock (mutex);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
cache_init (ECalBackendMAPI *cbmapi)
{
	ECalBackendMAPIPrivate *priv = cbmapi->priv;
	ECalBackendSyncStatus status;
	icalcomponent_kind kind;
	const char *time_interval_string;
	int time_interval;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi));

	time_interval = CACHE_REFRESH_INTERVAL;
	time_interval_string = g_getenv ("GETQM_TIME_INTERVAL");
	if (time_interval_string) {
		time_interval = g_ascii_strtod (time_interval_string, NULL);
		time_interval *= (60*1000); 
	}

	/* We poke the cache for a default timezone. Its
	 * absence indicates that the cache file has not been
	 * populated before. */
	if (!e_cal_backend_cache_get_marker (priv->cache)) {
		/* Populate the cache for the first time.*/
		/* start a timed polling thread set to 1 minute*/
		status = populate_cache (cbmapi);
		if (status != GNOME_Evolution_Calendar_Success) {
			g_warning (G_STRLOC ": Could not populate the cache");
			/*FIXME  why dont we do a notify here */
			return GNOME_Evolution_Calendar_PermissionDenied;
		} else {
			gchar *time_string = NULL;
			time_t current_time;
			icaltimetype tmp;
			struct tm tm;
			char t_str [26]; 

			tmp = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
			current_time = icaltime_as_timet_with_zone (tmp, icaltimezone_get_utc_timezone ());
			gmtime_r (&current_time, &tm);
			strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
			time_string = g_strdup (t_str);
 
			e_cal_backend_cache_set_marker (priv->cache);
			e_cal_backend_cache_put_server_utc_time (priv->cache, time_string);
			/* FIXME: not the right thing to do + do we have to free other stuff here ? */
			g_free (time_string);

			/*  Set delta fetch timeout */
			priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas_timeout, (gpointer) cbmapi);
			priv->mode = CAL_MODE_REMOTE;

			return GNOME_Evolution_Calendar_Success;
		}

	} else {
		/* get the deltas from the cache */
		if (get_deltas (cbmapi)) {
			priv->timeout_id = g_timeout_add (time_interval, (GSourceFunc) get_deltas_timeout, (gpointer) cbmapi);
			priv->mode = CAL_MODE_REMOTE;
			return GNOME_Evolution_Calendar_Success;
		} else {
			g_warning (G_STRLOC ": Could not populate the cache");
			/*FIXME  why dont we do a notify here */
			return GNOME_Evolution_Calendar_PermissionDenied;	
		}
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_mapi_connect (ECalBackendMAPI *cbmapi)
{
	ECalBackendMAPIPrivate *priv;
	ESource *source;
	ECalSourceType source_type;
	GThread *thread;
	GError *error = NULL;
	gboolean authenticated = FALSE;

	priv = cbmapi->priv;

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbmapi));

	if (exchange_mapi_connection_exists ())
		authenticated = TRUE;
	else if (exchange_mapi_connection_new (priv->user_email, NULL)) 
		authenticated = TRUE;
	else { 
		authenticated = FALSE;
		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Authentication failed"));
		return GNOME_Evolution_Calendar_AuthenticationFailed;
	}

	/* We have established a connection */
	if (authenticated && priv->cache && priv->fid) {
		priv->mode = CAL_MODE_REMOTE;
		priv->total_count = exchange_mapi_folder_get_total_count (exchange_mapi_folder_get_folder (priv->fid));
		if (priv->mode_changed && !priv->timeout_id ) {
			GThread *thread1;
			priv->mode_changed = FALSE;

			thread1 = g_thread_create ((GThreadFunc) get_deltas, cbmapi, FALSE, &error);
			if (!thread1) {
				g_warning (G_STRLOC ": %s", error->message);
				g_error_free (error);

				e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create thread for getting deltas"));
				return GNOME_Evolution_Calendar_OtherError;
			}
			priv->timeout_id = g_timeout_add (CACHE_REFRESH_INTERVAL, (GSourceFunc) get_deltas_timeout, (gpointer)cbmapi);
		}
		/* FIXME: put server UTC time in cache */
		return GNOME_Evolution_Calendar_Success;
	}

	priv->mode_changed = FALSE;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi))) {
	case ICAL_VEVENT_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_EVENT;
		break;
	case ICAL_VTODO_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_TODO;
		break;
	case ICAL_VJOURNAL_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_JOURNAL;
		break;
	default:
		source_type = E_CAL_SOURCE_TYPE_EVENT;
		break;
	}

	priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbmapi)), source_type);
	if (!priv->cache) {
		g_mutex_unlock (priv->mutex);
		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create cache file"));
		return GNOME_Evolution_Calendar_OtherError;
	}

	e_cal_backend_cache_put_default_timezone (priv->cache, priv->default_zone);

	/* spawn a new thread for caching the calendar items */
	thread = g_thread_create ((GThreadFunc) cache_init, cbmapi, FALSE, &error);
	if (!thread) {
		g_warning (G_STRLOC ": %s", error->message);
		g_error_free (error);

		e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create thread for populating cache"));
		return GNOME_Evolution_Calendar_OtherError;
	} 

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	ECalBackendSyncStatus status;
	ECalSourceType source_type;
	ESource *esource;
	const char *source = NULL;
	char *filename;
	char *mangled_uri;
	int i;
	uint32_t olFolder = 0;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	esource = e_cal_backend_get_source (E_CAL_BACKEND (cbmapi));
	if (!e_source_get_property (esource, "folder-id"))
		return GNOME_Evolution_Calendar_Success;

	g_mutex_lock (priv->mutex);

	cbmapi->priv->read_only = FALSE;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbmapi))) {
	case ICAL_VEVENT_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_EVENT;
		source = "calendar";
		olFolder = olFolderCalendar;
		break;
	case ICAL_VTODO_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_TODO;
		source = "tasks";
		olFolder = olFolderTasks;
		break;
	case ICAL_VJOURNAL_COMPONENT:
		source_type = E_CAL_SOURCE_TYPE_JOURNAL;
		source = "journal";
		olFolder = olFolderNotes;
		break;
	default:
		source_type = E_CAL_SOURCE_TYPE_EVENT;
		break;
	}

	/* Not for remote */	
	if (priv->mode == CAL_MODE_LOCAL) {
		const gchar *display_contents = NULL;

		cbmapi->priv->read_only = TRUE;				
		display_contents = e_source_get_property (esource, "offline_sync");
		
		if (!display_contents || !g_str_equal (display_contents, "1")) {
			g_mutex_unlock (priv->mutex);	
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

		/* Cache created here for the first time */
		if (!priv->cache) {
			priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbmapi)), source_type);
			if (!priv->cache) {
				g_mutex_unlock (priv->mutex);
				e_cal_backend_notify_error (E_CAL_BACKEND (cbmapi), _("Could not create cache file"));
				
				return GNOME_Evolution_Calendar_OtherError;
			}
		}
		e_cal_backend_cache_put_default_timezone (priv->cache, priv->default_zone);
		g_mutex_unlock (priv->mutex);	
		return GNOME_Evolution_Calendar_Success;
	}

	priv->username = g_strdup (username);
	priv->password = g_strdup (password);
	priv->user_email = g_strdup (e_source_get_property (esource, "profile"));
	exchange_mapi_util_mapi_id_from_string (e_source_get_property (esource, "folder-id"), &priv->fid);
	priv->olFolder = olFolder;

	/* Set the local attachment store*/
	mangled_uri = g_strdup (e_cal_backend_get_uri (E_CAL_BACKEND (cbmapi)));
	/* mangle the URI to not contain invalid characters */
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	filename = g_build_filename (g_get_home_dir (),
				     ".evolution/cache/", source,
				     mangled_uri,
				     NULL);

	g_free (mangled_uri);
	priv->local_attachments_store = 
		g_filename_to_uri (filename, NULL, NULL);
	g_free (filename);

	status = e_cal_backend_mapi_connect (cbmapi);
	g_mutex_unlock (priv->mutex);

	return status;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_create_object (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	mapi_id_t mid = 0;
	gchar *tmp = NULL;
	GSList *attachments = NULL;
int i;
ExchangeMAPIAttachment *att1 = g_new0 (ExchangeMAPIAttachment, 1);
att1->filename = "somefile.txt";
att1->value = g_byte_array_new ();
  for (i = 0; i < 10000; i++)
    g_byte_array_append (att1->value, (guint8*) "abcd", 4);
attachments = g_slist_append (attachments, att1);

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL && *calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL)
		return GNOME_Evolution_Calendar_RepositoryOffline;

	/* check the component for validity */
	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* Check if object exists */
	switch (priv->mode) {
		case CAL_MODE_ANY:
		case CAL_MODE_REMOTE:
			/* Create an appointment */
			mid = exchange_mapi_create_item (priv->olFolder, priv->fid, build_name_id, comp, build_props, comp, NULL, attachments);
			if (!mid) {
				g_object_unref (comp);
				return GNOME_Evolution_Calendar_OtherError;
			} 

			tmp = exchange_mapi_util_mapi_id_to_string (mid);
			e_cal_component_set_uid (comp, tmp);
			g_free (tmp);

			e_cal_component_commit_sequence (comp);
			e_cal_backend_cache_put_component (priv->cache, comp);
			*calobj = e_cal_component_get_as_string (comp);	
			e_cal_backend_notify_object_created (E_CAL_BACKEND (cbmapi), *calobj);

			break;
		default:
			break;	
	}

	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				  CalObjModType mod, char **old_object, char **new_object)
{
	ECalBackendMAPI *cbmapi;
        ECalBackendMAPIPrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp, *cache_comp = NULL;
	gboolean status;
	mapi_id_t mid;
//	EGwConnectionStatus status;
//	EGwItem *item, *cache_item;
	const char *uid = NULL, *rid = NULL;

	*old_object = *new_object = NULL;
	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL)
		return GNOME_Evolution_Calendar_RepositoryOffline;

	/* check the component for validity */
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	e_cal_component_get_uid (comp, &uid);
	rid = e_cal_component_get_recurid_as_string (comp);

	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE :	/* when online, send the item to the server */
		/* check if the object exists */
		cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
		if (!cache_comp) {
			g_message ("CRITICAL : Could not find the object in cache");
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}
		exchange_mapi_util_mapi_id_from_string (uid, &mid);
		status = exchange_mapi_modify_item (priv->olFolder, priv->fid, mid, build_name_id, comp, build_props, comp);
		if (!status) {
			g_object_unref (comp);
			g_object_unref (cache_comp);
			return GNOME_Evolution_Calendar_OtherError;
		}
		break;
	default :
		return GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED;
	}

	*old_object = e_cal_component_get_as_string (cache_comp);
	*new_object = e_cal_component_get_as_string (comp);

	g_object_unref (cache_comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

struct folder_data {
	mapi_id_t id;
};

static ECalBackendSyncStatus 
e_cal_backend_mapi_remove_object (ECalBackendSync *backend, EDataCal *cal,
				  const char *uid, const char *rid, CalObjModType mod, 
				  char **old_object, char **object)
{
	ECalBackendMAPI *cbmapi;
        ECalBackendMAPIPrivate *priv;
	icalcomponent *icalcomp;
	ECalBackendSyncStatus status;
	char *calobj = NULL;
	mapi_id_t mid;

	*old_object = *object = NULL;
	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL)
		return GNOME_Evolution_Calendar_RepositoryOffline;

	switch (priv->mode) {
	case CAL_MODE_ANY :
	case CAL_MODE_REMOTE : 	/* when online, modify/delete the item from the server */
		/* check if the object exists */
		status = e_cal_backend_mapi_get_object (backend, cal, uid, rid, &calobj);
		if (status != GNOME_Evolution_Calendar_Success)
			return status;

		/* check the component for validity */
		icalcomp = icalparser_parse_string (calobj);
		if (!icalcomp) {
			g_free (calobj);
			return GNOME_Evolution_Calendar_InvalidObject;
		}

		exchange_mapi_util_mapi_id_from_string (uid, &mid);

		if (mod == CALOBJ_MOD_THIS && rid && *rid) {
			char *obj = NULL, *new_object = NULL, *new_calobj = NULL;
			struct icaltimetype time_rid;

			/*remove a single instance of a recurring event and modify */
			time_rid = icaltime_from_string (rid);
			e_cal_util_remove_instances (icalcomp, time_rid, mod);
			new_calobj  = (char *) icalcomponent_as_ical_string (icalcomp);
			status = e_cal_backend_mapi_modify_object (backend, cal, new_calobj, mod, &obj, &new_object);
			if (status == GNOME_Evolution_Calendar_Success) {
				*old_object = obj;
				*object = new_object;
			}
			g_free (new_calobj);
		} else {
			GSList *list=NULL, *l, *comp_list = e_cal_backend_cache_get_components_by_uid (priv->cache, uid);

//			if (e_cal_component_has_attendees (E_CAL_COMPONENT (comp_list->data))) { 
//			} else { 
				struct folder_data *data = g_new (struct folder_data, 1);
				data->id = mid;
				list = g_slist_prepend (list, (gpointer) data);
//			}

			if (exchange_mapi_remove_items (priv->olFolder, priv->fid, list)) {
				for (l = comp_list; l; l = l->next) {
					ECalComponent *comp = E_CAL_COMPONENT (l->data);
					ECalComponentId *id = e_cal_component_get_id (comp);

					e_cal_backend_cache_remove_component (priv->cache, id->uid, id->rid);
					if (!id->rid || !g_str_equal (id->rid, rid))
						e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbmapi), id, e_cal_component_get_as_string (comp), NULL);
					e_cal_component_free_id (id);

					g_object_unref (comp);
				}
				*old_object = g_strdup (calobj);
				*object = NULL;
				status = GNOME_Evolution_Calendar_Success;
			} else
				status = GNOME_Evolution_Calendar_OtherError;

			g_slist_free (list);
			g_slist_free (comp_list);
		} 
		g_free (calobj);
		return status;
	default :
		return GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED;
	}
}

static ECalBackendSyncStatus	e_cal_backend_mapi_discard_alarm(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{

	return GNOME_Evolution_Calendar_Success;
	
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				 GList **users, char **modified_calobj)
{

	return GNOME_Evolution_Calendar_Success;

}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	icaltimezone *zone;
	icalcomponent *icalcomp;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!strcmp (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	icalcomponent *tz_comp;

	cbmapi = (ECalBackendMAPI *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbmapi->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;
		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		if (e_cal_backend_cache_put_timezone (priv->cache, zone) == FALSE) {
			icaltimezone_free (zone, 1);
			return GNOME_Evolution_Calendar_OtherError;
		}
		icaltimezone_free (zone, 1);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
	icaltimezone *zone;

	cbmapi = (ECalBackendMAPI *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_MAPI (cbmapi), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbmapi->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_free_busy (ECalBackendSync *backend, EDataCal *cal, 
				  GList *users, time_t start, time_t end, GList **freebusy)
{

	return GNOME_Evolution_Calendar_Success;	

}

static ECalBackendSyncStatus 
e_cal_backend_mapi_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
				GList **adds, GList **modifies, GList **deletes)
{

	return GNOME_Evolution_Calendar_Success;

}


/***** BACKEND CLASS FUNCTIONS *****/
static gboolean	
e_cal_backend_mapi_is_loaded (ECalBackend *backend)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	return priv->cache ? TRUE : FALSE;
}

static void 
e_cal_backend_mapi_start_query (ECalBackend *backend, EDataCalView *query)
{
        ECalBackendSyncStatus status;
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;
        GList *objects = NULL;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

        status = e_cal_backend_mapi_get_object_list (E_CAL_BACKEND_SYNC (backend), NULL,
						     e_data_cal_view_get_text (query), &objects);
        if (status != GNOME_Evolution_Calendar_Success) {
		e_data_cal_view_notify_done (query, status);
                return;
	}

       	/* notify listeners of all objects */
	if (objects) {
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);
		/* free memory */
		g_list_foreach (objects, (GFunc) g_free, NULL);
		g_list_free (objects);
	}

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static CalMode 
e_cal_backend_mapi_get_mode (ECalBackend *backend)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	return priv->mode;
}

static void 
e_cal_backend_mapi_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	if (!priv->mode && priv->mode == mode) {
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
				  	   cal_mode_to_corba (mode));
		return;
	}	

	g_mutex_lock (priv->mutex);

	priv->mode_changed = TRUE;
	switch (mode) {
		case CAL_MODE_REMOTE:
			priv->mode = CAL_MODE_REMOTE;
			priv->read_only = FALSE;
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					GNOME_Evolution_Calendar_MODE_REMOTE);
			e_cal_backend_notify_readonly (backend, priv->read_only);
			if (e_cal_backend_mapi_is_loaded (backend))
		              e_cal_backend_notify_auth_required(backend);
			break;
		case CAL_MODE_LOCAL:
			priv->mode = CAL_MODE_LOCAL;
			priv->read_only = TRUE;
			/* do we have to close the connection here ? */
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					GNOME_Evolution_Calendar_MODE_REMOTE);
			e_cal_backend_notify_readonly (backend, priv->read_only);
			break;
		default:
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					cal_mode_to_corba (mode));
	}	

	g_mutex_unlock (priv->mutex);
}

static icaltimezone * 
e_cal_backend_mapi_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	return priv->default_zone;
}

static icaltimezone *
e_cal_backend_mapi_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	icaltimezone *zone;

	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!zone)
		return icaltimezone_get_utc_timezone ();

	return zone;
}


/* MAPI CLASS INIT */
static void 
e_cal_backend_mapi_class_init (ECalBackendMAPIClass *class)
{
	GObjectClass *object_class;
	ECalBackendSyncClass *sync_class;
	ECalBackendClass *backend_class;
	
	object_class = (GObjectClass *) class;
	sync_class = (ECalBackendSyncClass *) class;
	backend_class = (ECalBackendClass *) class;
	
	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_mapi_dispose;
	object_class->finalize = e_cal_backend_mapi_finalize;

	sync_class->is_read_only_sync = e_cal_backend_mapi_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_mapi_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_mapi_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_mapi_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_mapi_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_mapi_open;
	sync_class->remove_sync = e_cal_backend_mapi_remove;
	sync_class->get_default_object_sync = e_cal_backend_mapi_get_default_object;
	sync_class->get_object_sync = e_cal_backend_mapi_get_object;
	sync_class->get_object_list_sync = e_cal_backend_mapi_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_mapi_get_attachment_list;
	sync_class->create_object_sync = e_cal_backend_mapi_create_object;
	sync_class->modify_object_sync = e_cal_backend_mapi_modify_object;
	sync_class->remove_object_sync = e_cal_backend_mapi_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_mapi_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_mapi_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_mapi_send_objects;
	sync_class->get_timezone_sync = e_cal_backend_mapi_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_mapi_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_mapi_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_mapi_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_mapi_get_changes;

	backend_class->is_loaded = e_cal_backend_mapi_is_loaded;
	backend_class->start_query = e_cal_backend_mapi_start_query;
	backend_class->get_mode = e_cal_backend_mapi_get_mode;
	backend_class->set_mode = e_cal_backend_mapi_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_mapi_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_mapi_internal_get_timezone;
}


static void
e_cal_backend_mapi_init (ECalBackendMAPI *cbmapi, ECalBackendMAPIClass *class)
{
	ECalBackendMAPIPrivate *priv;
	
	priv = g_new0 (ECalBackendMAPIPrivate, 1);

	priv->timeout_id = 0;
	priv->sendoptions_sync_timeout = 0;

	/* create the mutex for thread safety */
	priv->mutex = g_mutex_new ();

	cbmapi->priv = priv;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbmapi), TRUE);
}

GType
e_cal_backend_mapi_get_type (void)
{
	static GType e_cal_backend_mapi_type = 0;

	if (!e_cal_backend_mapi_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendMAPIClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_mapi_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendMAPI),
                        0,
                        (GInstanceInitFunc) e_cal_backend_mapi_init
                };
		e_cal_backend_mapi_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackendMAPI", &info, 0);
	}

	return e_cal_backend_mapi_type;
}


/***** UTILITY FUNCTIONS *****/
const char *	
e_cal_backend_mapi_get_local_attachments_store (ECalBackend *backend)
{
	ECalBackendMAPI *cbmapi;
	ECalBackendMAPIPrivate *priv;

	cbmapi = E_CAL_BACKEND_MAPI (backend);
	priv = cbmapi->priv;

	return priv->local_attachments_store;
}

static ECalBackendSyncStatus
e_cal_backend_mapi_authenticate ()
{
}


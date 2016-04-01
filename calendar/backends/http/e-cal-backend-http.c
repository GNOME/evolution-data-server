/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - iCalendar http backend
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Hans Petter Jansson <hpj@ximian.com>
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>
#include <libedata-cal/libedata-cal.h>
#include "e-cal-backend-http.h"

#define E_CAL_BACKEND_HTTP_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_HTTP, ECalBackendHttpPrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

G_DEFINE_TYPE (ECalBackendHttp, e_cal_backend_http, E_TYPE_CAL_BACKEND_SYNC)

/* Private part of the ECalBackendHttp structure */
struct _ECalBackendHttpPrivate {
	/* signal handler id for source's 'changed' signal */
	gulong source_changed_id;
	/* URI to get remote calendar data from */
	gchar *uri;

	/* The file cache */
	ECalBackendStore *store;

	/* Soup handles for remote file */
	SoupSession *soup_session;

	/* Reload */
	guint reload_timeout_id;
	guint is_loading : 1;

	/* Flags */
	gboolean opened;
	gboolean requires_auth;

	gchar *username;
	gchar *password;
};

#define d(x)

static void	e_cal_backend_http_add_timezone	(ECalBackendSync *backend,
						 EDataCal *cal,
						 GCancellable *cancellable,
						 const gchar *tzobj,
						 GError **perror);

static void
soup_authenticate (SoupSession *session,
                   SoupMessage *msg,
                   SoupAuth *auth,
                   gboolean retrying,
                   gpointer data)
{
	ECalBackendHttp *cbhttp;
	ESourceAuthentication *auth_extension;
	ESource *source;
	const gchar *extension_name;
	const gchar *username;
	gchar *auth_user;

	if (retrying)
		return;

	cbhttp = E_CAL_BACKEND_HTTP (data);

	source = e_backend_get_source (E_BACKEND (data));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	auth_user = e_source_authentication_dup_user (auth_extension);

	username = cbhttp->priv->username;
	if (!username || !*username)
		username = auth_user;

	if (!username || !*username || !cbhttp->priv->password)
		soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
	else
		soup_auth_authenticate (auth, username, cbhttp->priv->password);

	g_free (auth_user);
}

/* Dispose handler for the file backend */
static void
e_cal_backend_http_dispose (GObject *object)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;

	cbhttp = E_CAL_BACKEND_HTTP (object);
	priv = cbhttp->priv;

	if (priv->reload_timeout_id) {
		ESource *source = e_backend_get_source (E_BACKEND (cbhttp));
		e_source_refresh_remove_timeout (source, priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	if (priv->soup_session) {
		soup_session_abort (priv->soup_session);
		g_object_unref (priv->soup_session);
		priv->soup_session = NULL;
	}
	if (priv->source_changed_id) {
		g_signal_handler_disconnect (
			e_backend_get_source (E_BACKEND (cbhttp)),
			priv->source_changed_id);
		priv->source_changed_id = 0;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->dispose (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_http_finalize (GObject *object)
{
	ECalBackendHttpPrivate *priv;

	priv = E_CAL_BACKEND_HTTP_GET_PRIVATE (object);

	/* Clean up */

	if (priv->store) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	g_free (priv->uri);
	g_free (priv->username);
	g_free (priv->password);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->finalize (object);
}

static void
e_cal_backend_http_constructed (GObject *object)
{
	ECalBackendHttp *backend;
	SoupSession *soup_session;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->constructed (object);

	soup_session = soup_session_sync_new ();
	g_object_set (
		soup_session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	backend = E_CAL_BACKEND_HTTP (object);
	backend->priv->soup_session = soup_session;

	e_binding_bind_property (
		backend, "proxy-resolver",
		backend->priv->soup_session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	g_signal_connect (
		backend->priv->soup_session, "authenticate",
		G_CALLBACK (soup_authenticate), backend);

	if (g_getenv ("WEBCAL_DEBUG") != NULL) {
		SoupLogger *logger;

		logger = soup_logger_new (
			SOUP_LOGGER_LOG_BODY, 1024 * 1024);
		soup_session_add_feature (
			backend->priv->soup_session,
			SOUP_SESSION_FEATURE (logger));
		g_object_unref (logger);
	}
}

/* Calendar backend methods */

static gchar *
e_cal_backend_http_get_backend_property (ECalBackend *backend,
                                         const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (
			","
			CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS,
			CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED,
			NULL);

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		/* A HTTP backend has no particular email address associated
		 * with it (although that would be a useful feature some day).
		 */
		return NULL;

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		icalcomponent *icalcomp;
		icalcomponent_kind kind;
		gchar *prop_value;

		kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
		icalcomp = e_cal_util_new_component (kind);
		prop_value = icalcomponent_as_ical_string_r (icalcomp);
		icalcomponent_free (icalcomp);

		return prop_value;
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_http_parent_class)->
		get_backend_property (backend, prop_name);
}

static gchar *
webcal_to_http_method (const gchar *webcal_str,
                       gboolean secure)
{
	if (secure && (strncmp ("http://", webcal_str, sizeof ("http://") - 1) == 0))
		return g_strconcat ("https://", webcal_str + sizeof ("http://") - 1, NULL);

	if (strncmp ("webcal://", webcal_str, sizeof ("webcal://") - 1))
		return g_strdup (webcal_str);

	if (secure)
		return g_strconcat ("https://", webcal_str + sizeof ("webcal://") - 1, NULL);
	else
		return g_strconcat ("http://", webcal_str + sizeof ("webcal://") - 1, NULL);
}

static gboolean
notify_and_remove_from_cache (gpointer key,
                              gpointer value,
                              gpointer user_data)
{
	const gchar *calobj = value;
	ECalBackendHttp *cbhttp = E_CAL_BACKEND_HTTP (user_data);
	ECalComponent *comp = e_cal_component_new_from_string (calobj);
	ECalComponentId *id = e_cal_component_get_id (comp);

	if (id) {
		e_cal_backend_store_remove_component (cbhttp->priv->store, id->uid, id->rid);
		e_cal_backend_notify_component_removed (E_CAL_BACKEND (cbhttp), id, comp, NULL);

		e_cal_component_free_id (id);
	}

	g_object_unref (comp);

	return TRUE;
}

static void
empty_cache (ECalBackendHttp *cbhttp)
{
	ECalBackendHttpPrivate *priv;
	GSList *comps, *l;

	priv = cbhttp->priv;

	if (!priv->store)
		return;

	comps = e_cal_backend_store_get_components (priv->store);

	for (l = comps; l != NULL; l = g_slist_next (l)) {
		ECalComponentId *id;
		ECalComponent *comp = l->data;

		id = e_cal_component_get_id (comp);

		e_cal_backend_notify_component_removed ((ECalBackend *) cbhttp, id, comp, NULL);

		e_cal_component_free_id (id);
		g_object_unref (comp);
	}
	g_slist_free (comps);

	e_cal_backend_store_put_key_value (priv->store, "ETag", NULL);
	e_cal_backend_store_clean (priv->store);
}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
	ETimezoneCache *timezone_cache;

	timezone_cache = E_TIMEZONE_CACHE (user_data);

	return e_timezone_cache_get_timezone (timezone_cache, tzid);
}

static gboolean
put_component_to_store (ECalBackendHttp *cb,
                        ECalComponent *comp)
{
	time_t time_start, time_end;
	ECalBackendHttpPrivate *priv;
	ECalComponent *cache_comp;
	const gchar *uid;
	gchar *rid;

	priv = cb->priv;

	e_cal_component_get_uid (comp, &uid);
	rid = e_cal_component_get_recurid_as_string (comp);
	cache_comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	g_free (rid);

	if (cache_comp) {
		gboolean changed = TRUE;
		struct icaltimetype stamp1, stamp2;

		stamp1 = icaltime_null_time ();
		stamp2 = icaltime_null_time ();

		e_cal_component_get_dtstamp (comp, &stamp1);
		e_cal_component_get_dtstamp (cache_comp, &stamp2);

		changed = (icaltime_is_null_time (stamp1) && !icaltime_is_null_time (stamp2)) ||
			  (!icaltime_is_null_time (stamp1) && icaltime_is_null_time (stamp2)) ||
			  (icaltime_compare (stamp1, stamp2) != 0);

		if (!changed) {
			struct icaltimetype *last_modified1 = NULL, *last_modified2 = NULL;

			e_cal_component_get_last_modified (comp, &last_modified1);
			e_cal_component_get_last_modified (cache_comp, &last_modified2);

			changed = (last_modified1 != NULL && last_modified2 == NULL) ||
				  (last_modified1 == NULL && last_modified2 != NULL) ||
				  (last_modified1 != NULL && last_modified2 != NULL && icaltime_compare (*last_modified1, *last_modified2) != 0);

			if (last_modified1)
				e_cal_component_free_icaltimetype (last_modified1);
			if (last_modified2)
				e_cal_component_free_icaltimetype (last_modified2);

			if (!changed) {
				gint *sequence1 = NULL, *sequence2 = NULL;

				e_cal_component_get_sequence (comp, &sequence1);
				e_cal_component_get_sequence (cache_comp, &sequence2);

				changed = (sequence1 != NULL && sequence2 == NULL) ||
					  (sequence1 == NULL && sequence2 != NULL) ||
					  (sequence1 != NULL && sequence2 != NULL && *sequence1 != *sequence2);

				if (sequence1)
					e_cal_component_free_sequence (sequence1);
				if (sequence2)
					e_cal_component_free_sequence (sequence2);
			}
		}

		g_object_unref (cache_comp);

		if (!changed)
			return FALSE;
	}

	e_cal_util_get_component_occur_times (
		comp, &time_start, &time_end,
		resolve_tzid, cb, icaltimezone_get_utc_timezone (),
		e_cal_backend_get_kind (E_CAL_BACKEND (cb)));

	e_cal_backend_store_put_component_with_time_range (priv->store, comp, time_start, time_end);

	return TRUE;
}

static SoupMessage *
cal_backend_http_new_message (ECalBackendHttp *backend,
                              const gchar *uri)
{
	SoupMessage *soup_message;

	/* create message to be sent to server */
	soup_message = soup_message_new (SOUP_METHOD_GET, uri);
	if (soup_message == NULL)
		return NULL;

	soup_message_headers_append (
		soup_message->request_headers,
		"User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (
		soup_message->request_headers,
		"Connection", "close");
	soup_message_set_flags (
		soup_message, SOUP_MESSAGE_NO_REDIRECT);
	if (backend->priv->store != NULL) {
		const gchar *etag;

		etag = e_cal_backend_store_get_key_value (
			backend->priv->store, "ETag");

		if (etag != NULL && *etag != '\0')
			soup_message_headers_append (
				soup_message->request_headers,
				"If-None-Match", etag);
	}

	return soup_message;
}

static void
cal_backend_http_cancelled (GCancellable *cancellable,
                            gpointer user_data)
{
	struct {
		SoupSession *soup_session;
		SoupMessage *soup_message;
	} *cancel_data = user_data;

	soup_session_cancel_message (
		cancel_data->soup_session,
		cancel_data->soup_message,
		SOUP_STATUS_CANCELLED);
}

static void
cal_backend_http_extract_ssl_failed_data (SoupMessage *msg,
					  gchar **out_certificate_pem,
					  GTlsCertificateFlags *out_certificate_errors)
{
	GTlsCertificate *certificate = NULL;

	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	if (!out_certificate_pem || !out_certificate_errors)
		return;

	g_object_get (G_OBJECT (msg),
		"tls-certificate", &certificate,
		"tls-errors", out_certificate_errors,
		NULL);

	if (certificate) {
		g_object_get (certificate, "certificate-pem", out_certificate_pem, NULL);
		g_object_unref (certificate);
	}
}

static gboolean
cal_backend_http_load (ECalBackendHttp *backend,
                       const gchar *uri,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
                       GCancellable *cancellable,
                       GError **error)
{
	ECalBackendHttpPrivate *priv = backend->priv;
	ETimezoneCache *timezone_cache;
	SoupMessage *soup_message;
	SoupSession *soup_session;
	icalcomponent *icalcomp, *subcomp;
	icalcomponent_kind kind;
	const gchar *newuri;
	SoupURI *uri_parsed;
	GHashTable *old_cache;
	GSList *comps_in_cache;
	ESource *source;
	guint status_code;
	gulong cancel_id = 0;

	struct {
		SoupSession *soup_session;
		SoupMessage *soup_message;
	} cancel_data;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	soup_session = backend->priv->soup_session;
	soup_message = cal_backend_http_new_message (backend, uri);

	if (soup_message == NULL) {
		g_set_error (
			error, SOUP_HTTP_ERROR,
			SOUP_STATUS_MALFORMED,
			_("Malformed URI: %s"), uri);
		return FALSE;
	}

	if (G_IS_CANCELLABLE (cancellable)) {
		cancel_data.soup_session = soup_session;
		cancel_data.soup_message = soup_message;

		cancel_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (cal_backend_http_cancelled),
			&cancel_data, (GDestroyNotify) NULL);
	}

	source = e_backend_get_source (E_BACKEND (backend));

	e_soup_ssl_trust_connect (soup_message, source);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	status_code = soup_session_send_message (soup_session, soup_message);

	if (G_IS_CANCELLABLE (cancellable))
		g_cancellable_disconnect (cancellable, cancel_id);

	if (status_code == SOUP_STATUS_NOT_MODIFIED) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);

		/* attempts with ETag can result in 304 status code */
		g_object_unref (soup_message);
		priv->opened = TRUE;
		return TRUE;
	}

	/* Handle redirection ourselves */
	if (SOUP_STATUS_IS_REDIRECTION (status_code)) {
		gboolean success;

		newuri = soup_message_headers_get_list (
			soup_message->response_headers, "Location");

		d (g_message ("Redirected from %s to %s\n", async_context->uri, newuri));

		if (newuri != NULL) {
			gchar *redirected_uri;

			if (newuri[0]=='/') {
				g_warning ("Hey! Relative URI returned! Working around...\n");

				uri_parsed = soup_uri_new (uri);
				soup_uri_set_path (uri_parsed, newuri);
				soup_uri_set_query (uri_parsed, NULL);
				/* g_free (newuri); */

				newuri = soup_uri_to_string (uri_parsed, FALSE);
				g_message ("Translated URI: %s\n", newuri);
				soup_uri_free (uri_parsed);
			}

			redirected_uri =
				webcal_to_http_method (newuri, FALSE);
			success = cal_backend_http_load (
				backend, redirected_uri, out_certificate_pem, out_certificate_errors, cancellable, error);
			g_free (redirected_uri);

		} else {
			g_set_error (
				error, SOUP_HTTP_ERROR,
				SOUP_STATUS_BAD_REQUEST,
				_("Redirected to Invalid URI"));
			success = FALSE;
		}

		if (success) {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
		} else {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
		}

		g_object_unref (soup_message);
		return success;
	}

	/* check status code */
	if (!SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		/* because evolution knows only G_IO_ERROR_CANCELLED */
		if (status_code == SOUP_STATUS_CANCELLED)
			g_set_error (
				error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
				"%s", soup_message->reason_phrase);
		else
			g_set_error (
				error, SOUP_HTTP_ERROR, status_code,
				"%s", soup_message->reason_phrase);

		if (status_code == SOUP_STATUS_SSL_FAILED) {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_SSL_FAILED);
			cal_backend_http_extract_ssl_failed_data (soup_message, out_certificate_pem, out_certificate_errors);
		} else {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
		}

		g_object_unref (soup_message);
		empty_cache (backend);
		return FALSE;
	}

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);

	if (priv->store) {
		const gchar *etag;

		etag = soup_message_headers_get_one (
			soup_message->response_headers, "ETag");

		if (etag != NULL && *etag == '\0')
			etag = NULL;

		e_cal_backend_store_put_key_value (priv->store, "ETag", etag);
	}

	/* get the calendar from the response */
	icalcomp = icalparser_parse_string (soup_message->response_body->data);

	if (!icalcomp) {
		g_set_error (
			error, SOUP_HTTP_ERROR,
			SOUP_STATUS_MALFORMED,
			_("Bad file format."));
		g_object_unref (soup_message);
		empty_cache (backend);
		return FALSE;
	}

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		g_set_error (
			error, SOUP_HTTP_ERROR,
			SOUP_STATUS_MALFORMED,
			_("Not a calendar."));
		icalcomponent_free (icalcomp);
		g_object_unref (soup_message);
		empty_cache (backend);
		return FALSE;
	}

	g_object_unref (soup_message);
	soup_message = NULL;

	/* Update cache */
	old_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	comps_in_cache = e_cal_backend_store_get_components (priv->store);
	while (comps_in_cache != NULL) {
		const gchar *uid;
		ECalComponent *comp = comps_in_cache->data;

		e_cal_component_get_uid (comp, &uid);
		g_hash_table_insert (old_cache, g_strdup (uid), e_cal_component_get_as_string (comp));

		comps_in_cache = g_slist_remove (comps_in_cache, comps_in_cache->data);
		g_object_unref (comp);
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	subcomp = icalcomponent_get_first_component (icalcomp, ICAL_ANY_COMPONENT);
	e_cal_backend_store_freeze_changes (priv->store);
	while (subcomp) {
		ECalComponent *comp;
		icalcomponent_kind subcomp_kind;
		icalproperty *prop = NULL;

		subcomp_kind = icalcomponent_isa (subcomp);
		prop = icalcomponent_get_first_property (subcomp, ICAL_UID_PROPERTY);
		if (!prop && subcomp_kind == kind) {
			gchar *new_uid = e_cal_component_gen_uid ();
			icalcomponent_set_uid (subcomp, new_uid);
			g_free (new_uid);
		}

		if (subcomp_kind == kind) {
			comp = e_cal_component_new ();
			if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp))) {
				const gchar *uid;
				gpointer orig_key, orig_value;

				e_cal_component_get_uid (comp, &uid);

				if (!put_component_to_store (backend, comp)) {
					g_hash_table_remove (old_cache, uid);
				} else if (g_hash_table_lookup_extended (old_cache, uid, &orig_key, &orig_value)) {
					ECalComponent *orig_comp = e_cal_component_new_from_string (orig_value);

					e_cal_backend_notify_component_modified (E_CAL_BACKEND (backend), orig_comp, comp);

					g_hash_table_remove (old_cache, uid);
					if (orig_comp)
						g_object_unref (orig_comp);
				} else {
					e_cal_backend_notify_component_created (E_CAL_BACKEND (backend), comp);
				}
			}

			g_object_unref (comp);
		} else if (subcomp_kind == ICAL_VTIMEZONE_COMPONENT) {
			icaltimezone *zone;

			zone = icaltimezone_new ();
			icaltimezone_set_component (zone, icalcomponent_new_clone (subcomp));
			e_timezone_cache_add_timezone (timezone_cache, zone);

			icaltimezone_free (zone, 1);
		}

		subcomp = icalcomponent_get_next_component (icalcomp, ICAL_ANY_COMPONENT);
	}

	e_cal_backend_store_thaw_changes (priv->store);

	/* notify the removals */
	g_hash_table_foreach_remove (old_cache, (GHRFunc) notify_and_remove_from_cache, backend);
	g_hash_table_destroy (old_cache);

	/* free memory */
	icalcomponent_free (icalcomp);

	priv->opened = TRUE;

	return TRUE;
}

static const gchar *
cal_backend_http_ensure_uri (ECalBackendHttp *backend)
{
	ESource *source;
	ESourceSecurity *security_extension;
	ESourceWebdav *webdav_extension;
	SoupURI *soup_uri;
	gboolean secure_connection;
	const gchar *extension_name;
	gchar *uri_string;

	if (backend->priv->uri != NULL)
		return backend->priv->uri;

	source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	security_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	secure_connection = e_source_security_get_secure (security_extension);

	soup_uri = e_source_webdav_dup_soup_uri (webdav_extension);
	uri_string = soup_uri_to_string (soup_uri, FALSE);
	soup_uri_free (soup_uri);

	backend->priv->uri = webcal_to_http_method (
		uri_string, secure_connection);

	g_free (uri_string);

	return backend->priv->uri;
}

static void
begin_retrieval_cb (GTask *task,
		    gpointer source_object,
		    gpointer task_tada,
		    GCancellable *cancellable)
{
	ECalBackendHttp *backend = source_object;
	const gchar *uri;
	gchar *certificate_pem = NULL;
	GTlsCertificateFlags certificate_errors = 0;
	GError *error = NULL;

	if (!e_backend_get_online (E_BACKEND (backend)) ||
	    backend->priv->is_loading)
		return;

	d (g_message ("Starting retrieval...\n"));

	backend->priv->is_loading = TRUE;

	uri = cal_backend_http_ensure_uri (backend);
	cal_backend_http_load (backend, uri, &certificate_pem, &certificate_errors, cancellable, &error);

	if (g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
	    g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		GError *local_error = NULL;
		ESourceCredentialsReason reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;

		if (g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
			reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
		}

		e_backend_credentials_required_sync (E_BACKEND (backend),
			reason, certificate_pem, certificate_errors, error, cancellable, &local_error);

		g_clear_error (&error);
		error = local_error;
	} else if (g_error_matches (error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN)) {
		GError *local_error = NULL;

		e_backend_credentials_required_sync (E_BACKEND (backend), E_SOURCE_CREDENTIALS_REASON_REJECTED,
			certificate_pem, certificate_errors, error, cancellable, &local_error);

		g_clear_error (&error);
		error = local_error;
	}

	g_free (certificate_pem);
	backend->priv->is_loading = FALSE;

	/* Ignore cancellations. */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_error_free (error);

	} else if (error != NULL) {
		e_cal_backend_notify_error (
			E_CAL_BACKEND (backend),
			error->message);
		empty_cache (backend);
		g_error_free (error);
	}

	d (g_message ("Retrieval really done.\n"));
}

static void
http_cal_schedule_begin_retrieval (ECalBackendHttp *cbhttp)
{
	GTask *task;

	task = g_task_new (cbhttp, NULL, NULL, NULL);

	g_task_run_in_thread (task, begin_retrieval_cb);

	g_object_unref (task);
}

static void
source_changed_cb (ESource *source,
                   ECalBackendHttp *cbhttp)
{
	g_return_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp));

	g_object_ref (cbhttp);

	if (cbhttp->priv->uri != NULL) {
		gboolean uri_changed;
		const gchar *new_uri;
		gchar *old_uri;

		old_uri = g_strdup (cbhttp->priv->uri);

		g_free (cbhttp->priv->uri);
		cbhttp->priv->uri = NULL;

		new_uri = cal_backend_http_ensure_uri (cbhttp);

		uri_changed = (g_strcmp0 (old_uri, new_uri) != 0);

		if (uri_changed && !cbhttp->priv->is_loading)
			http_cal_schedule_begin_retrieval (cbhttp);

		g_free (old_uri);
	}

	g_object_unref (cbhttp);
}

static void
http_cal_reload_cb (ESource *source,
                    gpointer user_data)
{
	ECalBackendHttp *cbhttp = user_data;

	g_return_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp));

	if (!e_backend_get_online (E_BACKEND (cbhttp)))
		return;

	http_cal_schedule_begin_retrieval (cbhttp);
}

/* Open handler for the file backend */
static void
e_cal_backend_http_open (ECalBackendSync *backend,
                         EDataCal *cal,
                         GCancellable *cancellable,
                         gboolean only_if_exists,
                         GError **perror)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	ESource *source;
	ESourceWebdav *webdav_extension;
	const gchar *extension_name;
	const gchar *cache_dir;
	gboolean opened = TRUE;
	gchar *tmp;
	GError *local_error = NULL;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	/* already opened, thus can skip all this initialization */
	if (priv->opened)
		return;

	source = e_backend_get_source (E_BACKEND (backend));
	cache_dir = e_cal_backend_get_cache_dir (E_CAL_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	e_source_webdav_unset_temporary_ssl_trust (webdav_extension);

	if (priv->source_changed_id == 0) {
		priv->source_changed_id = g_signal_connect (
			source, "changed",
			G_CALLBACK (source_changed_cb), cbhttp);
	}

	/* always read uri again */
	tmp = priv->uri;
	priv->uri = NULL;
	g_free (tmp);

	if (priv->store == NULL) {
		/* remove the old cache while migrating to ECalBackendStore */
		e_cal_backend_cache_remove (cache_dir, "cache.xml");
		priv->store = e_cal_backend_store_new (
			cache_dir, E_TIMEZONE_CACHE (backend));
		e_cal_backend_store_load (priv->store);

		if (!priv->store) {
			g_propagate_error (
				perror, EDC_ERROR_EX (OtherError,
				_("Could not create cache file")));
			return;
		}
	}

	e_cal_backend_set_writable (E_CAL_BACKEND (backend), FALSE);

	if (e_backend_get_online (E_BACKEND (backend))) {
		gchar *certificate_pem = NULL;
		GTlsCertificateFlags certificate_errors = 0;
		const gchar *uri;

		uri = cal_backend_http_ensure_uri (cbhttp);

		opened = cal_backend_http_load (cbhttp, uri, &certificate_pem,
			&certificate_errors, cancellable, &local_error);

		if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		    g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED) ||
		    (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN) &&
		    !cbhttp->priv->password)) {
			GError *local_error2 = NULL;
			ESourceCredentialsReason reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;

			if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
				reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
			}

			e_backend_credentials_required_sync (E_BACKEND (cbhttp), reason, certificate_pem,
				certificate_errors, local_error, cancellable, &local_error2);
			g_clear_error (&local_error);
			local_error = local_error2;
		} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN)) {
			GError *local_error2 = NULL;

			e_backend_credentials_required_sync (E_BACKEND (cbhttp), E_SOURCE_CREDENTIALS_REASON_REJECTED,
				certificate_pem, certificate_errors, local_error, cancellable, &local_error2);

			g_clear_error (&local_error);
			local_error = local_error2;
		}

		g_free (certificate_pem);

		if (local_error != NULL)
			g_propagate_error (perror, local_error);
	}

	if (opened) {
		if (!priv->reload_timeout_id)
			priv->reload_timeout_id = e_source_refresh_add_timeout (source, NULL, http_cal_reload_cb, backend, NULL);
	}
}

static void
e_cal_backend_http_refresh (ECalBackendSync *backend,
                            EDataCal *cal,
                            GCancellable *cancellable,
                            GError **perror)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	ESource *source;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->opened ||
	    priv->is_loading)
		return;

	source = e_backend_get_source (E_BACKEND (cbhttp));
	g_return_if_fail (source != NULL);

	e_source_refresh_force_timeout (source);
}

/* Set_mode handler for the http backend */
static void
e_cal_backend_http_notify_online_cb (ECalBackend *backend,
                                     GParamSpec *pspec)
{
	gboolean loaded;
	gboolean online;

	online = e_backend_get_online (E_BACKEND (backend));
	loaded = e_cal_backend_is_opened (backend);

	if (online && loaded)
		http_cal_schedule_begin_retrieval (E_CAL_BACKEND_HTTP (backend));
}

/* Get_object_component handler for the http backend */
static void
e_cal_backend_http_get_object (ECalBackendSync *backend,
                               EDataCal *cal,
                               GCancellable *cancellable,
                               const gchar *uid,
                               const gchar *rid,
                               gchar **object,
                               GError **error)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	ECalComponent *comp = NULL;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->store) {
		g_propagate_error (error, EDC_ERROR (ObjectNotFound));
		return;
	}

	if (rid && *rid) {
		comp = e_cal_backend_store_get_component (priv->store, uid, rid);
		if (!comp) {
			g_propagate_error (error, EDC_ERROR (ObjectNotFound));
			return;
		}

		*object = e_cal_component_get_as_string (comp);
		g_object_unref (comp);
	} else {
		*object = e_cal_backend_store_get_components_by_uid_as_ical_string (priv->store, uid);
		if (!*object)
			g_propagate_error (error, EDC_ERROR (ObjectNotFound));
	}
}

/* Add_timezone handler for the file backend */
static void
e_cal_backend_http_add_timezone (ECalBackendSync *backend,
                                 EDataCal *cal,
                                 GCancellable *cancellable,
                                 const gchar *tzobj,
                                 GError **error)
{
	ETimezoneCache *timezone_cache;
	icalcomponent *tz_comp;
	icaltimezone *zone;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT) {
		icalcomponent_free (tz_comp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	e_timezone_cache_add_timezone (timezone_cache, zone);
}

/* Get_objects_in_range handler for the file backend */
static void
e_cal_backend_http_get_object_list (ECalBackendSync *backend,
                                    EDataCal *cal,
                                    GCancellable *cancellable,
                                    const gchar *sexp,
                                    GSList **objects,
                                    GError **perror)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GSList *components, *l;
	ECalBackendSExp *cbsexp;
	ETimezoneCache *timezone_cache;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	if (!priv->store) {
		g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		return;
	}

	/* process all components in the cache */
	cbsexp = e_cal_backend_sexp_new (sexp);

	*objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		cbsexp,
		&occur_start,
		&occur_end);

	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = g_slist_next (l)) {
		if (e_cal_backend_sexp_match_comp (cbsexp, E_CAL_COMPONENT (l->data), timezone_cache)) {
			*objects = g_slist_append (*objects, e_cal_component_get_as_string (l->data));
		}
	}

	g_slist_foreach (components, (GFunc) g_object_unref, NULL);
	g_slist_free (components);
	g_object_unref (cbsexp);
}

static void
e_cal_backend_http_start_view (ECalBackend *backend,
                               EDataCalView *query)
{
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	GSList *components, *l;
	GSList *objects = NULL;
	ECalBackendSExp *cbsexp;
	ETimezoneCache *timezone_cache;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	cbsexp = e_data_cal_view_get_sexp (query);

	d (g_message (G_STRLOC ": Starting query (%s)", e_cal_backend_sexp_text (cbsexp)));

	if (!priv->store) {
		GError *error = EDC_ERROR (NoSuchCal);
		e_data_cal_view_notify_complete (query, error);
		g_error_free (error);
		return;
	}

	/* process all components in the cache */
	objects = NULL;
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		cbsexp,
		&occur_start,
		&occur_end);

	components = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (priv->store);

	for (l = components; l != NULL; l = g_slist_next (l)) {
		ECalComponent *comp = l->data;

		if (e_cal_backend_sexp_match_comp (cbsexp, comp, timezone_cache)) {
			objects = g_slist_append (objects, comp);
		}
	}

	e_data_cal_view_notify_components_added (query, objects);

	g_slist_free_full (components, g_object_unref);
	g_slist_free (objects);

	e_data_cal_view_notify_complete (query, NULL /* Success */);
}

/***** static icaltimezone *
resolve_tzid (const gchar *tzid,
 *            gpointer user_data)
{
	icalcomponent *vcalendar_comp = user_data;
 *
	if (!tzid || !tzid[0])
		return NULL;
 *      else if (!strcmp (tzid, "UTC"))
		return icaltimezone_get_utc_timezone ();
 *
	return icalcomponent_get_timezone (vcalendar_comp, tzid);
} *****/

static gboolean
free_busy_instance (ECalComponent *comp,
                    time_t instance_start,
                    time_t instance_end,
                    gpointer data)
{
	icalcomponent *vfb = data;
	icalproperty *prop;
	icalparameter *param;
	struct icalperiodtype ipt;
	icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	ipt.start = icaltime_from_timet_with_zone (instance_start, FALSE, utc_zone);
	ipt.end = icaltime_from_timet_with_zone (instance_end, FALSE, utc_zone);
	ipt.duration = icaldurationtype_null_duration ();

        /* add busy information to the vfb component */
	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	icalproperty_set_freebusy (prop, ipt);

	param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
	icalproperty_add_parameter (prop, param);

	icalcomponent_add_property (vfb, prop);

	return TRUE;
}

static icalcomponent *
create_user_free_busy (ECalBackendHttp *cbhttp,
                       const gchar *address,
                       const gchar *cn,
                       time_t start,
                       time_t end)
{
	GSList *slist = NULL, *l;
	icalcomponent *vfb;
	icaltimezone *utc_zone;
	ECalBackendSExp *obj_sexp;
	ECalBackendHttpPrivate *priv;
	ECalBackendStore *store;
	gchar *query, *iso_start, *iso_end;

	priv = cbhttp->priv;
	store = priv->store;

        /* create the (unique) VFREEBUSY object that we'll return */
	vfb = icalcomponent_new_vfreebusy ();
	if (address != NULL) {
		icalproperty *prop;
		icalparameter *param;

		prop = icalproperty_new_organizer (address);
		if (prop != NULL && cn != NULL) {
			param = icalparameter_new_cn (cn);
			icalproperty_add_parameter (prop, param);
		}
		if (prop != NULL)
			icalcomponent_add_property (vfb, prop);
	}
	utc_zone = icaltimezone_get_utc_timezone ();
	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

        /* add all objects in the given interval */
	iso_start = isodate_from_time_t (start);
	iso_end = isodate_from_time_t (end);
	query = g_strdup_printf (
		"occur-in-time-range? (make-time \"%s\") (make-time \"%s\")",
		iso_start, iso_end);
	obj_sexp = e_cal_backend_sexp_new (query);
	g_free (query);
	g_free (iso_start);
	g_free (iso_end);

	if (!obj_sexp)
		return vfb;

	slist = e_cal_backend_store_get_components (store);

	for (l = slist; l; l = g_slist_next (l)) {
		ECalComponent *comp = l->data;
		icalcomponent *icalcomp, *vcalendar_comp;
		icalproperty *prop;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (!icalcomp)
			continue;

                /* If the event is TRANSPARENT, skip it. */
		prop = icalcomponent_get_first_property (
			icalcomp,
			ICAL_TRANSP_PROPERTY);
		if (prop) {
			icalproperty_transp transp_val = icalproperty_get_transp (prop);
			if (transp_val == ICAL_TRANSP_TRANSPARENT ||
			    transp_val == ICAL_TRANSP_TRANSPARENTNOCONFLICT)
				continue;
		}

		if (!e_cal_backend_sexp_match_comp (
			obj_sexp, l->data,
			E_TIMEZONE_CACHE (cbhttp)))
			continue;

		vcalendar_comp = icalcomponent_get_parent (icalcomp);
		if (!vcalendar_comp)
			vcalendar_comp = icalcomp;
		e_cal_recur_generate_instances (
			comp, start, end,
			free_busy_instance,
			vfb,
			resolve_tzid,
			vcalendar_comp,
			icaltimezone_get_utc_timezone ());
	}
	g_object_unref (obj_sexp);

	return vfb;
}

/* Get_free_busy handler for the file backend */
static void
e_cal_backend_http_get_free_busy (ECalBackendSync *backend,
                                  EDataCal *cal,
                                  GCancellable *cancellable,
                                  const GSList *users,
                                  time_t start,
                                  time_t end,
                                  GSList **freebusy,
                                  GError **error)
{
	ESourceRegistry *registry;
	ECalBackendHttp *cbhttp;
	ECalBackendHttpPrivate *priv;
	gchar *address, *name;
	icalcomponent *vfb;
	gchar *calobj;

	cbhttp = E_CAL_BACKEND_HTTP (backend);
	priv = cbhttp->priv;

	if (!priv->store) {
		g_propagate_error (error, EDC_ERROR (NoSuchCal));
		return;
	}

	registry = e_cal_backend_get_registry (E_CAL_BACKEND (backend));

	if (users == NULL) {
		if (e_cal_backend_mail_account_get_default (registry, &address, &name)) {
			vfb = create_user_free_busy (cbhttp, address, name, start, end);
			calobj = icalcomponent_as_ical_string_r (vfb);
                        *freebusy = g_slist_append (*freebusy, calobj);
			icalcomponent_free (vfb);
			g_free (address);
			g_free (name);
		}
	} else {
		const GSList *l;
		for (l = users; l != NULL; l = l->next ) {
			address = l->data;
			if (e_cal_backend_mail_account_is_valid (registry, address, &name)) {
				vfb = create_user_free_busy (cbhttp, address, name, start, end);
				calobj = icalcomponent_as_ical_string_r (vfb);
                                *freebusy = g_slist_append (*freebusy, calobj);
				icalcomponent_free (vfb);
				g_free (name);
			}
		}
	}
}

static void
e_cal_backend_http_create_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *calobjs,
                                   GSList **uids,
                                   GSList **new_components,
                                   GError **perror)
{
	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
}

static void
e_cal_backend_http_modify_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *calobjs,
                                   ECalObjModType mod,
                                   GSList **old_components,
                                   GSList **new_components,
                                   GError **perror)
{
	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
}

/* Remove_objects handler for the file backend */
static void
e_cal_backend_http_remove_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *ids,
                                   ECalObjModType mod,
                                   GSList **old_components,
                                   GSList **new_components,
                                   GError **perror)
{
	*old_components = *new_components = NULL;

	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
}

/* Update_objects handler for the file backend. */
static void
e_cal_backend_http_receive_objects (ECalBackendSync *backend,
                                    EDataCal *cal,
                                    GCancellable *cancellable,
                                    const gchar *calobj,
                                    GError **perror)
{
	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
}

static void
e_cal_backend_http_send_objects (ECalBackendSync *backend,
                                 EDataCal *cal,
                                 GCancellable *cancellable,
                                 const gchar *calobj,
                                 GSList **users,
                                 gchar **modified_calobj,
                                 GError **perror)
{
	*users = NULL;
	*modified_calobj = NULL;

	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
}

static ESourceAuthenticationResult
e_cal_backend_http_authenticate_sync (EBackend *backend,
				      const ENamedParameters *credentials,
				      gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors,
				      GCancellable *cancellable,
				      GError **error)
{
	ECalBackendHttp *cbhttp;
	ESourceAuthenticationResult result;
	const gchar *uri, *username;
	GError *local_error = NULL;

	cbhttp = E_CAL_BACKEND_HTTP (backend);

	g_free (cbhttp->priv->username);
	cbhttp->priv->username = NULL;

	g_free (cbhttp->priv->password);
	cbhttp->priv->password = g_strdup (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));

	username = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME);
	if (username && *username) {
		cbhttp->priv->username = g_strdup (username);
	}

	uri = cal_backend_http_ensure_uri (cbhttp);
	if (cal_backend_http_load (cbhttp, uri, out_certificate_pem, out_certificate_errors, cancellable, &local_error)) {
		if (!cbhttp->priv->reload_timeout_id) {
			ESource *source = e_backend_get_source (backend);

			cbhttp->priv->reload_timeout_id = e_source_refresh_add_timeout (source, NULL, http_cal_reload_cb, backend, NULL);
		}
	}

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_error (&local_error);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		g_propagate_error (error, local_error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	return result;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_http_init (ECalBackendHttp *cbhttp)
{
	cbhttp->priv = E_CAL_BACKEND_HTTP_GET_PRIVATE (cbhttp);

	g_signal_connect (
		cbhttp, "notify::online",
		G_CALLBACK (e_cal_backend_http_notify_online_cb), NULL);
}

/* Class initialization function for the file backend */
static void
e_cal_backend_http_class_init (ECalBackendHttpClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *sync_class;

	g_type_class_add_private (class, sizeof (ECalBackendHttpPrivate));

	object_class = (GObjectClass *) class;
	backend_class = E_BACKEND_CLASS (class);
	cal_backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	object_class->dispose = e_cal_backend_http_dispose;
	object_class->finalize = e_cal_backend_http_finalize;
	object_class->constructed = e_cal_backend_http_constructed;

	backend_class->authenticate_sync = e_cal_backend_http_authenticate_sync;

	/* Execute one method at a time. */
	cal_backend_class->use_serial_dispatch_queue = TRUE;
	cal_backend_class->get_backend_property = e_cal_backend_http_get_backend_property;
	cal_backend_class->start_view = e_cal_backend_http_start_view;

	sync_class->open_sync = e_cal_backend_http_open;
	sync_class->refresh_sync = e_cal_backend_http_refresh;
	sync_class->create_objects_sync = e_cal_backend_http_create_objects;
	sync_class->modify_objects_sync = e_cal_backend_http_modify_objects;
	sync_class->remove_objects_sync = e_cal_backend_http_remove_objects;
	sync_class->receive_objects_sync = e_cal_backend_http_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_http_send_objects;
	sync_class->get_object_sync = e_cal_backend_http_get_object;
	sync_class->get_object_list_sync = e_cal_backend_http_get_object_list;
	sync_class->add_timezone_sync = e_cal_backend_http_add_timezone;
	sync_class->get_free_busy_sync = e_cal_backend_http_get_free_busy;
}

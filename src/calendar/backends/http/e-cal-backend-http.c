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

#include "evolution-data-server-config.h"

#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>
#include <libedata-cal/libedata-cal.h>

#include "e-cal-backend-http.h"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)

struct _ECalBackendHttpPrivate {
	ESoupSession *session;

	SoupMessage *message;
	gchar *icalstring;
	gchar *last_uri;
	GRecMutex conn_lock;
	GHashTable *components; /* gchar *uid ~> ICalComponent * */
	gint64 hsts_until_time;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendHttp, e_cal_backend_http, E_TYPE_CAL_META_BACKEND)

static gchar *
ecb_http_webcal_to_http_method (const gchar *webcal_str,
				gboolean secure)
{
	if (secure && g_str_has_prefix (webcal_str, "http://"))
		return g_strconcat ("https://", webcal_str + sizeof ("http://") - 1, NULL);

	if (g_str_has_prefix (webcal_str, "webcals://"))
		return g_strconcat ("https://", webcal_str + strlen ("webcals://"), NULL);

	if (!g_str_has_prefix (webcal_str, "webcal://"))
		return g_strdup (webcal_str);

	if (secure)
		return g_strconcat ("https://", webcal_str + sizeof ("webcal://") - 1, NULL);
	else
		return g_strconcat ("http://", webcal_str + sizeof ("webcal://") - 1, NULL);
}

static gchar *
ecb_http_dup_uri (ECalBackendHttp *cbhttp)
{
	ESource *source;
	ESourceSecurity *security_extension;
	ESourceWebdav *webdav_extension;
	GUri *parsed_uri;
	gboolean secure_connection;
	const gchar *extension_name;
	gchar *uri_string, *uri;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp), NULL);

	source = e_backend_get_source (E_BACKEND (cbhttp));

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	security_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	secure_connection = e_source_security_get_secure (security_extension);

	parsed_uri = e_source_webdav_dup_uri (webdav_extension);
	uri_string = g_uri_to_string_partial (parsed_uri, G_URI_HIDE_PASSWORD);
	g_uri_unref (parsed_uri);

	if (!uri_string || !*uri_string) {
		g_free (uri_string);
		return NULL;
	}

	secure_connection = secure_connection || (cbhttp->priv->hsts_until_time && g_get_real_time () <= cbhttp->priv->hsts_until_time);

	uri = ecb_http_webcal_to_http_method (uri_string, secure_connection);

	g_free (uri_string);

	return uri;
}

/* https://tools.ietf.org/html/rfc6797 */
static gint64
ecb_http_extract_hsts_until_time (ECalBackendHttp *cbhttp)
{
	GTlsCertificate *cert = NULL;
	GTlsCertificateFlags cert_errors = 0;
	gint64 hsts_until_time = 0;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (cbhttp), hsts_until_time);
	g_return_val_if_fail (cbhttp->priv->message, hsts_until_time);

	cert = soup_message_get_tls_peer_certificate (cbhttp->priv->message);
	cert_errors = soup_message_get_tls_peer_certificate_errors (cbhttp->priv->message);

	if (soup_message_get_response_headers (cbhttp->priv->message) && cert && !cert_errors) {
		const gchar *hsts_header;

		hsts_header = soup_message_headers_get_one (soup_message_get_response_headers (cbhttp->priv->message), "Strict-Transport-Security");
		if (hsts_header && *hsts_header) {
			GHashTable *params;

			params = soup_header_parse_semi_param_list (hsts_header);
			if (params) {
				const gchar *max_age;

				max_age = g_hash_table_lookup (params, "max-age");

				if (max_age && *max_age) {
					gint64 value;

					if (*max_age == '\"')
						max_age++;

					value = g_ascii_strtoll (max_age, NULL, 10);

					if (value > 0)
						hsts_until_time = g_get_real_time () + (value * G_USEC_PER_SEC);
				}

				soup_header_free_param_list (params);
			}
		}
	}

	return hsts_until_time;
}

static gchar *
ecb_http_read_stream_sync (GInputStream *input_stream,
			   goffset expected_length,
			   GCancellable *cancellable,
			   GError **error)
{
	GString *icalstr;
	void *buffer;
	gsize nread = 0;
	gboolean success = FALSE;

	g_return_val_if_fail (G_IS_INPUT_STREAM (input_stream), NULL);

	icalstr = g_string_sized_new ((expected_length > 0 && expected_length <= 1024 * 1024) ? expected_length + 1 : 1024);

	buffer = g_malloc (16384);

	while (success = g_input_stream_read_all (input_stream, buffer, 16384, &nread, cancellable, error),
	       success && nread > 0) {
		g_string_append_len (icalstr, (const gchar *) buffer, nread);
	}

	g_free (buffer);

	return g_string_free (icalstr, !success);
}

static gboolean
ecb_http_connect_sync (ECalMetaBackend *meta_backend,
		       const ENamedParameters *credentials,
		       ESourceAuthenticationResult *out_auth_result,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors,
		       GCancellable *cancellable,
		       GError **error)
{
	ECalBackendHttp *cbhttp;
	ESource *source;
	SoupMessage *message;
	GInputStream *input_stream = NULL;
	gchar *uri;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	cbhttp = E_CAL_BACKEND_HTTP (meta_backend);

	g_rec_mutex_lock (&cbhttp->priv->conn_lock);

	if (cbhttp->priv->message && cbhttp->priv->icalstring) {
		g_rec_mutex_unlock (&cbhttp->priv->conn_lock);
		return TRUE;
	}

	source = e_backend_get_source (E_BACKEND (meta_backend));

	g_clear_pointer (&cbhttp->priv->icalstring, g_free);
	g_clear_object (&cbhttp->priv->message);

	uri = ecb_http_dup_uri (cbhttp);

	if (!uri || !*uri) {
		g_rec_mutex_unlock (&cbhttp->priv->conn_lock);
		g_free (uri);

		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("URI not set")));
		return FALSE;
	}

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	e_soup_session_set_credentials (cbhttp->priv->session, credentials);

	message = e_soup_session_new_message (cbhttp->priv->session, SOUP_METHOD_GET, uri, &local_error);
	success = message != NULL;

	if (success) {
		gboolean uri_changed;
		gchar *last_etag;

		uri_changed = cbhttp->priv->last_uri && g_strcmp0 (cbhttp->priv->last_uri, uri) != 0;

		if (!cbhttp->priv->last_uri || uri_changed) {
			g_clear_pointer (&cbhttp->priv->last_uri, g_free);
			cbhttp->priv->last_uri = g_strdup (uri);
		}

		if (uri_changed)
			e_cal_meta_backend_set_sync_tag (meta_backend, NULL);

		last_etag = e_cal_meta_backend_dup_sync_tag (meta_backend);

		if (last_etag && *last_etag && !uri_changed)
			soup_message_headers_append (soup_message_get_request_headers (message), "If-None-Match", last_etag);

		g_free (last_etag);

		input_stream = e_soup_session_send_message_sync (cbhttp->priv->session, message, cancellable, &local_error);

		success = input_stream != NULL;

		if (success && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message)) && soup_message_get_status (message) != SOUP_STATUS_NOT_MODIFIED) {
			g_clear_object (&input_stream);
			success = FALSE;
		} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_MODIFIED)) {
			g_clear_object (&input_stream);
			g_clear_error (&local_error);
			success = TRUE;
		}

		if (success) {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
		} else {
			guint status_code;
			gboolean credentials_empty;
			gboolean is_tsl_error;

			if (local_error && local_error->domain == E_SOUP_SESSION_ERROR)
				status_code = local_error->code;
			else
				status_code = soup_message_get_status (message);

			credentials_empty = !credentials || !e_named_parameters_count (credentials);
			is_tsl_error = g_error_matches (local_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE);

			*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR;

			if (status_code == SOUP_STATUS_FORBIDDEN && credentials_empty) {
				*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
			} else if (status_code == SOUP_STATUS_UNAUTHORIZED) {
				if (credentials_empty)
					*out_auth_result = E_SOURCE_AUTHENTICATION_REQUIRED;
				else
					*out_auth_result = E_SOURCE_AUTHENTICATION_REJECTED;
			} else if (local_error) {
				g_propagate_error (error, local_error);
				local_error = NULL;
			} else {
				g_set_error_literal (error, E_SOUP_SESSION_ERROR, status_code,
					soup_message_get_reason_phrase (message) ? soup_message_get_reason_phrase (message) : soup_status_get_phrase (status_code));
			}

			if (is_tsl_error) {
				*out_auth_result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;

				e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_SSL_FAILED);
				e_soup_session_get_ssl_error_details (cbhttp->priv->session, out_certificate_pem, out_certificate_errors);
			} else {
				e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
			}
		}
	} else {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

		g_set_error (error, E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR, _("Malformed URI “%s”: %s"),
			uri, local_error ? local_error->message : _("Unknown error"));
	}

	/* The 'input_stream' can be NULL when the server returned SOUP_STATUS_NOT_MODIFIED */
	if (success && input_stream) {
		cbhttp->priv->icalstring = ecb_http_read_stream_sync (input_stream,
			soup_message_headers_get_content_length (soup_message_get_response_headers (message)), cancellable, error);
		success =  cbhttp->priv->icalstring != NULL;
	}

	if (success) {
		cbhttp->priv->message = message;
		cbhttp->priv->hsts_until_time = ecb_http_extract_hsts_until_time (cbhttp);

		*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else {
		g_clear_object (&message);

		if (*out_auth_result != E_SOURCE_AUTHENTICATION_REQUIRED &&
		    *out_auth_result != E_SOURCE_AUTHENTICATION_REJECTED)
			cbhttp->priv->hsts_until_time = 0;
	}

	g_rec_mutex_unlock (&cbhttp->priv->conn_lock);
	g_clear_object (&input_stream);
	g_clear_error (&local_error);
	g_free (uri);

	return success;
}

static gboolean
ecb_http_disconnect_sync (ECalMetaBackend *meta_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalBackendHttp *cbhttp;
	ESource *source;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (meta_backend), FALSE);

	cbhttp = E_CAL_BACKEND_HTTP (meta_backend);

	g_rec_mutex_lock (&cbhttp->priv->conn_lock);

	g_clear_pointer (&cbhttp->priv->icalstring, g_free);
	g_clear_object (&cbhttp->priv->message);

	if (cbhttp->priv->session)
		soup_session_abort (SOUP_SESSION (cbhttp->priv->session));

	g_clear_pointer (&cbhttp->priv->components, g_hash_table_destroy);

	g_rec_mutex_unlock (&cbhttp->priv->conn_lock);

	source = e_backend_get_source (E_BACKEND (meta_backend));
	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

	return TRUE;
}

static gboolean
ecb_http_get_changes_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendHttp *cbhttp;
	ICalCompIter *iter = NULL;
	ICalComponent *maincomp, *subcomp;
	ICalComponentKind backend_kind;
	GHashTable *components = NULL;
	const gchar *new_etag;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag != NULL, FALSE);
	g_return_val_if_fail (out_created_objects != NULL, FALSE);
	g_return_val_if_fail (out_modified_objects != NULL, FALSE);
	g_return_val_if_fail (out_removed_objects != NULL, FALSE);

	cbhttp = E_CAL_BACKEND_HTTP (meta_backend);
	backend_kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

	g_rec_mutex_lock (&cbhttp->priv->conn_lock);

	if (!cbhttp->priv->message) {
		g_rec_mutex_unlock (&cbhttp->priv->conn_lock);
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE));
		return FALSE;
	}

	if (soup_message_get_status (cbhttp->priv->message) == SOUP_STATUS_NOT_MODIFIED) {
		g_rec_mutex_unlock (&cbhttp->priv->conn_lock);

		ecb_http_disconnect_sync (meta_backend, cancellable, NULL);

		return TRUE;
	}

	g_warn_if_fail (cbhttp->priv->icalstring != NULL);

	new_etag = soup_message_headers_get_one (soup_message_get_response_headers (cbhttp->priv->message), "ETag");
	if (new_etag && !*new_etag) {
		new_etag = NULL;
	} else if (new_etag && g_strcmp0 (last_sync_tag, new_etag) == 0) {
		g_rec_mutex_unlock (&cbhttp->priv->conn_lock);

		/* Nothing changed */
		ecb_http_disconnect_sync (meta_backend, cancellable, NULL);

		return TRUE;
	}

	*out_new_sync_tag = g_strdup (new_etag);

	g_rec_mutex_unlock (&cbhttp->priv->conn_lock);

	/* Skip the UTF-8 marker at the beginning of the string */
	if (((guchar) cbhttp->priv->icalstring[0]) == 0xEF &&
	    ((guchar) cbhttp->priv->icalstring[1]) == 0xBB &&
	    ((guchar) cbhttp->priv->icalstring[2]) == 0xBF)
		maincomp = i_cal_parser_parse_string (cbhttp->priv->icalstring + 3);
	else
		maincomp = i_cal_parser_parse_string (cbhttp->priv->icalstring);

	if (!maincomp) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Bad file format."));
		e_cal_meta_backend_empty_cache_sync (meta_backend, cancellable, NULL);
		ecb_http_disconnect_sync (meta_backend, cancellable, NULL);
		return FALSE;
	}

	if (i_cal_component_isa (maincomp) != I_CAL_VCALENDAR_COMPONENT &&
	    i_cal_component_isa (maincomp) != I_CAL_XROOT_COMPONENT) {
		g_object_unref (maincomp);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Not a calendar."));
		e_cal_meta_backend_empty_cache_sync (meta_backend, cancellable, NULL);
		ecb_http_disconnect_sync (meta_backend, cancellable, NULL);
		return FALSE;
	}

	if (i_cal_component_isa (maincomp) == I_CAL_VCALENDAR_COMPONENT) {
		subcomp = g_object_ref (maincomp);
	} else {
		iter = i_cal_component_begin_component (maincomp, I_CAL_VCALENDAR_COMPONENT);
		subcomp = i_cal_comp_iter_deref (iter);
		if (subcomp)
			i_cal_object_set_owner (I_CAL_OBJECT (subcomp), G_OBJECT (maincomp));
	}

	while (subcomp && success) {
		ICalComponent *next_subcomp = NULL;

		if (iter) {
			next_subcomp = i_cal_comp_iter_next (iter);
			if (next_subcomp)
				i_cal_object_set_owner (I_CAL_OBJECT (next_subcomp), G_OBJECT (maincomp));
		}

		if (i_cal_component_isa (subcomp) == I_CAL_VCALENDAR_COMPONENT) {
			success = e_cal_meta_backend_gather_timezones_sync (meta_backend, subcomp, TRUE, cancellable, error);
			if (success) {
				ICalComponent *icomp;

				while (icomp = i_cal_component_get_first_component (subcomp, backend_kind), icomp) {
					ICalComponent *existing_icomp;
					gpointer orig_key, orig_value;
					const gchar *uid;

					i_cal_component_remove_component (subcomp, icomp);

					if (!components)
						components = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

					if (!e_cal_util_component_has_property (icomp, I_CAL_UID_PROPERTY)) {
						gchar *new_uid = e_util_generate_uid ();
						i_cal_component_set_uid (icomp, new_uid);
						g_free (new_uid);
					}

					uid = i_cal_component_get_uid (icomp);

					if (!g_hash_table_lookup_extended (components, uid, &orig_key, &orig_value)) {
						orig_key = NULL;
						orig_value = NULL;
					}

					existing_icomp = orig_value;
					if (existing_icomp) {
						if (i_cal_component_isa (existing_icomp) != I_CAL_VCALENDAR_COMPONENT) {
							ICalComponent *vcal;

							vcal = e_cal_util_new_top_level ();

							g_warn_if_fail (g_hash_table_steal (components, uid));

							i_cal_component_take_component (vcal, existing_icomp);
							g_hash_table_insert (components, g_strdup (uid), vcal);

							g_free (orig_key);

							existing_icomp = vcal;
						}

						i_cal_component_take_component (existing_icomp, icomp);
					} else {
						g_hash_table_insert (components, g_strdup (uid), icomp);
					}
				}
			}
		}

		g_object_unref (subcomp);
		subcomp = next_subcomp;
	}

	g_clear_object (&subcomp);
	g_clear_object (&iter);

	if (components) {
		g_warn_if_fail (cbhttp->priv->components == NULL);
		cbhttp->priv->components = components;

		g_object_unref (maincomp);

		success = E_CAL_META_BACKEND_CLASS (e_cal_backend_http_parent_class)->get_changes_sync (meta_backend,
			last_sync_tag, is_repeat, out_new_sync_tag, out_repeat, out_created_objects,
			out_modified_objects, out_removed_objects, cancellable, error);
	} else {
		g_object_unref (maincomp);
	}

	/* Always disconnect, to free the resources needed to download the iCalendar file */
	ecb_http_disconnect_sync (meta_backend, cancellable, NULL);

	return success;
}

static gboolean
ecb_http_list_existing_sync (ECalMetaBackend *meta_backend,
			     gchar **out_new_sync_tag,
			     GSList **out_existing_objects, /* ECalMetaBackendInfo * */
			     GCancellable *cancellable,
			     GError **error)
{
	ECalBackendHttp *cbhttp;
	ECalCache *cal_cache;
	ICalComponentKind kind;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	cbhttp = E_CAL_BACKEND_HTTP (meta_backend);

	*out_existing_objects = NULL;

	g_return_val_if_fail (cbhttp->priv->components != NULL, FALSE);

	cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
	g_return_val_if_fail (cal_cache != NULL, FALSE);

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (meta_backend));

	g_hash_table_iter_init (&iter, cbhttp->priv->components);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		ICalComponent *icomp = value;
		ECalMetaBackendInfo *nfo;
		const gchar *uid;
		gchar *revision, *object;

		if (icomp && i_cal_component_isa (icomp) == I_CAL_VCALENDAR_COMPONENT)
			icomp = i_cal_component_get_first_component (icomp, kind);
		else if (icomp)
			icomp = g_object_ref (icomp);

		if (!icomp)
			continue;

		uid = i_cal_component_get_uid (icomp);
		revision = e_cal_cache_dup_component_revision (cal_cache, icomp);
		object = i_cal_component_as_ical_string (value);

		nfo = e_cal_meta_backend_info_new (uid, revision, object, NULL);

		*out_existing_objects = g_slist_prepend (*out_existing_objects, nfo);

		g_object_unref (icomp);
		g_free (revision);
		g_free (object);
	}

	g_object_unref (cal_cache);

	ecb_http_disconnect_sync (meta_backend, cancellable, NULL);

	return TRUE;
}

static gboolean
ecb_http_load_component_sync (ECalMetaBackend *meta_backend,
			      const gchar *uid,
			      const gchar *extra,
			      ICalComponent **out_component,
			      gchar **out_extra,
			      GCancellable *cancellable,
			      GError **error)
{
	ECalBackendHttp *cbhttp;
	gpointer key = NULL, value = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_HTTP (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);

	cbhttp = E_CAL_BACKEND_HTTP (meta_backend);
	g_return_val_if_fail (cbhttp->priv->components != NULL, FALSE);

	if (!cbhttp->priv->components ||
	    !g_hash_table_contains (cbhttp->priv->components, uid)) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
		return FALSE;
	}

	g_warn_if_fail (g_hash_table_lookup_extended (cbhttp->priv->components, uid, &key, &value));
	g_warn_if_fail (g_hash_table_steal (cbhttp->priv->components, uid));

	*out_component = value;

	g_free (key);

	if (!g_hash_table_size (cbhttp->priv->components)) {
		g_hash_table_destroy (cbhttp->priv->components);
		cbhttp->priv->components = NULL;

		ecb_http_disconnect_sync (meta_backend, cancellable, NULL);
	}

	return value != NULL;
}

static void
e_cal_backend_http_constructed (GObject *object)
{
	ECalBackendHttp *cbhttp = E_CAL_BACKEND_HTTP (object);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->constructed (object);

	cbhttp->priv->session = e_soup_session_new (e_backend_get_source (E_BACKEND (cbhttp)));

	e_soup_session_setup_logging (cbhttp->priv->session, g_getenv ("WEBCAL_DEBUG"));

	e_binding_bind_property (
		cbhttp, "proxy-resolver",
		cbhttp->priv->session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);
}

static void
e_cal_backend_http_dispose (GObject *object)
{
	ECalBackendHttp *cbhttp;

	cbhttp = E_CAL_BACKEND_HTTP (object);

	g_rec_mutex_lock (&cbhttp->priv->conn_lock);

	g_clear_object (&cbhttp->priv->message);
	g_clear_pointer (&cbhttp->priv->icalstring, g_free);
	g_clear_pointer (&cbhttp->priv->last_uri, g_free);

	if (cbhttp->priv->session)
		soup_session_abort (SOUP_SESSION (cbhttp->priv->session));

	g_clear_pointer (&cbhttp->priv->components, g_hash_table_destroy);

	g_rec_mutex_unlock (&cbhttp->priv->conn_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->dispose (object);
}

static void
e_cal_backend_http_finalize (GObject *object)
{
	ECalBackendHttp *cbhttp = E_CAL_BACKEND_HTTP (object);

	g_clear_object (&cbhttp->priv->session);
	g_rec_mutex_clear (&cbhttp->priv->conn_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_http_parent_class)->finalize (object);
}

static void
e_cal_backend_http_init (ECalBackendHttp *cbhttp)
{
	cbhttp->priv = e_cal_backend_http_get_instance_private (cbhttp);

	g_rec_mutex_init (&cbhttp->priv->conn_lock);

	e_cal_backend_set_writable (E_CAL_BACKEND (cbhttp), FALSE);
}

static void
e_cal_backend_http_class_init (ECalBackendHttpClass *klass)
{
	GObjectClass *object_class;
	ECalBackendSyncClass *cal_backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_http_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_http_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_http_get_changes_sync;
	cal_meta_backend_class->list_existing_sync = ecb_http_list_existing_sync;
	cal_meta_backend_class->load_component_sync = ecb_http_load_component_sync;

	/* Setting these methods to NULL will cause "Not supported" error,
	   which is more accurate than "Permission denied" error */
	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->create_objects_sync = NULL;
	cal_backend_sync_class->modify_objects_sync = NULL;
	cal_backend_sync_class->remove_objects_sync = NULL;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_cal_backend_http_constructed;
	object_class->dispose = e_cal_backend_http_dispose;
	object_class->finalize = e_cal_backend_http_finalize;
}

/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Christian Kellner <gicmo@gnome.org>
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-backend-caldav.h"

#define E_CALDAV_MAX_MULTIGET_AMOUNT 100 /* what's the maximum count of items to fetch within a multiget request */

#define E_CALDAV_X_ETAG "X-EVOLUTION-CALDAV-ETAG"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define ECC_ERROR(_code) e_cal_client_error_create (_code, NULL)
#define ECC_ERROR_EX(_code, _msg) e_cal_client_error_create (_code, _msg)

struct _ECalBackendCalDAVPrivate {
	/* The main WebDAV session  */
	EWebDAVSession *webdav;
	GUri *last_uri;
	GMutex webdav_lock;

	/* If already been connected, then the connect_sync() will relax server checks,
	   to avoid unnecessary requests towards the server. */
	gboolean been_connected;

	/* support for 'getctag' extension */
	gboolean ctag_supported;

	/* TRUE when 'calendar-schedule' supported on the server */
	gboolean calendar_schedule;
	/* TRUE when 'calendar-auto-schedule' supported on the server */
	gboolean calendar_auto_schedule;
	/* with 'calendar-schedule' supported, here's an outbox url
	 * for queries of free/busy information */
	gchar *schedule_outbox_url;

	/* "Temporary hack" to indicate it's talking to a google calendar.
	 * The proper solution should be to subclass whole backend and change only
	 * necessary parts in it, but this will give us more freedom, as also direct
	 * caldav calendars can profit from this. */
	gboolean is_google;

	/* The iCloud.com requires timezone IDs as locations */
	gboolean is_icloud;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalBackendCalDAV, e_cal_backend_caldav, E_TYPE_CAL_META_BACKEND)

static gchar *ecb_caldav_get_backend_property (ECalBackend *backend, const gchar *prop_name);

static EWebDAVSession *
ecb_caldav_ref_session (ECalBackendCalDAV *cbdav)
{
	EWebDAVSession *webdav;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), NULL);

	g_mutex_lock (&cbdav->priv->webdav_lock);
	if (cbdav->priv->webdav)
		webdav = g_object_ref (cbdav->priv->webdav);
	else
		webdav = NULL;
	g_mutex_unlock (&cbdav->priv->webdav_lock);

	return webdav;
}

static void
ecb_caldav_update_tweaks (ECalBackendCalDAV *cbdav)
{
	ESource *source;
	GUri *parsed_uri;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	source = e_backend_get_source (E_BACKEND (cbdav));

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND))
		return;

	parsed_uri = e_source_webdav_dup_uri (e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND));
	if (!parsed_uri)
		return;

	cbdav->priv->is_google = g_uri_get_host (parsed_uri) && (
		e_util_host_is_in_domain (g_uri_get_host (parsed_uri), "google.com") ||
		e_util_host_is_in_domain (g_uri_get_host (parsed_uri), "googleapis.com") ||
		e_util_host_is_in_domain (g_uri_get_host (parsed_uri), "googleusercontent.com"));

	cbdav->priv->is_icloud = g_uri_get_host (parsed_uri) &&
		e_util_host_is_in_domain (g_uri_get_host (parsed_uri), "icloud.com");

	g_uri_unref (parsed_uri);
}

static gboolean
ecb_caldav_connect_sync (ECalMetaBackend *meta_backend,
			 const ENamedParameters *credentials,
			 ESourceAuthenticationResult *out_auth_result,
			 gchar **out_certificate_pem,
			 GTlsCertificateFlags *out_certificate_errors,
			 GCancellable *cancellable,
			 GError **error)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	GHashTable *capabilities = NULL, *allows = NULL;
	ESource *source;
	gboolean success, is_writable = FALSE, uri_changed = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);
	source = e_backend_get_source (E_BACKEND (meta_backend));

	g_mutex_lock (&cbdav->priv->webdav_lock);
	if (cbdav->priv->webdav) {
		g_mutex_unlock (&cbdav->priv->webdav_lock);
		return TRUE;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
		ESourceWebdav *webdav_extension;
		GUri *current_uri;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		current_uri = e_source_webdav_dup_uri (webdav_extension);

		#define uri_str_equal(_func) (g_strcmp0 (_func (cbdav->priv->last_uri), _func (current_uri)) == 0)

		uri_changed = cbdav->priv->last_uri && current_uri && (
			g_uri_get_port (cbdav->priv->last_uri) != g_uri_get_port (current_uri) ||
			!uri_str_equal (g_uri_get_auth_params) ||
			!uri_str_equal (g_uri_get_host) ||
			!uri_str_equal (g_uri_get_path) ||
			!uri_str_equal (g_uri_get_query) ||
			!uri_str_equal (g_uri_get_fragment) ||
			!uri_str_equal (g_uri_get_scheme) ||
			!uri_str_equal (g_uri_get_userinfo) ||
			!uri_str_equal (g_uri_get_user) ||
			!uri_str_equal (g_uri_get_password));

		#undef uri_str_equal

		if (!cbdav->priv->last_uri || uri_changed) {
			g_clear_pointer (&cbdav->priv->last_uri, g_uri_unref);
			cbdav->priv->last_uri = current_uri;
		} else if (current_uri) {
			g_uri_unref (current_uri);
		}
	}
	g_mutex_unlock (&cbdav->priv->webdav_lock);

	if (uri_changed)
		e_cal_meta_backend_set_sync_tag (meta_backend, NULL);

	webdav = e_webdav_session_new (source);

	e_soup_session_setup_logging (E_SOUP_SESSION (webdav), g_getenv ("CALDAV_DEBUG"));

	e_binding_bind_property (
		cbdav, "proxy-resolver",
		webdav, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	e_soup_session_set_credentials (E_SOUP_SESSION (webdav), credentials);

	if (cbdav->priv->been_connected) {
		g_mutex_lock (&cbdav->priv->webdav_lock);
		cbdav->priv->webdav = webdav;
		g_mutex_unlock (&cbdav->priv->webdav_lock);

		return TRUE;
	}

	/* Thinks the 'getctag' extension is available the first time, but unset it when realizes it isn't. */
	cbdav->priv->ctag_supported = TRUE;

	success = e_webdav_session_options_sync (webdav, NULL,
		&capabilities, &allows, cancellable, &local_error);

	if (success && !g_cancellable_is_cancelled (cancellable)) {
		GSList *privileges = NULL, *link;

		/* Ignore any errors here */
		if (e_webdav_session_get_current_user_privilege_set_full_sync (webdav, NULL, &privileges,
			capabilities ? NULL : &capabilities,
			allows ? NULL : &allows, cancellable, NULL)) {
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
				g_hash_table_contains (allows, "PUT") ||
				g_hash_table_contains (allows, "POST") ||
				g_hash_table_contains (allows, "DELETE"));
		}
	}

	if (success) {
		ESourceWebdav *webdav_extension;
		GUri *parsed_uri;
		gboolean calendar_access, calendar_schedule, calendar_auto_schedule;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		parsed_uri = e_source_webdav_dup_uri (webdav_extension);

		calendar_auto_schedule = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav)) != I_CAL_VJOURNAL_COMPONENT &&
			capabilities && g_hash_table_contains (capabilities, E_WEBDAV_CAPABILITY_CALENDAR_AUTO_SCHEDULE);
		calendar_schedule = calendar_auto_schedule ||
			(e_cal_backend_get_kind (E_CAL_BACKEND (cbdav)) != I_CAL_VJOURNAL_COMPONENT &&
			(!capabilities || g_hash_table_contains (capabilities, E_WEBDAV_CAPABILITY_CALENDAR_SCHEDULE)));
		calendar_access = capabilities && g_hash_table_contains (capabilities, E_WEBDAV_CAPABILITY_CALENDAR_ACCESS);

		if (calendar_access) {
			if ((!calendar_schedule) != (!cbdav->priv->calendar_schedule) ||
			    (!calendar_auto_schedule) != (!cbdav->priv->calendar_auto_schedule)) {
				ECalBackend *cal_backend = E_CAL_BACKEND (cbdav);
				gchar *value;

				cbdav->priv->calendar_schedule = calendar_schedule;
				cbdav->priv->calendar_auto_schedule = calendar_auto_schedule;

				value = ecb_caldav_get_backend_property (cal_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES);
				e_cal_backend_notify_property_changed (cal_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES, value);
				g_free (value);
			}

			e_cal_backend_set_writable (E_CAL_BACKEND (cbdav), is_writable);

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);

			ecb_caldav_update_tweaks (cbdav);
		} else {
			gchar *uri;

			uri = g_uri_to_string_partial (parsed_uri, G_URI_HIDE_PASSWORD);

			success = FALSE;
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("Given URL “%s” doesn’t reference CalDAV calendar"), uri);

			g_free (uri);

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
		}

		g_uri_unref (parsed_uri);
	}

	if (success) {
		gchar *ctag = NULL;

		/* Some servers, notably Google, allow OPTIONS when not
		   authorized (aka without credentials), thus try something
		   more aggressive, just in case.

		   The 'getctag' extension is not required, thus check
		   for unauthorized error only. */
		if (!e_webdav_session_getctag_sync (webdav, NULL, &ctag, cancellable, &local_error) &&
		    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			success = FALSE;
		} else {
			g_clear_error (&local_error);
		}
		g_free (ctag);
	}

	if (success) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
	} else {
		e_soup_session_handle_authentication_failure (E_SOUP_SESSION (webdav), credentials,
			local_error, out_auth_result, out_certificate_pem, out_certificate_errors, error);
	}

	g_clear_error (&local_error);

	if (capabilities)
		g_hash_table_destroy (capabilities);
	if (allows)
		g_hash_table_destroy (allows);

	if (success && !g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_mutex_lock (&cbdav->priv->webdav_lock);
		cbdav->priv->webdav = webdav;
		g_mutex_unlock (&cbdav->priv->webdav_lock);
		cbdav->priv->been_connected = TRUE;
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
ecb_caldav_disconnect_sync (ECalMetaBackend *meta_backend,
			    GCancellable *cancellable,
			    GError **error)
{
	ECalBackendCalDAV *cbdav;
	ESource *source;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);

	g_mutex_lock (&cbdav->priv->webdav_lock);
	if (cbdav->priv->webdav)
		soup_session_abort (SOUP_SESSION (cbdav->priv->webdav));

	g_clear_object (&cbdav->priv->webdav);
	g_mutex_unlock (&cbdav->priv->webdav_lock);

	source = e_backend_get_source (E_BACKEND (meta_backend));
	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

	return TRUE;
}

static const gchar *
ecb_caldav_get_vcalendar_uid (ICalComponent *vcalendar)
{
	const gchar *uid = NULL;
	ICalComponent *subcomp;

	g_return_val_if_fail (vcalendar != NULL, NULL);
	g_return_val_if_fail (i_cal_component_isa (vcalendar) == I_CAL_VCALENDAR_COMPONENT, NULL);

	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp && !uid;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {
		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VJOURNAL_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			uid = i_cal_component_get_uid (subcomp);
			if (uid && !*uid)
				uid = NULL;
		}
	}

	g_clear_object (&subcomp);

	return uid;
}

static void
ecb_caldav_update_nfo_with_vcalendar (ECalMetaBackendInfo *nfo,
				      ICalComponent *vcalendar,
				      const gchar *etag)
{
	ICalComponent *subcomp;
	const gchar *uid;

	g_return_if_fail (nfo != NULL);
	g_return_if_fail (vcalendar != NULL);

	uid = ecb_caldav_get_vcalendar_uid (vcalendar);

	if (!etag || !*etag)
		etag = nfo->revision;

	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {
		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VJOURNAL_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			e_cal_util_component_set_x_property (subcomp, E_CALDAV_X_ETAG, etag);
		}
	}

	g_warn_if_fail (nfo->object == NULL);
	nfo->object = i_cal_component_as_ical_string (vcalendar);

	if (!nfo->uid || !*(nfo->uid)) {
		g_free (nfo->uid);
		nfo->uid = g_strdup (uid);
	}

	if (g_strcmp0 (etag, nfo->revision) != 0) {
		gchar *copy = g_strdup (etag);

		g_free (nfo->revision);
		nfo->revision = copy;
	}
}

typedef struct _MultigetData {
	GSList *from_link;
	GSList **out_removed_objects;
} MultigetData;

static gboolean
ecb_caldav_multiget_response_cb (EWebDAVSession *webdav,
				 xmlNodePtr prop_node,
				 const GUri *request_uri,
				 const gchar *href,
				 guint status_code,
				 gpointer user_data)
{
	MultigetData *md = user_data;

	g_return_val_if_fail (md != NULL, FALSE);
	g_return_val_if_fail (md->from_link != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		const xmlChar *calendar_data, *etag;
		xmlNodePtr calendar_data_node = NULL, etag_node = NULL;

		g_return_val_if_fail (href != NULL, FALSE);

		e_xml_find_children_nodes (prop_node, 2,
			E_WEBDAV_NS_CALDAV, "calendar-data", &calendar_data_node,
			E_WEBDAV_NS_DAV, "getetag", &etag_node);

		calendar_data = e_xml_get_node_text (calendar_data_node);
		etag = e_xml_get_node_text (etag_node);

		if (calendar_data) {
			ICalComponent *vcalendar;

			vcalendar = i_cal_component_new_from_string ((const gchar *) calendar_data);
			if (vcalendar) {
				const gchar *uid;

				uid = ecb_caldav_get_vcalendar_uid (vcalendar);
				if (uid) {
					gchar *dequoted_etag;
					GSList *link;

					dequoted_etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) etag));

					for (link = md->from_link; link; link = g_slist_next (link)) {
						ECalMetaBackendInfo *nfo = link->data;

						if (!nfo)
							continue;

						if (e_webdav_session_util_item_href_equal (nfo->extra, href)) {
							/* If the server returns data in the same order as it had been requested,
							   then this speeds up lookup for the matching object. */
							if (link == md->from_link)
								md->from_link = g_slist_next (md->from_link);

							ecb_caldav_update_nfo_with_vcalendar (nfo, vcalendar, dequoted_etag);

							break;
						}
					}

					if (!link && e_soup_session_get_log_level (E_SOUP_SESSION (webdav)) != SOUP_LOGGER_LOG_NONE)
						e_util_debug_print ("CalDAV", "Failed to find item with href '%s' in known server items\n", href);

					g_free (dequoted_etag);
				}

				g_object_unref (vcalendar);
			}
		}
	} else if (status_code == SOUP_STATUS_NOT_FOUND) {
		GSList *link;

		g_return_val_if_fail (href != NULL, FALSE);

		for (link = md->from_link; link; link = g_slist_next (link)) {
			ECalMetaBackendInfo *nfo = link->data;

			if (!nfo)
				continue;

			if (e_webdav_session_util_item_href_equal (nfo->extra, href)) {
				/* If the server returns data in the same order as it had been requested,
				   then this speeds up lookup for the matching object. */
				if (link == md->from_link)
					md->from_link = g_slist_next (md->from_link);

				if (md->out_removed_objects)
					*md->out_removed_objects = g_slist_prepend (*md->out_removed_objects, nfo);
				else
					e_cal_meta_backend_info_free (nfo);

				link->data = NULL;

				break;
			}
		}
	}

	return TRUE;
}

static gboolean
ecb_caldav_multiget_from_sets_sync (ECalBackendCalDAV *cbdav,
				    EWebDAVSession *webdav,
				    GSList **in_link,
				    GSList **set2,
				    GSList **out_removed_objects,
				    GCancellable *cancellable,
				    GError **error)
{
	EXmlDocument *xml;
	gint left_to_go = E_CALDAV_MAX_MULTIGET_AMOUNT;
	GSList *link;
	gboolean success = TRUE;

	g_return_val_if_fail (in_link != NULL, FALSE);
	g_return_val_if_fail (*in_link != NULL, FALSE);
	g_return_val_if_fail (set2 != NULL, FALSE);

	xml = e_xml_document_new (E_WEBDAV_NS_CALDAV, "calendar-multiget");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "getetag");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "calendar-data");
	e_xml_document_end_element (xml); /* prop */

	link = *in_link;

	while (link && left_to_go > 0) {
		GSList *nfo_link = link;
		ECalMetaBackendInfo *nfo = nfo_link->data;

		link = g_slist_next (link);
		if (!link) {
			link = *set2;
			*set2 = NULL;
			/* read created and modified sets separately, because md.from_link
			   does not handle move between the sets */
			left_to_go = 1;
		}

		if (!nfo)
			continue;

		left_to_go--;

		/* iCloud returns broken calendar-multiget responses, with
		   empty <DAV:href> elements, thus read one-by-one for it.
		   This is confirmed as of 2017-04-11. */
		if (cbdav->priv->is_icloud) {
			gchar *calendar_data = NULL, *etag = NULL;

			success = FALSE;

			/* iCloud returns '@' escaped as "%40", but it doesn't accept it in GET,
			   thus try to unescape it, together with some other characters */
			if (nfo->extra && strchr (nfo->extra, '%')) {
				GUri *suri;
				gchar *new_uri = NULL;

				suri = g_uri_parse (nfo->extra, SOUP_HTTP_URI_FLAGS, NULL);

				if (suri) {
					const gchar *path;
					gchar *unesc;
					gchar *esc;

					path = g_uri_get_path (suri);
					unesc = g_uri_unescape_string (path, NULL);

					/* now re-escape the string but allowing a @ */
					esc = g_uri_escape_string (unesc, "@", FALSE);

					e_util_change_uri_component (&suri, SOUP_URI_PATH, esc);

					g_free (unesc);
					g_free (esc);

					new_uri = g_uri_to_string_partial (suri, G_URI_HIDE_PASSWORD);
					g_uri_unref (suri);
				}

				if (new_uri) {
					success = e_webdav_session_get_data_sync (webdav, new_uri, NULL, &etag, NULL, &calendar_data, NULL, cancellable, NULL);

					if (success) {
						/* Remember the corrected URI */
						g_free (nfo->extra);
						nfo->extra = new_uri;
						new_uri = NULL;
					}
				}

				g_free (new_uri);
			}

			if (!success) {
				GError *local_error = NULL;

				success = e_webdav_session_get_data_sync (webdav, nfo->extra, NULL, &etag, NULL, &calendar_data, NULL, cancellable, &local_error);

				if (!success && g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
					if (out_removed_objects)
						*out_removed_objects = g_slist_prepend (*out_removed_objects, nfo);
					else
						e_cal_meta_backend_info_free (nfo);

					nfo_link->data = NULL;
					g_clear_error (&local_error);
					continue;
				} else if (local_error) {
					g_propagate_error (error, local_error);
				}
			}

			if (success && calendar_data) {
				ICalComponent *vcalendar;

				vcalendar = i_cal_component_new_from_string (calendar_data);
				if (vcalendar) {
					ecb_caldav_update_nfo_with_vcalendar (nfo, vcalendar, etag);
					g_object_unref (vcalendar);
				}
			}

			g_free (calendar_data);
			g_free (etag);

			if (!success)
				break;
		} else {
			GUri *suri;
			gchar *path = NULL;

			suri = g_uri_parse (nfo->extra, SOUP_HTTP_URI_FLAGS, NULL);

			if (suri) {
				const gchar *upath, *uquery;

				upath = g_uri_get_path (suri);
				if (!*upath) upath = "/";
				uquery = g_uri_get_query (suri);

				if (uquery)
					path = g_strdup_printf ("%s?%s", upath, uquery);
				else
					path = g_strdup (upath);

				g_uri_unref (suri);
			}

			e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "href");
			e_xml_document_write_string (xml, path ? path : nfo->extra);
			e_xml_document_end_element (xml); /* href */

			g_free (path);
		}
	}

	if (left_to_go != E_CALDAV_MAX_MULTIGET_AMOUNT &&
	    !cbdav->priv->is_icloud && success) {
		MultigetData md;

		md.from_link = *in_link;
		md.out_removed_objects = out_removed_objects;

		success = e_webdav_session_report_sync (webdav, NULL, NULL, xml,
			ecb_caldav_multiget_response_cb, &md, NULL, NULL, cancellable, error);
	}

	g_object_unref (xml);

	*in_link = link;

	return success;
}

static gboolean
ecb_caldav_get_calendar_items_cb (EWebDAVSession *webdav,
				  xmlNodePtr prop_node,
				  const GUri *request_uri,
				  const gchar *href,
				  guint status_code,
				  gpointer user_data)
{
	GHashTable *known_items = user_data; /* gchar *href ~> ECalMetaBackendInfo * */
	ECalMetaBackendInfo *nfo;
	gchar *etag;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (known_items != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;

	g_return_val_if_fail (href != NULL, FALSE);

	/* Skip collection resource, if returned by the server (like iCloud.com does) */
	if (g_str_has_suffix (href, "/") ||
	    (request_uri && *g_uri_get_path ((GUri *) request_uri) && g_str_has_suffix (href, g_uri_get_path ((GUri *) request_uri))))
		return TRUE;

	etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) e_xml_find_child_and_get_text (prop_node, E_WEBDAV_NS_DAV, "getetag")));
	/* Return 'TRUE' to not stop on faulty data from the server */
	g_return_val_if_fail (etag != NULL, TRUE);

	/* UID is unknown at this moment */
	nfo = e_cal_meta_backend_info_new ("", etag, NULL, href);

	g_free (etag);
	g_return_val_if_fail (nfo != NULL, FALSE);

	g_hash_table_insert (known_items, g_strdup (href), nfo);

	return TRUE;
}

typedef struct _CalDAVChangesData {
	gboolean is_repeat;
	GSList **out_modified_objects;
	GSList **out_removed_objects;
	GHashTable *known_items; /* gchar *href ~> ECalMetaBackendInfo * */
} CalDAVChangesData;

static gboolean
ecb_caldav_search_changes_cb (ECalCache *cal_cache,
			      const gchar *uid,
			      const gchar *rid,
			      const gchar *revision,
			      const gchar *object,
			      const gchar *extra,
			      guint32 custom_flags,
			      EOfflineState offline_state,
			      gpointer user_data)
{
	CalDAVChangesData *ccd = user_data;

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
		} else if (ccd->is_repeat) {
			*(ccd->out_removed_objects) = g_slist_prepend (*(ccd->out_removed_objects),
				e_cal_meta_backend_info_new (uid, revision, object, extra));
		}
	}

	return TRUE;
}

static void
ecb_caldav_check_credentials_error (ECalBackendCalDAV *cbdav,
				    EWebDAVSession *webdav,
				    GError *op_error)
{
	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	if (g_error_matches (op_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE) && webdav) {
		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_TLS_NOT_AVAILABLE;
	} else if (g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		   g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN)) {
		gboolean was_forbidden = g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN);

		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_AUTHENTICATION_REQUIRED;

		cbdav->priv->been_connected = FALSE;

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
ecb_caldav_get_changes_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	EXmlDocument *xml;
	GHashTable *known_items; /* gchar *href ~> ECalMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_repeat, FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_created_objects, FALSE);
	g_return_val_if_fail (out_modified_objects, FALSE);
	g_return_val_if_fail (out_removed_objects, FALSE);

	*out_new_sync_tag = NULL;
	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);
	webdav = ecb_caldav_ref_session (cbdav);

	if (cbdav->priv->ctag_supported) {
		gchar *new_sync_tag = NULL;

		success = e_webdav_session_getctag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL);
		if (!success) {
			cbdav->priv->ctag_supported = g_cancellable_set_error_if_cancelled (cancellable, error);
			if (cbdav->priv->ctag_supported || !webdav) {
				g_clear_object (&webdav);
				return FALSE;
			}
		} else if (!is_repeat && new_sync_tag && last_sync_tag && g_strcmp0 (last_sync_tag, new_sync_tag) == 0) {
			*out_new_sync_tag = new_sync_tag;
			g_clear_object (&webdav);
			return TRUE;
		}

		/* Do not advertise the new ctag in the first go, otherwise a failure
		   in the second go might hide some events. */
		if (is_repeat)
			*out_new_sync_tag = new_sync_tag;
		else
			g_free (new_sync_tag);
	}

	xml = e_xml_document_new (E_WEBDAV_NS_CALDAV, "calendar-query");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "getetag");
	e_xml_document_end_element (xml); /* prop */

	e_xml_document_start_element (xml, NULL, "filter");
	e_xml_document_start_element (xml, NULL, "comp-filter");
	e_xml_document_add_attribute (xml, NULL, "name", "VCALENDAR");
	e_xml_document_start_element (xml, NULL, "comp-filter");

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
	default:
	case I_CAL_VEVENT_COMPONENT:
		e_xml_document_add_attribute (xml, NULL, "name", "VEVENT");
		break;
	case I_CAL_VJOURNAL_COMPONENT:
		e_xml_document_add_attribute (xml, NULL, "name", "VJOURNAL");
		break;
	case I_CAL_VTODO_COMPONENT:
		e_xml_document_add_attribute (xml, NULL, "name", "VTODO");
		break;
	}

	if (!is_repeat) {
		ICalTimezone *utc = i_cal_timezone_get_utc_timezone ();
		time_t now;
		gchar *tmp;

		time (&now);

		*out_repeat = TRUE;

		/* Check for events in the month before/after today first,
		   to show user actual data as soon as possible. */
		e_xml_document_start_element (xml, NULL, "time-range");

		tmp = isodate_from_time_t (time_add_week_with_zone (now, -5, utc));
		e_xml_document_add_attribute (xml, NULL, "start", tmp);
		g_free (tmp);

		tmp = isodate_from_time_t (time_add_week_with_zone (now, +5, utc));
		e_xml_document_add_attribute (xml, NULL, "end", tmp);
		g_free (tmp);

		e_xml_document_end_element (xml); /* time-range */
	}

	e_xml_document_end_element (xml); /* comp-filter / VEVENT|VJOURNAL|VTODO */
	e_xml_document_end_element (xml); /* comp-filter / VCALENDAR*/
	e_xml_document_end_element (xml); /* filter */

	known_items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_cal_meta_backend_info_free);

	success = e_webdav_session_report_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS_AND_CHILDREN, xml,
		ecb_caldav_get_calendar_items_cb, known_items, NULL, NULL, cancellable, &local_error);

	g_object_unref (xml);

	if (success) {
		ECalCache *cal_cache;
		CalDAVChangesData ccd;

		ccd.is_repeat = is_repeat;
		ccd.out_modified_objects = out_modified_objects;
		ccd.out_removed_objects = out_removed_objects;
		ccd.known_items = known_items;

		cal_cache = e_cal_meta_backend_ref_cache (meta_backend);

		success = e_cal_cache_search_with_callback (cal_cache, NULL, ecb_caldav_search_changes_cb, &ccd, cancellable, &local_error);

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
		ECalCache *cal_cache = e_cal_meta_backend_ref_cache (meta_backend);
		GSList *link, *set2 = *out_modified_objects;

		if (*out_created_objects) {
			link = *out_created_objects;
		} else {
			link = set2;
			set2 = NULL;
		}

		do {
			success = ecb_caldav_multiget_from_sets_sync (cbdav, webdav, &link, &set2, out_removed_objects, cancellable, &local_error);
		} while (success && link);

		for (link = *out_created_objects; link; link = g_slist_next (link)) {
			ECalMetaBackendInfo *nfo = link->data;

			if (!nfo)
				continue;

			if (!nfo->uid || !*nfo->uid) {
				GSList *ids = NULL, *ilink = NULL;

				link->data = NULL;

				if (e_cal_cache_get_ids_with_extra (cal_cache, nfo->extra, &ids, cancellable, NULL)) {
					for (ilink = ids; ilink; ilink = g_slist_next (ilink)) {
						ECalComponentId *id = ilink->data;

						if (id) {
							*out_removed_objects = g_slist_prepend (*out_removed_objects,
								e_cal_meta_backend_info_new (e_cal_component_id_get_uid (id), nfo->revision, NULL, nfo->extra));
						}
					}

					g_slist_free_full (ids, e_cal_component_id_free);
				}

				e_cal_meta_backend_info_free (nfo);
			}
		}

		g_clear_object (&cal_cache);
	}

	if (local_error) {
		ecb_caldav_check_credentials_error (cbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_caldav_extract_existing_cb (EWebDAVSession *webdav,
				xmlNodePtr prop_node,
				const GUri *request_uri,
				const gchar *href,
				guint status_code,
				gpointer user_data)
{
	GSList **out_existing_objects = user_data;

	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		const xmlChar *calendar_data, *etag;
		xmlNodePtr calendar_data_node = NULL, etag_node = NULL;

		g_return_val_if_fail (href != NULL, FALSE);

		e_xml_find_children_nodes (prop_node, 2,
			E_WEBDAV_NS_CALDAV, "calendar-data", &calendar_data_node,
			E_WEBDAV_NS_DAV, "getetag", &etag_node);

		calendar_data = e_xml_get_node_text (calendar_data_node);
		etag = e_xml_get_node_text (etag_node);

		if (calendar_data) {
			ICalComponent *vcalendar;

			vcalendar = i_cal_component_new_from_string ((const gchar *) calendar_data);
			if (vcalendar) {
				const gchar *uid;

				uid = ecb_caldav_get_vcalendar_uid (vcalendar);

				if (uid) {
					gchar *dequoted_etag;

					dequoted_etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) etag));

					*out_existing_objects = g_slist_prepend (*out_existing_objects,
						e_cal_meta_backend_info_new (uid, dequoted_etag, NULL, href));

					g_free (dequoted_etag);
				}

				g_object_unref (vcalendar);
			}
		}
	}

	return TRUE;
}

static gboolean
ecb_caldav_list_existing_sync (ECalMetaBackend *meta_backend,
			       gchar **out_new_sync_tag,
			       GSList **out_existing_objects,
			       GCancellable *cancellable,
			       GError **error)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	ICalComponentKind kind;
	EXmlDocument *xml;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	*out_existing_objects = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));

	xml = e_xml_document_new (E_WEBDAV_NS_CALDAV, "calendar-query");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "getetag");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "calendar-data");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "comp");
	e_xml_document_add_attribute (xml, NULL, "name", "VCALENDAR");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "comp");
	if (kind == I_CAL_VEVENT_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VEVENT");
	else if (kind == I_CAL_VJOURNAL_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VJOURNAL");
	else if (kind == I_CAL_VTODO_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VTODO");
	else
		g_warn_if_reached ();
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "prop");
	e_xml_document_add_attribute (xml, NULL, "name", "UID");
	e_xml_document_end_element (xml); /* prop / UID */
	e_xml_document_end_element (xml); /* comp / VEVENT|VJOURNAL|VTODO */
	e_xml_document_end_element (xml); /* comp / VCALENDAR */
	e_xml_document_end_element (xml); /* calendar-data */
	e_xml_document_end_element (xml); /* prop */

	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "filter");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "comp-filter");
	e_xml_document_add_attribute (xml, NULL, "name", "VCALENDAR");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "comp-filter");
	if (kind == I_CAL_VEVENT_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VEVENT");
	else if (kind == I_CAL_VJOURNAL_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VJOURNAL");
	else if (kind == I_CAL_VTODO_COMPONENT)
		e_xml_document_add_attribute (xml, NULL, "name", "VTODO");
	e_xml_document_end_element (xml); /* comp-filter / VEVENT|VJOURNAL|VTODO */
	e_xml_document_end_element (xml); /* comp-filter / VCALENDAR */
	e_xml_document_end_element (xml); /* filter */

	webdav = ecb_caldav_ref_session (cbdav);

	success = e_webdav_session_report_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS, xml,
		ecb_caldav_extract_existing_cb, out_existing_objects, NULL, NULL, cancellable, &local_error);

	g_object_unref (xml);

	if (success)
		*out_existing_objects = g_slist_reverse (*out_existing_objects);

	if (local_error) {
		ecb_caldav_check_credentials_error (cbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gchar *
ecb_caldav_uid_to_uri (ECalBackendCalDAV *cbdav,
		       const gchar *uid,
		       const gchar *extension)
{
	ESourceWebdav *webdav_extension;
	GUri *guri;
	gchar *uri, *tmp, *filename, *uid_hash = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	webdav_extension = e_source_get_extension (e_backend_get_source (E_BACKEND (cbdav)), E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	guri = e_source_webdav_dup_uri (webdav_extension);
	g_return_val_if_fail (guri != NULL, NULL);

	/* UIDs with forward slashes can cause trouble, because the destination server can
	   consider them as a path delimiter. Double-encode the URL doesn't always work,
	   thus rather cause a mismatch between stored UID and its href on the server. */
	if (strchr (uid, '/')) {
		uid_hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, uid, -1);

		if (uid_hash)
			uid = uid_hash;
	}

	if (extension) {
		tmp = g_strconcat (uid, extension, NULL);
		filename = g_uri_escape_string (tmp, NULL, FALSE);
		g_free (tmp);
	} else {
		filename = g_uri_escape_string (uid, NULL, FALSE);
	}

	if (g_uri_get_path (guri) && *g_uri_get_path (guri)) {
		const gchar *slash = strrchr (g_uri_get_path (guri), '/');

		if (slash && !slash[1])
			tmp = g_strconcat (g_uri_get_path (guri), filename, NULL);
		else
			tmp = g_strconcat (g_uri_get_path (guri), "/", filename, NULL);
	} else {
		tmp = g_strconcat ("/", filename, NULL);
	}

	e_util_change_uri_component (&guri, SOUP_URI_PATH, tmp);
	g_free (tmp);

	uri = g_uri_to_string_partial (guri, G_URI_HIDE_USERINFO | G_URI_HIDE_PASSWORD);

	g_uri_unref (guri);
	g_free (filename);
	g_free (uid_hash);

	return uri;
}

static void
ecb_caldav_store_component_etag (ICalComponent *icomp,
				 const gchar *etag)
{
	ICalComponent *subcomp;

	g_return_if_fail (icomp != NULL);
	g_return_if_fail (etag != NULL);

	e_cal_util_component_set_x_property (icomp, E_CALDAV_X_ETAG, etag);

	for (subcomp = i_cal_component_get_first_component (icomp, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (icomp, I_CAL_ANY_COMPONENT)) {
		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VJOURNAL_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			e_cal_util_component_set_x_property (subcomp, E_CALDAV_X_ETAG, etag);
		}
	}
}

static gboolean
ecb_caldav_get_save_schedules_enabled (ECalBackendCalDAV *cbdav)
{
	ESourceWebdav *extension;
	ESource *source;

	if ((!cbdav->priv->calendar_schedule && !cbdav->priv->calendar_auto_schedule) ||
	    e_cal_backend_get_kind (E_CAL_BACKEND (cbdav)) == I_CAL_VJOURNAL_COMPONENT) {
		return FALSE;
	}

	source = e_backend_get_source (E_BACKEND (cbdav));
	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);

	return e_source_webdav_get_calendar_auto_schedule (extension);
}

static void
ecb_cbdav_set_property_schedule_agent (ICalComponent *comp,
				       ICalPropertyKind kind,
				       ICalParameterScheduleagent value)
{
	ICalProperty *prop;

	for (prop = i_cal_component_get_first_property (comp, kind);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp, kind)) {
		i_cal_property_remove_parameter_by_kind (prop, I_CAL_SCHEDULEAGENT_PARAMETER);

		/* use the I_CAL_SCHEDULEAGENT_X to remove any existing parameters */
		if (value != I_CAL_SCHEDULEAGENT_X) {
			ICalParameter *param;

			param = i_cal_parameter_new_scheduleagent (value);
			i_cal_property_take_parameter (prop, param);
		}
	}
}

static gboolean
ecb_caldav_load_component_sync (ECalMetaBackend *meta_backend,
				const gchar *uid,
				const gchar *extra,
				ICalComponent **out_component,
				gchar **out_extra,
				GCancellable *cancellable,
				GError **error)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	gchar *uri = NULL, *href = NULL, *etag = NULL, *bytes = NULL;
	gsize length = -1;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_component != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);

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

	webdav = ecb_caldav_ref_session (cbdav);

	if (extra && *extra) {
		uri = g_strdup (extra);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);

		if (!success) {
			g_free (uri);
			uri = NULL;
		}
	}

	if (!success && cbdav->priv->ctag_supported) {
		gchar *new_sync_tag = NULL;

		if (e_webdav_session_getctag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL) && new_sync_tag) {
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
		uri = ecb_caldav_uid_to_uri (cbdav, uid, ".ics");
		g_return_val_if_fail (uri != NULL, FALSE);

		g_clear_error (&local_error);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);

		/* Do not try twice with Google, it's either with ".ics" extension or not there.
		   The worst, it counts to the Error requests quota limit. */
		if (!success && !cbdav->priv->is_google && !g_cancellable_is_cancelled (cancellable) &&
		    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			g_free (uri);
			uri = ecb_caldav_uid_to_uri (cbdav, uid, NULL);

			if (uri) {
				g_clear_error (&local_error);

				success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);
			}
		}
	}

	if (success) {
		*out_component = NULL;

		if (href && etag && bytes && length != ((gsize) -1)) {
			ICalComponent *icomp;

			icomp = i_cal_component_new_from_string (bytes);

			if (icomp) {
				ecb_caldav_store_component_etag (icomp, etag);

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

	g_free (uri);
	g_free (href);
	g_free (etag);
	g_free (bytes);

	if (local_error) {
		ecb_caldav_check_credentials_error (cbdav, webdav, local_error);

		if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			local_error->domain = E_CAL_CLIENT_ERROR;
			local_error->code = E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND;
		}

		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_caldav_save_component_sync (ECalMetaBackend *meta_backend,
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
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	ICalComponent *vcalendar, *subcomp;
	gchar *href = NULL, *etag = NULL, *uid = NULL;
	gchar *ical_string = NULL;
	gboolean disable_scheduling;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (instances != NULL, FALSE);
	g_return_val_if_fail (out_new_uid, FALSE);
	g_return_val_if_fail (out_new_extra, FALSE);

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);

	vcalendar = e_cal_meta_backend_merge_instances (meta_backend, instances, TRUE);
	g_return_val_if_fail (vcalendar != NULL, FALSE);

	disable_scheduling = cbdav->priv->calendar_auto_schedule && (
		(opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE) != 0 ||
		!ecb_caldav_get_save_schedules_enabled (cbdav));

	for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
	     subcomp;
	     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {
		ICalComponentKind kind = i_cal_component_isa (subcomp);

		if (kind == I_CAL_VEVENT_COMPONENT ||
		    kind == I_CAL_VJOURNAL_COMPONENT ||
		    kind == I_CAL_VTODO_COMPONENT) {
			if (!etag)
				etag = e_cal_util_component_dup_x_property (subcomp, E_CALDAV_X_ETAG);
			if (!uid)
				uid = g_strdup (i_cal_component_get_uid (subcomp));

			e_cal_util_component_remove_x_property (subcomp, E_CALDAV_X_ETAG);

			/* the SCHEDULE-AGENT parameter change can send cancellation notices,
			   thus it cannot be used. */
			if (disable_scheduling && !overwrite_existing) {
				/* this way the server should not send any messages on its own */
				ecb_cbdav_set_property_schedule_agent (subcomp, I_CAL_ORGANIZER_PROPERTY, I_CAL_SCHEDULEAGENT_CLIENT);
				ecb_cbdav_set_property_schedule_agent (subcomp, I_CAL_ATTENDEE_PROPERTY, I_CAL_SCHEDULEAGENT_CLIENT);
			}
		}
	}

	ical_string = i_cal_component_as_ical_string (vcalendar);

	webdav = ecb_caldav_ref_session (cbdav);

	if (uid && ical_string && (!overwrite_existing || (extra && *extra))) {
		gchar *new_extra = NULL, *new_etag = NULL;
		gboolean force_write = FALSE;

		if (!extra || !*extra)
			href = ecb_caldav_uid_to_uri (cbdav, uid, ".ics");

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

		success = e_webdav_session_put_data_sync (webdav, (extra && *extra) ? extra : href,
			force_write ? "" : overwrite_existing ? etag : NULL, E_WEBDAV_CONTENT_TYPE_CALENDAR,
			NULL, ical_string, -1, &new_extra, &new_etag, NULL, cancellable, &local_error);

		if (success) {
			/* Only if both are returned and it's not a weak ETag */
			if (new_extra && *new_extra && new_etag && *new_etag &&
			    g_ascii_strncasecmp (new_etag, "W/", 2) != 0) {
				gchar *tmp;

				ecb_caldav_store_component_etag (vcalendar, new_etag);

				/* remove the SCHEDULE-AGENT only if it was added */
				if (disable_scheduling && !overwrite_existing) {
					for (subcomp = i_cal_component_get_first_component (vcalendar, I_CAL_ANY_COMPONENT);
					     subcomp;
					     g_object_unref (subcomp), subcomp = i_cal_component_get_next_component (vcalendar, I_CAL_ANY_COMPONENT)) {
						ICalComponentKind kind = i_cal_component_isa (subcomp);

						if (kind == I_CAL_VEVENT_COMPONENT ||
						    kind == I_CAL_VJOURNAL_COMPONENT ||
						    kind == I_CAL_VTODO_COMPONENT) {
							/* remote the added scheduling agent parameters */
							ecb_cbdav_set_property_schedule_agent (subcomp, I_CAL_ORGANIZER_PROPERTY, I_CAL_SCHEDULEAGENT_X);
							ecb_cbdav_set_property_schedule_agent (subcomp, I_CAL_ATTENDEE_PROPERTY, I_CAL_SCHEDULEAGENT_X);
						}
					}
				}

				g_free (ical_string);
				ical_string = i_cal_component_as_ical_string (vcalendar);

				/* Encodes the href and the component into one string, which
				   will be decoded in the load function */
				tmp = g_strconcat (new_extra, "\n", ical_string, NULL);
				g_free (new_extra);
				new_extra = tmp;
			}

			/* To read the component back, either from the new_extra
			   or from the server, because the server could change it */
			*out_new_uid = g_strdup (uid);

			if (out_new_extra)
				*out_new_extra = new_extra;
			else
				g_free (new_extra);
		}

		g_free (new_etag);
	} else if (uid && ical_string) {
		ECalCache *cache;

		cache = e_cal_meta_backend_ref_cache (meta_backend);
		success = FALSE;

		g_propagate_error (error, e_cal_client_error_create_fmt (E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			_("Missing information about component URL, local cache is possibly incomplete or broken. You can try to remove it and restart background evolution-data-server processes. Cache file: %s"),
			e_cache_get_filename (E_CACHE (cache))));

		g_clear_object (&cache);
	} else {
		success = FALSE;
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
	}

	g_object_unref (vcalendar);
	g_free (ical_string);
	g_free (href);
	g_free (etag);
	g_free (uid);

	if (overwrite_existing && g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_PRECONDITION_FAILED)) {
		g_clear_error (&local_error);

		/* Pretend success when using the serer version on conflict,
		   the component will be updated during the refresh */
		if (conflict_resolution == E_CONFLICT_RESOLUTION_KEEP_SERVER)
			success = TRUE;
		else
			local_error = EC_ERROR (E_CLIENT_ERROR_OUT_OF_SYNC);
	}

	if (local_error) {
		ecb_caldav_check_credentials_error (cbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_caldav_remove_component_sync (ECalMetaBackend *meta_backend,
				  EConflictResolution conflict_resolution,
				  const gchar *uid,
				  const gchar *extra,
				  const gchar *object,
				  ECalOperationFlags opflags,
				  GCancellable *cancellable,
				  GError **error)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	ICalComponent *icomp;
	SoupMessageHeaders *extra_headers = NULL;
	gchar *etag = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);

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
		etag = e_cal_util_component_dup_x_property (icomp, E_CALDAV_X_ETAG);

	webdav = ecb_caldav_ref_session (cbdav);

	if ((opflags & E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE) != 0 ||
	    !ecb_caldav_get_save_schedules_enabled (cbdav)) {
		extra_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);

		soup_message_headers_append (extra_headers, "Schedule-Reply", "F");
	}

	success = e_webdav_session_delete_with_headers_sync (webdav, extra,
		NULL, etag, extra_headers, cancellable, &local_error);

	if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
		gchar *href;

		href = ecb_caldav_uid_to_uri (cbdav, uid, ".ics");
		if (href) {
			g_clear_error (&local_error);
			success = e_webdav_session_delete_with_headers_sync (webdav, href,
				NULL, etag, extra_headers, cancellable, &local_error);

			g_free (href);
		}

		if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			href = ecb_caldav_uid_to_uri (cbdav, uid, NULL);
			if (href) {
				g_clear_error (&local_error);
				success = e_webdav_session_delete_with_headers_sync (webdav, href,
					NULL, etag, extra_headers, cancellable, &local_error);

				g_free (href);
			}
		}
	}

	g_clear_pointer (&extra_headers, soup_message_headers_unref);
	g_object_unref (icomp);
	g_free (etag);

	/* Ignore not found errors, this was a delete and the resource is gone.
	   It can be that it had been deleted on the server by other application. */
	if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
		g_clear_error (&local_error);
		success = TRUE;
	} else if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_PRECONDITION_FAILED)) {
		g_clear_error (&local_error);

		/* Pretend success when using the serer version on conflict,
		   the component will be updated during the refresh */
		if (conflict_resolution == E_CONFLICT_RESOLUTION_KEEP_SERVER)
			success = TRUE;
		else
			local_error = EC_ERROR (E_CLIENT_ERROR_OUT_OF_SYNC);
	}

	if (local_error) {
		ecb_caldav_check_credentials_error (cbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ecb_caldav_get_ssl_error_details (ECalMetaBackend *meta_backend,
				  gchar **out_certificate_pem,
				  GTlsCertificateFlags *out_certificate_errors)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;
	gboolean res;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (meta_backend), FALSE);

	cbdav = E_CAL_BACKEND_CALDAV (meta_backend);
	webdav = ecb_caldav_ref_session (cbdav);

	if (!webdav)
		return FALSE;

	res = e_soup_session_get_ssl_error_details (E_SOUP_SESSION (webdav), out_certificate_pem, out_certificate_errors);

	g_clear_object (&webdav);

	return res;
}

static gboolean
ecb_caldav_dup_href_node_value (EWebDAVSession *webdav,
				const GUri *request_uri,
				xmlNodePtr from_node,
				const gchar *parent_ns_href,
				const gchar *parent_name,
				gchar **out_href)
{
	xmlNodePtr node;

	g_return_val_if_fail (out_href != NULL, FALSE);

	if (!from_node)
		return FALSE;

	node = e_xml_find_in_hierarchy (from_node, parent_ns_href, parent_name, E_WEBDAV_NS_DAV, "href", NULL, NULL);

	if (node) {
		const xmlChar *href;

		href = e_xml_get_node_text (node);

		if (href && *href) {
			*out_href = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) href);

			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
ecb_caldav_propfind_get_owner_cb (EWebDAVSession *webdav,
				  xmlNodePtr prop_node,
				  const GUri *request_uri,
				  const gchar *href,
				  guint status_code,
				  gpointer user_data)
{
	gchar **out_owner_href = user_data;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (out_owner_href != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK &&
	    ecb_caldav_dup_href_node_value (webdav, request_uri, prop_node, E_WEBDAV_NS_DAV, "owner", out_owner_href)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
ecb_caldav_propfind_get_schedule_outbox_url_cb (EWebDAVSession *webdav,
						xmlNodePtr prop_node,
						const GUri *request_uri,
						const gchar *href,
						guint status_code,
						gpointer user_data)
{
	gchar **out_schedule_outbox_url = user_data;

	g_return_val_if_fail (out_schedule_outbox_url != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK &&
	    ecb_caldav_dup_href_node_value (webdav, request_uri, prop_node, E_WEBDAV_NS_CALDAV, "schedule-outbox-URL", out_schedule_outbox_url)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
ecb_caldav_receive_schedule_outbox_url_sync (ECalBackendCalDAV *cbdav,
					     GCancellable *cancellable,
					     GError **error)
{
	EXmlDocument *xml;
	EWebDAVSession *webdav;
	gchar *owner_href = NULL, *schedule_outbox_url = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (cbdav->priv->schedule_outbox_url == NULL, TRUE);

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "owner");
	e_xml_document_end_element (xml); /* prop */

	webdav = ecb_caldav_ref_session (cbdav);

	success = e_webdav_session_propfind_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS, xml,
		ecb_caldav_propfind_get_owner_cb, &owner_href, cancellable, error);

	g_object_unref (xml);

	if (!success || !owner_href || !*owner_href) {
		g_clear_object (&webdav);
		g_free (owner_href);
		return FALSE;
	}

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	if (!xml) {
		g_warn_if_fail (xml != NULL);
		g_clear_object (&webdav);
		g_free (owner_href);
		return FALSE;
	}

	e_xml_document_add_namespaces (xml, "C", E_WEBDAV_NS_CALDAV, NULL);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "schedule-outbox-URL");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, owner_href, E_WEBDAV_DEPTH_THIS, xml,
		ecb_caldav_propfind_get_schedule_outbox_url_cb, &schedule_outbox_url, cancellable, error);

	g_clear_object (&webdav);
	g_object_unref (xml);
	g_free (owner_href);

	if (success && schedule_outbox_url && *schedule_outbox_url) {
		g_free (cbdav->priv->schedule_outbox_url);
		cbdav->priv->schedule_outbox_url = schedule_outbox_url;

		schedule_outbox_url = NULL;
	} else {
		success = FALSE;
	}

	g_free (schedule_outbox_url);

	return success;
}

static void
ecb_caldav_extract_objects (ICalComponent *icomp,
			    ICalComponentKind ekind,
			    GSList **out_objects,
			    GError **error)
{
	ICalComponent *scomp;
	ICalComponentKind kind;
	GSList *link;

	g_return_if_fail (icomp != NULL);
	g_return_if_fail (out_objects != NULL);

	kind = i_cal_component_isa (icomp);

	if (kind == ekind) {
		*out_objects = g_slist_prepend (NULL, i_cal_component_clone (icomp));
		return;
	}

	if (kind != I_CAL_VCALENDAR_COMPONENT) {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	*out_objects = NULL;
	for (scomp = i_cal_component_get_first_component (icomp, ekind);
	     scomp;
	     g_object_unref (scomp), scomp = i_cal_component_get_next_component (icomp, ekind)) {
		*out_objects = g_slist_prepend (*out_objects, g_object_ref (scomp));
	}

	for (link = *out_objects; link; link = g_slist_next (link)) {
		/* Remove components from toplevel here */
		i_cal_component_remove_component (icomp, link->data);
	}

	*out_objects = g_slist_reverse (*out_objects);
}

static gchar *
ecb_caldav_maybe_append_email_domain (const gchar *username,
				      const gchar *may_append)
{
	if (!username || !*username)
		return NULL;

	if (strchr (username, '@'))
		return g_strdup (username);

	return g_strconcat (username, may_append, NULL);
}

static gchar *
ecb_caldav_get_usermail (ECalBackendCalDAV *cbdav)
{
	ESource *source;
	ESourceAuthentication *auth_extension;
	ESourceWebdav *webdav_extension;
	const gchar *extension_name;
	gchar *usermail;
	gchar *username;
	gchar *res = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), NULL);

	source = e_backend_get_source (E_BACKEND (cbdav));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	/* This will never return an empty string. */
	usermail = e_source_webdav_dup_email_address (webdav_extension);

	if (usermail)
		return usermail;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);
	username = e_source_authentication_dup_user (auth_extension);

	if (cbdav->priv->is_google) {
		res = ecb_caldav_maybe_append_email_domain (username, "@gmail.com");
	} else if (username && strchr (username, '@') && strrchr (username, '.') > strchr (username, '@')) {
		res = username;
		username = NULL;
	}

	g_free (username);

	return res;
}

static gboolean
ecb_caldav_get_free_busy_from_schedule_outbox_sync (ECalBackendCalDAV *cbdav,
						    const GSList *users,
						    time_t start,
						    time_t end,
						    GSList **out_freebusy,
						    GCancellable *cancellable,
						    GError **error)
{
	ICalComponent *icomp;
	ECalComponent *comp;
	ECalComponentDateTime *dt;
	ECalComponentOrganizer *organizer;
	ESourceAuthentication *auth_extension;
	ESource *source;
	EWebDAVSession *webdav;
	ICalTimezone *utc;
	gchar *str;
	GSList *link;
	GSList *attendees = NULL;
	const gchar *extension_name;
	gchar *usermail, *organizer_value;
	GByteArray *response = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);

	if (!cbdav->priv->calendar_schedule)
		return FALSE;

	if (!cbdav->priv->schedule_outbox_url) {
		if (!ecb_caldav_receive_schedule_outbox_url_sync (cbdav, cancellable, error) ||
		    !cbdav->priv->schedule_outbox_url) {
			cbdav->priv->calendar_schedule = FALSE;
			return FALSE;
		}
	}

	comp = e_cal_component_new ();
	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY);

	str = e_util_generate_uid ();
	e_cal_component_set_uid (comp, str);
	g_free (str);

	utc = i_cal_timezone_get_utc_timezone ();
	dt = e_cal_component_datetime_new_take (i_cal_time_new_current_with_zone (utc), g_strdup (i_cal_timezone_get_tzid (utc)));

	e_cal_component_set_dtstamp (comp, e_cal_component_datetime_get_value (dt));

	e_cal_component_datetime_take_value (dt, i_cal_time_new_from_timet_with_zone (start, FALSE, utc));
	e_cal_component_set_dtstart (comp, dt);

	e_cal_component_datetime_take_value (dt, i_cal_time_new_from_timet_with_zone (end, FALSE, utc));
	e_cal_component_set_dtend (comp, dt);

	e_cal_component_datetime_free (dt);

	usermail = ecb_caldav_get_usermail (cbdav);
	if (usermail != NULL && *usermail == '\0') {
		g_free (usermail);
		usermail = NULL;
	}

	source = e_backend_get_source (E_BACKEND (cbdav));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	if (!usermail)
		usermail = e_source_authentication_dup_user (auth_extension);

	organizer_value = g_strconcat ("mailto:", usermail, NULL);
	organizer = e_cal_component_organizer_new ();
	e_cal_component_organizer_set_value (organizer, organizer_value);
	g_free (organizer_value);

	e_cal_component_set_organizer (comp, organizer);
	e_cal_component_organizer_free (organizer);

	g_free (usermail);

	for (link = (GSList *) users; link; link = g_slist_next (link)) {
		ECalComponentAttendee *ca;
		gchar *temp = g_strconcat ("mailto:", (const gchar *) link->data, NULL);

		ca = e_cal_component_attendee_new ();

		e_cal_component_attendee_set_value (ca, temp);
		e_cal_component_attendee_set_cutype (ca, I_CAL_CUTYPE_INDIVIDUAL);
		e_cal_component_attendee_set_partstat (ca, I_CAL_PARTSTAT_NEEDSACTION);
		e_cal_component_attendee_set_role (ca, I_CAL_ROLE_CHAIR);

		g_free (temp);

		attendees = g_slist_append (attendees, ca);
	}

	e_cal_component_set_attendees (comp, attendees);

	g_slist_free_full (attendees, e_cal_component_attendee_free);

	e_cal_component_abort_sequence (comp);

	/* put the free/busy request to a VCALENDAR */
	icomp = e_cal_util_new_top_level ();
	i_cal_component_set_method (icomp, I_CAL_METHOD_REQUEST);
	i_cal_component_take_component (icomp, i_cal_component_clone (e_cal_component_get_icalcomponent (comp)));

	str = i_cal_component_as_ical_string (icomp);

	g_object_unref (icomp);
	g_object_unref (comp);

	webdav = ecb_caldav_ref_session (cbdav);

	if (e_webdav_session_post_sync (webdav, cbdav->priv->schedule_outbox_url, str, -1, E_WEBDAV_CONTENT_TYPE_CALENDAR,
					NULL, NULL, NULL, &response, cancellable, &local_error) &&
	    response) {
		/* parse returned xml */
		xmlDocPtr doc;
		xmlNodePtr schedule_response = NULL;

		doc = e_xml_parse_data (response->data, response->len);

		if (!doc) {
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("Failed to parse response data"));
		} else {
			schedule_response = e_xml_find_sibling (xmlDocGetRootElement (doc), E_WEBDAV_NS_CALDAV, "schedule-response");
		}

		if (schedule_response) {
			xmlNodePtr response_node;

			for (response_node = e_xml_find_child (schedule_response, E_WEBDAV_NS_CALDAV, "response");
			     response_node && !g_cancellable_is_cancelled (cancellable);
			     response_node = e_xml_find_next_sibling (response_node, E_WEBDAV_NS_CALDAV, "response")) {
				const xmlChar *calendar_data;

				calendar_data = e_xml_find_child_and_get_text (response_node, E_WEBDAV_NS_CALDAV, "calendar-data");

				if (calendar_data && *calendar_data) {
					GSList *objects = NULL;

					icomp = i_cal_parser_parse_string ((const gchar *) calendar_data);

					if (icomp)
						ecb_caldav_extract_objects (icomp, I_CAL_VFREEBUSY_COMPONENT, &objects, &local_error);

					if (icomp && !local_error) {
						for (link = objects; link; link = g_slist_next (link)) {
							gchar *obj_str = i_cal_component_as_ical_string (link->data);

							if (obj_str && *obj_str)
								*out_freebusy = g_slist_prepend (*out_freebusy, obj_str);
							else
								g_free (obj_str);
						}
					}

					g_slist_free_full (objects, g_object_unref);

					g_clear_object (&icomp);
					g_clear_error (&local_error);
				}
			}
		}

		if (doc)
			xmlFreeDoc (doc);
	}

	g_clear_object (&webdav);
	if (response)
		g_byte_array_free (response, TRUE);
	g_free (str);

	if (local_error)
		g_propagate_error (error, local_error);

	return !local_error;
}

static gboolean
ecb_caldav_get_free_busy_from_principal_sync (ECalBackendCalDAV *cbdav,
					      const gchar *usermail,
					      time_t start,
					      time_t end,
					      GSList **out_freebusy,
					      GCancellable *cancellable,
					      GError **error)
{
	EWebDAVSession *webdav;
	EWebDAVResource *resource;
	GSList *principals = NULL;
	EXmlDocument *xml;
	gchar *href;
	gchar *content_type = NULL;
	GByteArray *content = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (usermail != NULL, FALSE);
	g_return_val_if_fail (out_freebusy != NULL, FALSE);

	webdav = ecb_caldav_ref_session (cbdav);

	if (!e_webdav_session_principal_property_search_sync (webdav, NULL, TRUE,
		E_WEBDAV_NS_CALDAV, "calendar-user-address-set", usermail, &principals, cancellable, error)) {
		g_clear_object (&webdav);
		return FALSE;
	}

	if (!principals || principals->next || !principals->data) {
		g_slist_free_full (principals, e_webdav_resource_free);
		g_clear_object (&webdav);
		return FALSE;
	}

	resource = principals->data;
	href = g_strdup (resource->href);

	g_slist_free_full (principals, e_webdav_resource_free);

	if (!href || !*href) {
		g_clear_object (&webdav);
		g_free (href);
		return FALSE;
	}

	xml = e_xml_document_new (E_WEBDAV_NS_CALDAV, "free-busy-query");

	e_xml_document_start_element (xml, NULL, "time-range");
	e_xml_document_add_attribute_time_ical (xml, NULL, "start", start);
	e_xml_document_add_attribute_time_ical (xml, NULL, "end", end);
	e_xml_document_end_element (xml); /* time-range */

	success = e_webdav_session_report_sync (webdav, href, E_WEBDAV_DEPTH_INFINITY, xml, NULL, NULL, &content_type, &content, cancellable, error);

	g_clear_object (&webdav);
	g_object_unref (xml);

	if (success && content_type && content && content->data && content->len &&
	    g_ascii_strcasecmp (content_type, "text/calendar") == 0) {
		ICalComponent *vcalendar;

		vcalendar = i_cal_component_new_from_string ((const gchar *) content->data);
		if (vcalendar) {
			GSList *comps = NULL, *link;

			ecb_caldav_extract_objects (vcalendar, I_CAL_VFREEBUSY_COMPONENT, &comps, NULL);

			for (link = comps; link; link = g_slist_next (link)) {
				ICalComponent *subcomp = link->data;
				gchar *obj_str;

				if (!e_cal_util_component_has_property (subcomp, I_CAL_ATTENDEE_PROPERTY)) {
					ICalProperty *prop;
					gchar *mailto;

					mailto = g_strconcat ("mailto:", usermail, NULL);
					prop = i_cal_property_new_attendee (mailto);
					g_free (mailto);

					i_cal_component_take_property (subcomp, prop);
				}

				obj_str = i_cal_component_as_ical_string (subcomp);

				if (obj_str && *obj_str)
					*out_freebusy = g_slist_prepend (*out_freebusy, obj_str);
				else
					g_free (obj_str);
			}

			success = comps != NULL;

			g_slist_free_full (comps, g_object_unref);
			g_object_unref (vcalendar);
		} else {
			success = FALSE;
		}
	}

	if (content)
		g_byte_array_free (content, TRUE);
	g_free (content_type);
	g_free (href);

	return success;
}

static void
ecb_caldav_get_free_busy_sync (ECalBackendSync *sync_backend,
			       EDataCal *cal,
			       GCancellable *cancellable,
			       const GSList *users,
			       time_t start,
			       time_t end,
			       GSList **out_freebusy,
			       GError **error)
{
	ECalBackendCalDAV *cbdav;
	EWebDAVSession *webdav;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (sync_backend));
	g_return_if_fail (out_freebusy != NULL);

	cbdav = E_CAL_BACKEND_CALDAV (sync_backend);
	webdav = ecb_caldav_ref_session (cbdav);

	if (e_backend_get_online (E_BACKEND (cbdav)) && webdav) {
		const GSList *link;
		GError *local_error = NULL;

		/* Finish only if found anything, otherwise re-check with the principals */
		if (ecb_caldav_get_free_busy_from_schedule_outbox_sync (cbdav, users, start, end, out_freebusy, cancellable, &local_error) &&
		    out_freebusy && *out_freebusy) {
			g_clear_object (&webdav);
			return;
		}

		g_clear_error (&local_error);

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			g_clear_object (&webdav);
			return;
		}

		*out_freebusy = NULL;

		for (link = users; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
			if (!ecb_caldav_get_free_busy_from_principal_sync (cbdav, link->data, start, end, out_freebusy, cancellable, &local_error))
				g_clear_error (&local_error);
		}

		g_clear_error (&local_error);

		if (*out_freebusy || g_cancellable_set_error_if_cancelled (cancellable, error)) {
			g_clear_object (&webdav);
			return;
		}
	}

	g_clear_object (&webdav);

	/* Chain up to parent's method. */
	E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_caldav_parent_class)->get_free_busy_sync (sync_backend, cal, cancellable,
		users, start, end, out_freebusy, error);
}

static void
ecb_caldav_discard_alarm_sync (ECalBackendSync *sync_backend,
			       EDataCal *cal,
			       GCancellable *cancellable,
			       const gchar *uid,
			       const gchar *rid,
			       const gchar *auid,
			       ECalOperationFlags opflags,
			       GError **error)
{
	ECalComponent *comp = NULL;
	ECalCache *cache;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (sync_backend));
	g_return_if_fail (uid != NULL);

	cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (sync_backend));

	if (cache) {
		GError *local_error = NULL;

		if (!e_cal_cache_get_component (cache, uid, rid, &comp, cancellable, &local_error) && rid && *rid) {
			if (!e_cal_cache_get_component (cache, uid, NULL, &comp, cancellable, NULL)) {
				g_propagate_error (error, local_error);
				g_object_unref (cache);
				return;
			}

			rid = NULL;
		}
	}

	if (comp) {
		if (e_cal_util_set_alarm_acknowledged (comp, auid, 0)) {
			/* the SCHEDULE-AGENT parameter change can send cancellation notices,
			   thus avoid saving these changes on the server which does auto-schedule;
			   revert this once there will be a way to save the change on the server
			   without notifying the attendees */
			ECalBackendCalDAV *cbdav = E_CAL_BACKEND_CALDAV (sync_backend);

			if (!cbdav->priv->calendar_auto_schedule) {
				GSList *calobjs, *old_components = NULL, *new_components = NULL;

				calobjs = g_slist_prepend (NULL, e_cal_component_get_as_string (comp));

				e_cal_backend_sync_modify_objects (sync_backend, cal, cancellable, calobjs,
					(rid && *rid) ? E_CAL_OBJ_MOD_THIS : E_CAL_OBJ_MOD_ALL,
					/* this is not a reason to notify meeting attendees */
					opflags | E_CAL_OPERATION_FLAG_DISABLE_ITIP_MESSAGE,
					&old_components, &new_components, error);

				e_util_free_nullable_object_slist (old_components);
				e_util_free_nullable_object_slist (new_components);
				g_slist_free_full (calobjs, g_free);
			}
		} else {
			g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
		}

		g_object_unref (comp);
	} else {
		g_propagate_error (error, ECC_ERROR (E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
	}

	g_clear_object (&cache);
}

static gchar *
ecb_caldav_get_backend_property (ECalBackend *backend,
				 const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		ECalBackendCalDAV *cbdav = E_CAL_BACKEND_CALDAV (backend);
		GString *caps;
		gchar *usermail;

		caps = g_string_new (
			E_CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
			E_CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED ","
			E_CAL_STATIC_CAPABILITY_TASK_CAN_RECUR ","
			E_CAL_STATIC_CAPABILITY_COMPONENT_COLOR ","
			E_CAL_STATIC_CAPABILITY_TASK_ESTIMATED_DURATION);
		g_string_append_c (caps, ',');
		g_string_append (caps, e_cal_meta_backend_get_capabilities (E_CAL_META_BACKEND (backend)));

		usermail = ecb_caldav_get_usermail (cbdav);
		if (!usermail || !*usermail)
			g_string_append (caps, "," E_CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS);
		g_free (usermail);

		if (ecb_caldav_get_save_schedules_enabled (cbdav)) {
			g_string_append (
				caps,
				"," E_CAL_STATIC_CAPABILITY_CREATE_MESSAGES
				"," E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES
				"," E_CAL_STATIC_CAPABILITY_ITIP_SUPPRESS_ON_REMOVE_SUPPORTED);
		}

		return g_string_free (caps, FALSE);
	} else if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		return ecb_caldav_get_usermail (E_CAL_BACKEND_CALDAV (backend));
	}

	/* Chain up to parent's method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_caldav_parent_class)->impl_get_backend_property (backend, prop_name);
}

static void
ecb_caldav_notify_property_changed_cb (GObject *object,
				       GParamSpec *param,
				       gpointer user_data)
{
	ECalBackendCalDAV *cbdav = user_data;
	ECalBackend *cal_backend;
	gboolean email_address_changed;
	gboolean calendar_auto_schedule_changed;
	gchar *value;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	cal_backend = E_CAL_BACKEND (cbdav);

	email_address_changed = param && g_strcmp0 (param->name, "email-address") == 0;
	calendar_auto_schedule_changed = param && g_strcmp0 (param->name, "calendar-auto-schedule") == 0;

	if (email_address_changed || calendar_auto_schedule_changed) {
		value = ecb_caldav_get_backend_property (cal_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES);
		e_cal_backend_notify_property_changed (cal_backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES, value);
		g_free (value);
	}

	if (email_address_changed) {
		value = ecb_caldav_get_backend_property (cal_backend, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS);
		e_cal_backend_notify_property_changed (cal_backend, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS, value);
		e_cal_backend_notify_property_changed (cal_backend, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS, value);
		g_free (value);
	}
}

static void
ecb_caldav_refresh_sync (ECalBackendSync *sync_backend,
			 EDataCal *cal,
			 GCancellable *cancellable,
			 GError **error)
{
	ECalBackendCalDAV *cbdav;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (sync_backend));

	cbdav = E_CAL_BACKEND_CALDAV (sync_backend);
	cbdav->priv->been_connected = FALSE;

	/* Chain up to parent's method. */
	E_CAL_BACKEND_SYNC_CLASS (e_cal_backend_caldav_parent_class)->refresh_sync (sync_backend, cal, cancellable, error);
}

static gchar *
ecb_caldav_dup_component_revision_cb (ECalCache *cal_cache,
				      ICalComponent *icomp)
{
	g_return_val_if_fail (icomp != NULL, NULL);

	return e_cal_util_component_dup_x_property (icomp, E_CALDAV_X_ETAG);
}

static void
e_cal_backend_caldav_constructed (GObject *object)
{
	ECalBackendCalDAV *cbdav = E_CAL_BACKEND_CALDAV (object);
	ECalCache *cal_cache;
	ESource *source;
	ESourceWebdav *webdav_extension;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_caldav_parent_class)->constructed (object);

	cal_cache = e_cal_meta_backend_ref_cache (E_CAL_META_BACKEND (cbdav));

	g_signal_connect (cal_cache, "dup-component-revision",
		G_CALLBACK (ecb_caldav_dup_component_revision_cb), NULL);

	g_clear_object (&cal_cache);

	ecb_caldav_update_tweaks (cbdav);

	source = e_backend_get_source (E_BACKEND (cbdav));
	webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);

	g_signal_connect_object (webdav_extension, "notify::calendar-auto-schedule",
		G_CALLBACK (ecb_caldav_notify_property_changed_cb), cbdav, 0);

	g_signal_connect_object (webdav_extension, "notify::email-address",
		G_CALLBACK (ecb_caldav_notify_property_changed_cb), cbdav, 0);
}

static void
e_cal_backend_caldav_dispose (GObject *object)
{
	ECalBackendCalDAV *cbdav = E_CAL_BACKEND_CALDAV (object);

	g_mutex_lock (&cbdav->priv->webdav_lock);
	g_clear_object (&cbdav->priv->webdav);
	g_clear_pointer (&cbdav->priv->last_uri, g_uri_unref);
	g_mutex_unlock (&cbdav->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_caldav_parent_class)->dispose (object);
}

static void
e_cal_backend_caldav_finalize (GObject *object)
{
	ECalBackendCalDAV *cbdav = E_CAL_BACKEND_CALDAV (object);

	g_clear_pointer (&cbdav->priv->schedule_outbox_url, g_free);
	g_mutex_clear (&cbdav->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_cal_backend_caldav_parent_class)->finalize (object);
}

static void
e_cal_backend_caldav_init (ECalBackendCalDAV *cbdav)
{
	cbdav->priv = e_cal_backend_caldav_get_instance_private (cbdav);

	g_mutex_init (&cbdav->priv->webdav_lock);
}

static void
e_cal_backend_caldav_class_init (ECalBackendCalDAVClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *cal_backend_sync_class;
	ECalMetaBackendClass *cal_meta_backend_class;

	cal_meta_backend_class = E_CAL_META_BACKEND_CLASS (klass);
	cal_meta_backend_class->connect_sync = ecb_caldav_connect_sync;
	cal_meta_backend_class->disconnect_sync = ecb_caldav_disconnect_sync;
	cal_meta_backend_class->get_changes_sync = ecb_caldav_get_changes_sync;
	cal_meta_backend_class->list_existing_sync = ecb_caldav_list_existing_sync;
	cal_meta_backend_class->load_component_sync = ecb_caldav_load_component_sync;
	cal_meta_backend_class->save_component_sync = ecb_caldav_save_component_sync;
	cal_meta_backend_class->remove_component_sync = ecb_caldav_remove_component_sync;
	cal_meta_backend_class->get_ssl_error_details = ecb_caldav_get_ssl_error_details;

	cal_backend_sync_class = E_CAL_BACKEND_SYNC_CLASS (klass);
	cal_backend_sync_class->refresh_sync = ecb_caldav_refresh_sync;
	cal_backend_sync_class->get_free_busy_sync = ecb_caldav_get_free_busy_sync;
	cal_backend_sync_class->discard_alarm_sync = ecb_caldav_discard_alarm_sync;

	cal_backend_class = E_CAL_BACKEND_CLASS (klass);
	cal_backend_class->impl_get_backend_property = ecb_caldav_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_cal_backend_caldav_constructed;
	object_class->dispose = e_cal_backend_caldav_dispose;
	object_class->finalize = e_cal_backend_caldav_finalize;
}

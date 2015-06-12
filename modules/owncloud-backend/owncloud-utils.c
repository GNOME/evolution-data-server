/*
 * owncloud-utils.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libebackend/libebackend.h>

#include "owncloud-utils.h"

#define XPATH_STATUS "string(/D:multistatus/D:response[%d]/D:propstat/D:status)"
#define XPATH_HREF "string(/D:multistatus/D:response[%d]/D:href)"
#define XPATH_DISPLAY_NAME "string(/D:multistatus/D:response[%d]/D:propstat/D:prop/D:displayname)"
#define XPATH_CALENDAR_COLOR "string(/D:multistatus/D:response[%d]/D:propstat/D:prop/APL:calendar-color)"
#define XPATH_RESOURCE_TYPE_ADDRESSBOOK "/D:multistatus/D:response[%d]/D:propstat/D:prop/D:resourcetype/B:addressbook"
#define XPATH_RESOURCE_TYPE_CALENDAR "/D:multistatus/D:response[%d]/D:propstat/D:prop/D:resourcetype/C:calendar"
#define XPATH_SUPPORTED_CALENDAR_COMPONENT_SET "/D:multistatus/D:response[%d]/D:propstat/D:prop/C:supported-calendar-component-set/C:comp"
#define XPATH_CALENDAR_COMP_TYPE "string(" XPATH_SUPPORTED_CALENDAR_COMPONENT_SET "[%d]/@name)"

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx,
            const gchar *format,
            ...)
{
	xmlXPathObjectPtr xpres;
	va_list args;
	gchar *expr;

	if (ctx == NULL)
		return NULL;

	va_start (args, format);
	expr = g_strdup_vprintf (format, args);
	va_end (args);

	xpres = xmlXPathEvalExpression ((xmlChar *) expr, ctx);
	g_free (expr);

	if (xpres == NULL)
		return NULL;

	if (xpres->type == XPATH_NODESET &&
	    xmlXPathNodeSetIsEmpty (xpres->nodesetval)) {
		xmlXPathFreeObject (xpres);
		return NULL;
	}

	return xpres;
}

static guint
xp_object_get_status (xmlXPathObjectPtr xpres)
{
	gboolean res;
	guint ret = 0;

	if (xpres == NULL)
		return ret;

	if (xpres->type == XPATH_STRING) {
		res = soup_headers_parse_status_line (
			(const gchar *) xpres->stringval,
			NULL,
			&ret,
			NULL);

		if (!res)
			ret = 0;
	}

	xmlXPathFreeObject (xpres);

	return ret;
}

static gchar *
xp_object_get_string (xmlXPathObjectPtr xpres)
{
	gchar *ret = NULL;

	if (xpres == NULL)
		return ret;

	if (xpres->type == XPATH_STRING) {
		ret = g_strdup ((gchar *) xpres->stringval);
	}

	xmlXPathFreeObject (xpres);

	return ret;
}

static void
add_source (ECollectionBackend *collection,
            OwnCloudSourceFoundCb found_cb,
            gpointer user_data,
            OwnCloudSourceType source_type,
            SoupURI *base_uri,
            const gchar *href,
            const gchar *display_name,
            const gchar *color)
{
	SoupURI *uri = NULL;

	if (!href || !display_name)
		return;

	if (!strstr (href, "://")) {
		soup_uri_set_path (base_uri, href);
	} else {
		uri = soup_uri_new (href);
	}

	found_cb (
		collection,
		source_type,
		uri ? uri : base_uri,
		display_name,
		color,
		user_data);

	if (uri)
		soup_uri_free (uri);
}

static void
enum_calendars (ECollectionBackend *collection,
                OwnCloudSourceFoundCb found_cb,
                gpointer user_data,
                /* const */ xmlXPathContextPtr xpctx,
                /* const */ xmlXPathObjectPtr xpathobj,
                gint response_index,
                SoupURI *base_uri,
                const gchar *href,
                const gchar *display_name,
                const gchar *color)
{
	gint ii, nn;

	if (href == NULL)
		return;

	if (display_name == NULL)
		return;

	if (xpctx == NULL)
		return;

	if (xpathobj == NULL)
		return;

	if (xpathobj->type != XPATH_NODESET)
		return;

	nn = xmlXPathNodeSetGetLength (xpathobj->nodesetval);
	for (ii = 0; ii < nn; ii++) {
		xmlXPathObjectPtr xpres;
		gchar *comp_type;

		xpres = xpath_eval (
			xpctx,
			XPATH_CALENDAR_COMP_TYPE,
			response_index,
			ii + 1);
		comp_type = xp_object_get_string (xpres);

		if (g_strcmp0 (comp_type, "VEVENT") == 0) {
			add_source (
				collection,
				found_cb,
				user_data,
				OwnCloud_Source_Events,
				base_uri,
				href,
				display_name,
				color);
		} else if (g_strcmp0 (comp_type, "VTODO") == 0) {
			add_source (
				collection,
				found_cb,
				user_data,
				OwnCloud_Source_Tasks,
				base_uri,
				href,
				display_name,
				color);
		} else if (g_strcmp0 (comp_type, "VJOURNAL") == 0) {
			add_source (
				collection,
				found_cb,
				user_data,
				OwnCloud_Source_Memos,
				base_uri,
				href,
				display_name,
				color);
		}

		g_free (comp_type);
	}
}

static void
parse_propfind_response (ECollectionBackend *collection,
                         OwnCloudSourceFoundCb found_cb,
                         gpointer user_data,
                         SoupURI *base_uri,
                         const gchar *body_str,
                         glong body_len)
{
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr xpathobj;
	xmlDocPtr doc;

	if (!body_str || !body_len || !base_uri)
		return;

	doc = xmlReadMemory (body_str, body_len, "response.xml", NULL, 0);
	if (!doc)
		return;

	xpctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (
		xpctx,
		(xmlChar *) "D",
		(xmlChar *) "DAV:");
	xmlXPathRegisterNs (
		xpctx,
		(xmlChar *) "B",
		(xmlChar *) "urn:ietf:params:xml:ns:carddav");
	xmlXPathRegisterNs (
		xpctx,
		(xmlChar *) "C",
		(xmlChar *) "urn:ietf:params:xml:ns:caldav");
	xmlXPathRegisterNs (
		xpctx,
		(xmlChar *) "CS",
		(xmlChar *) "http://calendarserver.org/ns/");
	xmlXPathRegisterNs (
		xpctx,
		(xmlChar *) "APL",
		(xmlChar *) "http://apple.com/ns/ical/");

	xpathobj = xpath_eval (xpctx, "/D:multistatus/D:response");
	if (xpathobj && xpathobj->type == XPATH_NODESET) {
		gint ii, nn;
		gchar *href, *display_name, *color;

		nn = xmlXPathNodeSetGetLength (xpathobj->nodesetval);
		for (ii = 0; ii < nn; ii++) {
			xmlXPathObjectPtr xpres;

			xpres = xpath_eval (xpctx, XPATH_STATUS, ii + 1);
			if (xp_object_get_status (xpres) != 200)
				continue;

			xpres = xpath_eval (xpctx, XPATH_HREF, ii + 1);
			href = xp_object_get_string (xpres);

			if (!href)
				continue;

			xpres = xpath_eval (xpctx, XPATH_DISPLAY_NAME, ii + 1);
			display_name = xp_object_get_string (xpres);

			xpres = xpath_eval (xpctx, XPATH_CALENDAR_COLOR, ii + 1);
			color = xp_object_get_string (xpres);

			if (display_name && *display_name) {
				xpres = xpath_eval (
					xpctx,
					XPATH_RESOURCE_TYPE_ADDRESSBOOK,
					ii + 1);
				if (xpres) {
					add_source (
						collection,
						found_cb,
						user_data,
						OwnCloud_Source_Contacts,
						base_uri,
						href,
						display_name,
						NULL);
					xmlXPathFreeObject (xpres);
				}

				xpres = xpath_eval (
					xpctx,
					XPATH_RESOURCE_TYPE_CALENDAR,
					ii + 1);
				if (xpres) {
					xmlXPathFreeObject (xpres);

					xpres = xpath_eval (
						xpctx,
						XPATH_SUPPORTED_CALENDAR_COMPONENT_SET,
						ii + 1);
					if (xpres) {
						enum_calendars (
							collection,
							found_cb,
							user_data,
							xpctx,
							xpres,
							ii + 1,
							base_uri,
							href,
							display_name,
							color);
						xmlXPathFreeObject (xpres);
					}
				}
			}

			g_free (display_name);
			g_free (color);
			g_free (href);
		}
	}

	if (xpathobj)
		xmlXPathFreeObject (xpathobj);
	xmlXPathFreeContext (xpctx);
	xmlFreeDoc (doc);
}

typedef struct _AuthenticateData {
	const gchar *username;
	const ENamedParameters *credentials;
} AuthenticateData;

static void
authenticate_cb (SoupSession *session,
                 SoupMessage *msg,
                 SoupAuth *auth,
                 gboolean retrying,
                 gpointer user_data)
{
	AuthenticateData *auth_data = user_data;

	g_return_if_fail (auth_data != NULL);
	g_return_if_fail (auth_data->credentials != NULL);

	if (retrying)
		return;

	if (auth_data->username && e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
		soup_auth_authenticate (auth, auth_data->username,
			e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_PASSWORD));
}

static gboolean
find_sources (ECollectionBackend *collection,
              OwnCloudSourceFoundCb found_cb,
              gpointer user_data,
              const gchar *base_url,
              const gchar *base_collection_path,
	      const ENamedParameters *credentials,
	      const gchar *default_username,
	      gchar **out_certificate_pem,
	      GTlsCertificateFlags *out_certificate_errors,
	      GCancellable *cancellable,
	      GError **error)
{
	const gchar *req_body =
		"<D:propfind "
			"xmlns:C=\"urn:ietf:params:xml:ns:caldav\" "
			"xmlns:IC=\"http://apple.com/ns/ical/\" "
			"xmlns:D=\"DAV:\">\n"
		"  <D:prop>\n"
		"    <D:displayname/>\n"
		"    <D:resourcetype/>\n"
		"    <C:supported-calendar-component-set/>\n"
		"    <IC:calendar-color/>\n"
		"  </D:prop>\n"
		"</D:propfind>\n";

	AuthenticateData auth_data;
	SoupSession *session;
	SoupMessage *msg;
	GString *url;
	const gchar *username;
	gboolean tested = FALSE;

	g_return_val_if_fail (base_url && *base_url, FALSE);
	g_return_val_if_fail (base_collection_path && *base_collection_path, FALSE);

	username = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME);
	if (!username || !*username)
		username = default_username;

	url = g_string_new (base_url);
	if (url->str[url->len - 1] != '/')
		g_string_append_c (url, '/');
	g_string_append (url, base_collection_path);
	g_string_append_c (url, '/');
	g_string_append (url, username);
	g_string_append_c (url, '/');

	msg = soup_message_new ("PROPFIND", url->str);

	if (!msg) {
		g_string_free (url, TRUE);
		return FALSE;
	}

	auth_data.username = username;
	auth_data.credentials = credentials;

	session = soup_session_sync_new ();
	g_object_set (
		session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_SSL_STRICT, TRUE,
		NULL);
	g_signal_connect (
		session, "authenticate",
		G_CALLBACK (authenticate_cb), &auth_data);

	e_binding_bind_property (
		collection, "proxy-resolver",
		session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	g_string_free (url, TRUE);

	soup_message_set_request (
		msg, "application/xml; charset=utf-8",
		SOUP_MEMORY_STATIC, req_body, strlen (req_body));

	e_soup_ssl_trust_connect (msg, e_backend_get_source (E_BACKEND (collection)));

	soup_session_send_message (session, msg);

	if (msg->status_code == SOUP_STATUS_MULTI_STATUS &&
	    msg->response_body && msg->response_body->length) {
		SoupURI *suri = soup_message_get_uri (msg);

		suri = soup_uri_copy (suri);

		parse_propfind_response (
			collection, found_cb, user_data, suri,
			msg->response_body->data,
			msg->response_body->length);

		soup_uri_free (suri);
		tested = TRUE;
	} else {
		g_set_error_literal (error, SOUP_HTTP_ERROR, msg->status_code, msg->reason_phrase);

		if (msg->status_code == SOUP_STATUS_SSL_FAILED && out_certificate_pem && out_certificate_errors) {
			GTlsCertificate *certificate = NULL;

			g_object_get (G_OBJECT (msg),
				"tls-certificate", &certificate,
				"tls-errors", out_certificate_errors,
				NULL);

			if (certificate) {
				g_object_get (certificate, "certificate-pem", out_certificate_pem, NULL);
				g_object_unref (certificate);
			}
		}
	}

	g_object_unref (msg);
	g_object_unref (session);

	return tested;
}

gboolean
owncloud_utils_search_server (ECollectionBackend *collection,
			      const ENamedParameters *credentials,
			      gchar **out_certificate_pem,
			      GTlsCertificateFlags *out_certificate_errors,
                              OwnCloudSourceFoundCb found_cb,
                              gpointer user_data,
			      GCancellable *cancellable,
			      GError **error)
{
	ESourceCollection *collection_extension;
	ESourceGoa *goa_extension;
	ESource *source;
	gchar *url, *username;
	gboolean res_calendars = FALSE;
	gboolean res_contacts = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (collection != NULL, FALSE);
	g_return_val_if_fail (found_cb != NULL, FALSE);

	source = e_backend_get_source (E_BACKEND (collection));
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	/* Ignore the request for non-GOA ownCloud sources */
	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_GOA))
		return FALSE;

	goa_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_GOA);

	username = e_source_collection_dup_identity (collection_extension);

	if (e_source_collection_get_calendar_enabled (collection_extension)) {
		url = e_source_goa_dup_calendar_url (goa_extension);

		if (url && *url)
			res_calendars = find_sources (
				collection, found_cb, user_data,
				url, "calendars", credentials, username,
				out_certificate_pem, out_certificate_errors,
				cancellable, &local_error);

		g_free (url);
	}

	if (e_source_collection_get_contacts_enabled (collection_extension) && !local_error) {
		url = e_source_goa_dup_contacts_url (goa_extension);

		if (url && *url)
			res_contacts = find_sources (
				collection, found_cb, user_data,
				url, "addressbooks", credentials, username,
				out_certificate_pem, out_certificate_errors,
				cancellable, &local_error);

		g_free (url);
	}

	if (local_error)
		g_propagate_error (error, local_error);

	g_free (username);

	return res_calendars || res_contacts;
}

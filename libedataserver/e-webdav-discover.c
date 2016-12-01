/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
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

#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "e-soup-auth-bearer.h"
#include "e-soup-ssl-trust.h"
#include "e-source-authentication.h"
#include "e-source-credentials-provider-impl-google.h"
#include "e-source-webdav.h"
#include "e-webdav-discover.h"

#define XC(string) ((xmlChar *) string)

/* Standard Namespaces */
#define NS_WEBDAV  "DAV:"
#define NS_CALDAV  "urn:ietf:params:xml:ns:caldav"
#define NS_CARDDAV "urn:ietf:params:xml:ns:carddav"

/* Application-Specific Namespaces */
#define NS_ICAL    "http://apple.com/ns/ical/"

/* Mainly for readability. */
enum {
	DEPTH_0 = 0,
	DEPTH_1 = 1
};

typedef struct _EWebDAVDiscoverContext {
	ESource *source;
	gchar *url_use_path;
	guint32 only_supports;
	ENamedParameters *credentials;
	gchar *out_certificate_pem;
	GTlsCertificateFlags out_certificate_errors;
	GSList *out_discovered_sources;
	GSList *out_calendar_user_addresses;
} EWebDAVDiscoverContext;

static EWebDAVDiscoverContext *
e_webdav_discover_context_new (ESource *source,
			       const gchar *url_use_path,
			       guint32 only_supports,
			       const ENamedParameters *credentials)
{
	EWebDAVDiscoverContext *context;

	context = g_new0 (EWebDAVDiscoverContext, 1);
	context->source = g_object_ref (source);
	context->url_use_path = g_strdup (url_use_path);
	context->only_supports = only_supports;
	context->credentials = e_named_parameters_new_clone (credentials);
	context->out_certificate_pem = NULL;
	context->out_certificate_errors = 0;
	context->out_discovered_sources = NULL;
	context->out_calendar_user_addresses = NULL;

	return context;
}

static void
e_webdav_discover_context_free (gpointer ptr)
{
	EWebDAVDiscoverContext *context = ptr;

	if (!context)
		return;

	g_clear_object (&context->source);
	g_free (context->url_use_path);
	e_named_parameters_free (context->credentials);
	g_free (context->out_certificate_pem);
	e_webdav_discover_free_discovered_sources (context->out_discovered_sources);
	g_slist_free_full (context->out_calendar_user_addresses, g_free);
	g_free (context);
}

static gchar *
e_webdav_discover_make_href_full_uri (SoupURI *base_uri,
				      const gchar *href)
{
	SoupURI *soup_uri;
	gchar *full_uri;

	if (!base_uri || !href)
		return g_strdup (href);

	if (strstr (href, "://"))
		return g_strdup (href);

	soup_uri = soup_uri_copy (base_uri);
	soup_uri_set_path (soup_uri, href);
	soup_uri_set_user (soup_uri, NULL);
	soup_uri_set_password (soup_uri, NULL);

	full_uri = soup_uri_to_string (soup_uri, FALSE);

	soup_uri_free (soup_uri);

	return full_uri;
}

static void
e_webdav_discover_redirect (SoupMessage *message,
			    SoupSession *session)
{
	SoupURI *soup_uri;
	const gchar *location;

	if (!SOUP_STATUS_IS_REDIRECTION (message->status_code))
		return;

	location = soup_message_headers_get_list (message->response_headers, "Location");

	if (location == NULL)
		return;

	soup_uri = soup_uri_new_with_base (soup_message_get_uri (message), location);

	if (soup_uri == NULL) {
		soup_message_set_status_full (
			message, SOUP_STATUS_MALFORMED,
			_("Invalid Redirect URL"));
		return;
	}

	soup_message_set_uri (message, soup_uri);
	soup_session_requeue_message (session, message);

	soup_uri_free (soup_uri);
}

static gconstpointer
compat_libxml_output_buffer_get_content (xmlOutputBufferPtr buf,
                                         gsize *out_len)
{
#ifdef LIBXML2_NEW_BUFFER
	*out_len = xmlOutputBufferGetSize (buf);
	return xmlOutputBufferGetContent (buf);
#else
	*out_len = buf->buffer->use;
	return buf->buffer->content;
#endif
}

static G_GNUC_NULL_TERMINATED SoupMessage *
e_webdav_discover_new_propfind (SoupSession *session,
				SoupURI *soup_uri,
				gint depth,
				...)
{
	GHashTable *namespaces;
	SoupMessage *message;
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlNodePtr node;
	xmlNsPtr ns;
	xmlOutputBufferPtr output;
	gconstpointer content;
	gsize length;
	gpointer key;
	va_list va;

	/* Construct the XML content. */

	doc = xmlNewDoc (XC ("1.0"));
	node = xmlNewDocNode (doc, NULL, XC ("propfind"), NULL);

	/* Build a hash table of namespace URIs to xmlNs structs. */
	namespaces = g_hash_table_new (NULL, NULL);

	ns = xmlNewNs (node, XC (NS_CALDAV), XC ("C"));
	g_hash_table_insert (namespaces, (gpointer) NS_CALDAV, ns);

	ns = xmlNewNs (node, XC (NS_CARDDAV), XC ("A"));
	g_hash_table_insert (namespaces, (gpointer) NS_CARDDAV, ns);

	ns = xmlNewNs (node, XC (NS_ICAL), XC ("IC"));
	g_hash_table_insert (namespaces, (gpointer) NS_ICAL, ns);

	/* Add WebDAV last since we use it below. */
	ns = xmlNewNs (node, XC (NS_WEBDAV), XC ("D"));
	g_hash_table_insert (namespaces, (gpointer) NS_WEBDAV, ns);

	xmlSetNs (node, ns);
	xmlDocSetRootElement (doc, node);

	node = xmlNewTextChild (node, ns, XC ("prop"), NULL);

	va_start (va, depth);
	while ((key = va_arg (va, gpointer)) != NULL) {
		xmlChar *name;

		ns = g_hash_table_lookup (namespaces, key);
		name = va_arg (va, xmlChar *);

		if (ns != NULL && name != NULL)
			xmlNewTextChild (node, ns, name, NULL);
		else
			g_warn_if_reached ();
	}
	va_end (va);

	g_hash_table_destroy (namespaces);

	/* Construct the SoupMessage. */

	message = soup_message_new_from_uri (SOUP_METHOD_PROPFIND, soup_uri);

	soup_message_set_flags (message, SOUP_MESSAGE_NO_REDIRECT);

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);

	soup_message_headers_append (
		message->request_headers,
		"Connection", "close");

	soup_message_headers_append (
		message->request_headers,
		"Depth", (depth == 0) ? "0" : "1");

	output = xmlAllocOutputBuffer (NULL);

	root = xmlDocGetRootElement (doc);
	xmlNodeDumpOutput (output, doc, root, 0, 1, NULL);
	xmlOutputBufferFlush (output);

	content = compat_libxml_output_buffer_get_content (output, &length);

	soup_message_set_request (
		message, "application/xml", SOUP_MEMORY_COPY,
		content, length);

	xmlOutputBufferClose (output);
	xmlFreeDoc (doc);

	soup_message_add_header_handler (
		message, "got-body", "Location",
		G_CALLBACK (e_webdav_discover_redirect), session);

	return message;
}

static xmlXPathObjectPtr
e_webdav_discover_get_xpath (xmlXPathContextPtr xp_ctx,
			     const gchar *path_format,
			     ...)
{
	xmlXPathObjectPtr xp_obj;
	va_list va;
	gchar *path;

	va_start (va, path_format);
	path = g_strdup_vprintf (path_format, va);
	va_end (va);

	xp_obj = xmlXPathEvalExpression (XC (path), xp_ctx);

	g_free (path);

	if (xp_obj == NULL)
		return NULL;

	if (xp_obj->type != XPATH_NODESET) {
		xmlXPathFreeObject (xp_obj);
		return NULL;
	}

	if (xmlXPathNodeSetGetLength (xp_obj->nodesetval) == 0) {
		xmlXPathFreeObject (xp_obj);
		return NULL;
	}

	return xp_obj;
}

static gchar *
e_webdav_discover_get_xpath_string (xmlXPathContextPtr xp_ctx,
				    const gchar *path_format,
				    ...)
{
	xmlXPathObjectPtr xp_obj;
	va_list va;
	gchar *path;
	gchar *expression;
	gchar *string = NULL;

	va_start (va, path_format);
	path = g_strdup_vprintf (path_format, va);
	va_end (va);

	expression = g_strdup_printf ("string(%s)", path);
	xp_obj = xmlXPathEvalExpression (XC (expression), xp_ctx);
	g_free (expression);

	g_free (path);

	if (xp_obj == NULL)
		return NULL;

	if (xp_obj->type == XPATH_STRING)
		string = g_strdup ((gchar *) xp_obj->stringval);

	/* If the string is empty, return NULL. */
	if (string != NULL && *string == '\0') {
		g_free (string);
		string = NULL;
	}

	xmlXPathFreeObject (xp_obj);

	return string;
}

static gboolean
e_webdav_discover_setup_bearer_auth (ESource *source,
				     const ENamedParameters *credentials,
				     ESoupAuthBearer *bearer,
				     GCancellable *cancellable,
				     GError **error)
{
	gchar *access_token = NULL;
	gint expires_in_seconds = -1;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	success = e_util_get_source_oauth2_access_token_sync (source, credentials,
		&access_token, &expires_in_seconds, cancellable, error);

	if (success)
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);

	g_free (access_token);

	return success;
}

typedef struct _AuthenticateData {
	ESource *source;
	const ENamedParameters *credentials;
} AuthenticateData;

static void
e_webdav_discover_authenticate_cb (SoupSession *session,
				   SoupMessage *msg,
				   SoupAuth *auth,
				   gboolean retrying,
				   gpointer user_data)
{
	AuthenticateData *auth_data = user_data;

	g_return_if_fail (auth_data != NULL);

	if (retrying)
		return;

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		GError *local_error = NULL;

		e_webdav_discover_setup_bearer_auth (auth_data->source, auth_data->credentials,
			E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

		if (local_error != NULL) {
			soup_message_set_status_full (msg, SOUP_STATUS_FORBIDDEN, local_error->message);

			g_error_free (local_error);
		}
	} else {
		gchar *auth_user = NULL;

		if (e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_USERNAME))
			auth_user = g_strdup (e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_USERNAME));

		if (auth_user && !*auth_user) {
			g_free (auth_user);
			auth_user = NULL;
		}

		if (!auth_user) {
			ESourceAuthentication *auth_extension;

			auth_extension = e_source_get_extension (auth_data->source, E_SOURCE_EXTENSION_AUTHENTICATION);
			auth_user = e_source_authentication_dup_user (auth_extension);
		}

		if (!auth_user || !*auth_user || !auth_data->credentials || !e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
			soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
		else
			soup_auth_authenticate (auth, auth_user, e_named_parameters_get (auth_data->credentials, E_SOURCE_CREDENTIAL_PASSWORD));

		g_free (auth_user);
	}
}

static gboolean
e_webdav_discover_check_successful (SoupMessage *message,
				    gchar **out_certificate_pem,
				    GTlsCertificateFlags *out_certificate_errors,
				    GError **error)
{
	GIOErrorEnum error_code;

	g_return_val_if_fail (message != NULL, FALSE);

	/* Loosely copied from the GVFS DAV backend. */

	if (SOUP_STATUS_IS_SUCCESSFUL (message->status_code))
		return TRUE;

	switch (message->status_code) {
		case SOUP_STATUS_CANCELLED:
			error_code = G_IO_ERROR_CANCELLED;
			break;
		case SOUP_STATUS_NOT_FOUND:
			error_code = G_IO_ERROR_NOT_FOUND;
			break;
		case SOUP_STATUS_UNAUTHORIZED:
		case SOUP_STATUS_FORBIDDEN:
			g_set_error (
				error, SOUP_HTTP_ERROR, message->status_code,
				_("HTTP Error: %s"), message->reason_phrase);
			return FALSE;
		case SOUP_STATUS_PAYMENT_REQUIRED:
			error_code = G_IO_ERROR_PERMISSION_DENIED;
			break;
		case SOUP_STATUS_REQUEST_TIMEOUT:
			error_code = G_IO_ERROR_TIMED_OUT;
			break;
		case SOUP_STATUS_CANT_RESOLVE:
			error_code = G_IO_ERROR_HOST_NOT_FOUND;
			break;
		case SOUP_STATUS_NOT_IMPLEMENTED:
			error_code = G_IO_ERROR_NOT_SUPPORTED;
			break;
		case SOUP_STATUS_INSUFFICIENT_STORAGE:
			error_code = G_IO_ERROR_NO_SPACE;
			break;
		case SOUP_STATUS_SSL_FAILED:
			if (out_certificate_pem) {
				GTlsCertificate *certificate = NULL;

				g_free (*out_certificate_pem);
				*out_certificate_pem = NULL;

				g_object_get (G_OBJECT (message), "tls-certificate", &certificate, NULL);

				if (certificate) {
					g_object_get (certificate, "certificate-pem", out_certificate_pem, NULL);
					g_object_unref (certificate);
				}
			}

			if (out_certificate_errors) {
				*out_certificate_errors = 0;
				g_object_get (G_OBJECT (message), "tls-errors", out_certificate_errors, NULL);
			}

			g_set_error (
				error, SOUP_HTTP_ERROR, message->status_code,
				_("HTTP Error: %s"), message->reason_phrase);
			return FALSE;
		default:
			error_code = G_IO_ERROR_FAILED;
			break;
	}

	g_set_error (
		error, G_IO_ERROR, error_code,
		_("HTTP Error: %s"), message->reason_phrase);

	return FALSE;
}

static xmlDocPtr
e_webdav_discover_parse_xml (SoupMessage *message,
			     const gchar *expected_name,
			     gchar **out_certificate_pem,
			     GTlsCertificateFlags *out_certificate_errors,
			     GError **error)
{
	xmlDocPtr doc;
	xmlNodePtr root;

	if (!e_webdav_discover_check_successful (message, out_certificate_pem, out_certificate_errors, error))
		return NULL;

	doc = xmlReadMemory (
		message->response_body->data,
		message->response_body->length,
		"response.xml", NULL,
		XML_PARSE_NONET |
		XML_PARSE_NOWARNING |
		XML_PARSE_NOCDATA |
		XML_PARSE_COMPACT);

	if (doc == NULL) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Could not parse response"));
		return NULL;
	}

	root = xmlDocGetRootElement (doc);

	if (root == NULL || root->children == NULL) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Empty response"));
		xmlFreeDoc (doc);
		return NULL;
	}

	if (g_strcmp0 ((gchar *) root->name, expected_name) != 0) {
		g_set_error_literal (
			error, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Unexpected reply from server"));
		xmlFreeDoc (doc);
		return NULL;
	}

	return doc;
}

static void
e_webdav_discover_process_user_address_set (xmlXPathContextPtr xp_ctx,
					    GSList **out_calendar_user_addresses)
{
	xmlXPathObjectPtr xp_obj;
	gint ii, length;

	if (!out_calendar_user_addresses)
		return;

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/C:calendar-user-address-set");

	if (xp_obj == NULL)
		return;

	length = xmlXPathNodeSetGetLength (xp_obj->nodesetval);

	for (ii = 0; ii < length; ii++) {
		GSList *duplicate;
		const gchar *address;
		gchar *href;

		href = e_webdav_discover_get_xpath_string (
			xp_ctx,
			"/D:multistatus"
			"/D:response"
			"/D:propstat"
			"/D:prop"
			"/C:calendar-user-address-set"
			"/D:href[%d]", ii + 1);

		if (href == NULL)
			continue;

		if (!g_str_has_prefix (href, "mailto:")) {
			g_free (href);
			continue;
		}

		/* strlen("mailto:") == 7 */
		address = href + 7;

		/* Avoid duplicates. */
		duplicate = g_slist_find_custom (
			*out_calendar_user_addresses,
			address, (GCompareFunc) g_ascii_strcasecmp);

		if (duplicate != NULL) {
			g_free (href);
			continue;
		}

		*out_calendar_user_addresses = g_slist_prepend (
			*out_calendar_user_addresses, g_strdup (address));

		g_free (href);
	}

	xmlXPathFreeObject (xp_obj);
}

static guint32
e_webdav_discover_get_supported_component_set (xmlXPathContextPtr xp_ctx,
					       gint response_index,
					       gint propstat_index)
{
	xmlXPathObjectPtr xp_obj;
	guint32 set = 0;
	gint ii, length;

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/C:supported-calendar-component-set"
		"/C:comp",
		response_index,
		propstat_index);

	/* If the property is not present, assume all component
	 * types are supported.  (RFC 4791, Section 5.2.3) */
	if (xp_obj == NULL)
		return E_WEBDAV_DISCOVER_SUPPORTS_EVENTS |
		       E_WEBDAV_DISCOVER_SUPPORTS_MEMOS |
		       E_WEBDAV_DISCOVER_SUPPORTS_TASKS;

	length = xmlXPathNodeSetGetLength (xp_obj->nodesetval);

	for (ii = 0; ii < length; ii++) {
		gchar *name;

		name = e_webdav_discover_get_xpath_string (
			xp_ctx,
			"/D:multistatus"
			"/D:response[%d]"
			"/D:propstat[%d]"
			"/D:prop"
			"/C:supported-calendar-component-set"
			"/C:comp[%d]"
			"/@name",
			response_index,
			propstat_index,
			ii + 1);

		if (name == NULL)
			continue;

		if (g_ascii_strcasecmp (name, "VEVENT") == 0)
			set |= E_WEBDAV_DISCOVER_SUPPORTS_EVENTS;
		else if (g_ascii_strcasecmp (name, "VJOURNAL") == 0)
			set |= E_WEBDAV_DISCOVER_SUPPORTS_MEMOS;
		else if (g_ascii_strcasecmp (name, "VTODO") == 0)
			set |= E_WEBDAV_DISCOVER_SUPPORTS_TASKS;

		g_free (name);
	}

	xmlXPathFreeObject (xp_obj);

	return set;
}

static void
e_webdav_discover_process_calendar_response_propstat (SoupMessage *message,
						      xmlXPathContextPtr xp_ctx,
						      gint response_index,
						      gint propstat_index,
						      GSList **out_discovered_sources)
{
	xmlXPathObjectPtr xp_obj;
	guint32 comp_set;
	gchar *color_spec;
	gchar *display_name;
	gchar *description;
	gchar *href_encoded;
	gchar *status_line;
	guint status;
	gboolean success;
	EWebDAVDiscoveredSource *discovered_source;

	if (!out_discovered_sources)
		return;

	status_line = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:status",
		response_index,
		propstat_index);

	if (status_line == NULL)
		return;

	success = soup_headers_parse_status_line (
		status_line, NULL, &status, NULL);

	g_free (status_line);

	if (!success || status != SOUP_STATUS_OK)
		return;

	comp_set = e_webdav_discover_get_supported_component_set (xp_ctx, response_index, propstat_index);
	if (comp_set == E_WEBDAV_DISCOVER_SUPPORTS_NONE)
		return;

	href_encoded = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:href",
		response_index);

	if (href_encoded == NULL)
		return;

	/* Make sure the resource is a calendar. */

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/D:resourcetype"
		"/C:calendar",
		response_index,
		propstat_index);

	if (xp_obj == NULL) {
		g_free (href_encoded);
		return;
	}

	xmlXPathFreeObject (xp_obj);

	/* Get the display name or fall back to the href. */

	display_name = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/D:displayname",
		response_index,
		propstat_index);

	if (display_name == NULL) {
		gchar *href_decoded = soup_uri_decode (href_encoded);

		if (href_decoded) {
			gchar *cp;

			/* Use the last non-empty path segment. */
			while ((cp = strrchr (href_decoded, '/')) != NULL) {
				if (*(cp + 1) == '\0')
					*cp = '\0';
				else {
					display_name = g_strdup (cp + 1);
					break;
				}
			}
		}

		g_free (href_decoded);
	}

	description = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/C:calendar-description",
		response_index,
		propstat_index);

	/* Get the color specification string. */

	color_spec = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/IC:calendar-color",
		response_index,
		propstat_index);

	discovered_source = g_new0 (EWebDAVDiscoveredSource, 1);
	discovered_source->href = e_webdav_discover_make_href_full_uri (soup_message_get_uri (message), href_encoded);
	discovered_source->supports = comp_set;
	discovered_source->display_name = g_strdup (display_name);
	discovered_source->description = g_strdup (description);
	discovered_source->color = g_strdup (color_spec);

	*out_discovered_sources = g_slist_prepend (*out_discovered_sources, discovered_source);

	g_free (href_encoded);
	g_free (display_name);
	g_free (description);
	g_free (color_spec);
}

static void
e_webdav_discover_traverse_responses (SoupMessage *message,
				      xmlXPathContextPtr xp_ctx,
				      GSList **out_discovered_sources,
				      void (* func) (
						SoupMessage *message,
						xmlXPathContextPtr xp_ctx,
						gint response_index,
						gint propstat_index,
						GSList **out_discovered_sources))
{
	xmlXPathObjectPtr xp_obj_response;

	xp_obj_response = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response");

	if (xp_obj_response != NULL) {
		gint response_index, response_length;

		response_length = xmlXPathNodeSetGetLength (xp_obj_response->nodesetval);

		for (response_index = 0; response_index < response_length; response_index++) {
			xmlXPathObjectPtr xp_obj_propstat;

			xp_obj_propstat = e_webdav_discover_get_xpath (
				xp_ctx,
				"/D:multistatus"
				"/D:response[%d]"
				"/D:propstat",
				response_index + 1);

			if (xp_obj_propstat != NULL) {
				gint propstat_index, propstat_length;

				propstat_length = xmlXPathNodeSetGetLength (xp_obj_propstat->nodesetval);

				for (propstat_index = 0; propstat_index < propstat_length; propstat_index++) {
					func (message, xp_ctx, response_index + 1, propstat_index + 1, out_discovered_sources);
				}

				xmlXPathFreeObject (xp_obj_propstat);
			}
		}

		xmlXPathFreeObject (xp_obj_response);
	}
}

static gboolean
e_webdav_discover_get_calendar_collection_details (SoupSession *session,
						   SoupMessage *message,
						   const gchar *path_or_uri,
						   ESource *source,
						   gchar **out_certificate_pem,
						   GTlsCertificateFlags *out_certificate_errors,
						   GSList **out_discovered_sources,
						   GCancellable *cancellable,
						   GError **error)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xp_ctx;
	SoupURI *soup_uri;
	GError *local_error = NULL;

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	soup_uri = soup_uri_new (path_or_uri);
	if (!soup_uri ||
	    !soup_uri_get_scheme (soup_uri) ||
	    !soup_uri_get_host (soup_uri) ||
	    !soup_uri_get_path (soup_uri) ||
	    !*soup_uri_get_scheme (soup_uri) ||
	    !*soup_uri_get_host (soup_uri) ||
	    !*soup_uri_get_path (soup_uri)) {
		/* it's a path only, not full uri */
		if (soup_uri)
			soup_uri_free (soup_uri);
		soup_uri = soup_uri_copy (soup_message_get_uri (message));
		soup_uri_set_path (soup_uri, path_or_uri);
	}

	message = e_webdav_discover_new_propfind (
		session, soup_uri, DEPTH_1,
		NS_WEBDAV, XC ("displayname"),
		NS_WEBDAV, XC ("resourcetype"),
		NS_CALDAV, XC ("calendar-description"),
		NS_CALDAV, XC ("supported-calendar-component-set"),
		NS_CALDAV, XC ("calendar-user-address-set"),
		NS_ICAL,   XC ("calendar-color"),
		NULL);

	e_soup_ssl_trust_connect (message, source);

	/* This takes ownership of the message. */
	soup_session_send_message (session, message);

	if (message->status_code == SOUP_STATUS_BAD_REQUEST) {
		g_clear_object (&message);

		message = e_webdav_discover_new_propfind (
			session, soup_uri, DEPTH_0,
			NS_WEBDAV, XC ("displayname"),
			NS_WEBDAV, XC ("resourcetype"),
			NS_CALDAV, XC ("calendar-description"),
			NS_CALDAV, XC ("supported-calendar-component-set"),
			NS_CALDAV, XC ("calendar-user-address-set"),
			NS_ICAL,   XC ("calendar-color"),
			NULL);

		e_soup_ssl_trust_connect (message, source);
		soup_session_send_message (session, message);
	}

	soup_uri_free (soup_uri);

	doc = e_webdav_discover_parse_xml (message, "multistatus", out_certificate_pem, out_certificate_errors, &local_error);
	if (!doc) {
		g_clear_object (&message);

		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			/* Ignore these errors, but still propagate them. */
			g_propagate_error (error, local_error);
			return TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		return FALSE;
	}

	xp_ctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (xp_ctx, XC ("D"), XC (NS_WEBDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("C"), XC (NS_CALDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("A"), XC (NS_CARDDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("IC"), XC (NS_ICAL));

	e_webdav_discover_traverse_responses (message, xp_ctx, out_discovered_sources,
		e_webdav_discover_process_calendar_response_propstat);

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	g_clear_object (&message);

	return TRUE;
}

static gboolean
e_webdav_discover_process_calendar_home_set (SoupSession *session,
					     SoupMessage *message,
					     ESource *source,
					     gchar **out_certificate_pem,
					     GTlsCertificateFlags *out_certificate_errors,
					     GSList **out_discovered_sources,
					     GSList **out_calendar_user_addresses,
					     GCancellable *cancellable,
					     GError **error)
{
	SoupURI *soup_uri;
	xmlDocPtr doc;
	xmlXPathContextPtr xp_ctx;
	xmlXPathObjectPtr xp_obj;
	gchar *calendar_home_set;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (SOUP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_discovered_sources != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	doc = e_webdav_discover_parse_xml (message, "multistatus", out_certificate_pem, out_certificate_errors, &local_error);

	if (!doc) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			/* Ignore these errors, but still propagate them. */
			g_propagate_error (error, local_error);
			return TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		return FALSE;
	}

	xp_ctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (xp_ctx, XC ("D"), XC (NS_WEBDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("C"), XC (NS_CALDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("A"), XC (NS_CARDDAV));

	/* Record any "C:calendar-user-address-set" properties. */
	e_webdav_discover_process_user_address_set (xp_ctx, out_calendar_user_addresses);

	/* Try to find the calendar home URL using the
	 * following properties in order of preference:
	 *
	 *   "C:calendar-home-set"
	 *   "D:current-user-principal"
	 *   "D:principal-URL"
	 *
	 * If the second or third URL preference is used, rerun
	 * the PROPFIND method on that URL at Depth=1 in hopes
	 * of getting a proper "C:calendar-home-set" property.
	 */

	/* FIXME There can be multiple "D:href" elements for a
	 *       "C:calendar-home-set".  We're only processing
	 *       the first one.  Need to iterate over them. */

	calendar_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/C:calendar-home-set"
		"/D:href");

	if (calendar_home_set != NULL)
		goto get_collection_details;

	g_free (calendar_home_set);

	calendar_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:current-user-principal"
		"/D:href");

	if (calendar_home_set != NULL)
		goto retry_propfind;

	g_free (calendar_home_set);

	calendar_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:principal-URL"
		"/D:href");

	if (calendar_home_set != NULL)
		goto retry_propfind;

	g_free (calendar_home_set);
	calendar_home_set = NULL;

	/* None of the aforementioned properties are present.  If the
	 * user-supplied CalDAV URL is a calendar resource, use that. */

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:resourcetype"
		"/C:calendar");

	if (xp_obj != NULL) {
		soup_uri = soup_message_get_uri (message);

		if (soup_uri->path != NULL && *soup_uri->path != '\0') {
			gchar *slash;

			soup_uri = soup_uri_copy (soup_uri);

			slash = strrchr (soup_uri->path, '/');
			while (slash != NULL && slash != soup_uri->path) {

				if (slash[1] != '\0') {
					slash[1] = '\0';
					calendar_home_set =
						g_strdup (soup_uri->path);
					break;
				}

				slash[0] = '\0';
				slash = strrchr (soup_uri->path, '/');
			}

			soup_uri_free (soup_uri);
		}

		xmlXPathFreeObject (xp_obj);
	}

	if (calendar_home_set == NULL || *calendar_home_set == '\0') {
		g_free (calendar_home_set);
		xmlXPathFreeContext (xp_ctx);
		xmlFreeDoc (doc);
		return TRUE;
	}

 get_collection_details:

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	if (!e_webdav_discover_get_calendar_collection_details (
		session, message, calendar_home_set, source,
		out_certificate_pem, out_certificate_errors, out_discovered_sources,
		cancellable, error)) {
		g_free (calendar_home_set);
		return FALSE;
	}

	g_free (calendar_home_set);

	return TRUE;

 retry_propfind:

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	soup_uri = soup_uri_copy (soup_message_get_uri (message));
	soup_uri_set_path (soup_uri, calendar_home_set);

	/* Note that we omit "D:resourcetype", "D:current-user-principal"
	 * and "D:principal-URL" in order to short-circuit the recursion. */
	message = e_webdav_discover_new_propfind (
		session, soup_uri, DEPTH_1,
		NS_CALDAV, XC ("calendar-home-set"),
		NS_CALDAV, XC ("calendar-user-address-set"),
		NULL);

	e_soup_ssl_trust_connect (message, source);

	/* This takes ownership of the message. */
	soup_session_send_message (session, message);

	if (message->status_code == SOUP_STATUS_BAD_REQUEST) {
		g_clear_object (&message);

		message = e_webdav_discover_new_propfind (
			session, soup_uri, DEPTH_0,
			NS_CALDAV, XC ("calendar-home-set"),
			NS_CALDAV, XC ("calendar-user-address-set"),
			NULL);

		e_soup_ssl_trust_connect (message, source);
		soup_session_send_message (session, message);
	}

	soup_uri_free (soup_uri);

	g_free (calendar_home_set);

	success = e_webdav_discover_process_calendar_home_set (session, message, source,
		out_certificate_pem, out_certificate_errors, out_discovered_sources, out_calendar_user_addresses,
		cancellable, error);

	g_object_unref (message);

	return success;
}

static void
e_webdav_discover_process_addressbook_response_propstat (SoupMessage *message,
							 xmlXPathContextPtr xp_ctx,
							 gint response_index,
							 gint propstat_index,
							 GSList **out_discovered_sources)
{
	xmlXPathObjectPtr xp_obj;
	gchar *display_name;
	gchar *description;
	gchar *href_encoded;
	gchar *status_line;
	guint status;
	gboolean success;
	EWebDAVDiscoveredSource *discovered_source;

	if (!out_discovered_sources)
		return;

	status_line = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:status",
		response_index,
		propstat_index);

	if (status_line == NULL)
		return;

	success = soup_headers_parse_status_line (
		status_line, NULL, &status, NULL);

	g_free (status_line);

	if (!success || status != SOUP_STATUS_OK)
		return;

	href_encoded = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:href",
		response_index);

	if (href_encoded == NULL)
		return;

	/* Make sure the resource is an addressbook. */

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/D:resourcetype"
		"/A:addressbook",
		response_index,
		propstat_index);

	if (xp_obj == NULL) {
		g_free (href_encoded);
		return;
	}

	xmlXPathFreeObject (xp_obj);

	/* Get the display name or fall back to the href. */

	display_name = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/D:displayname",
		response_index,
		propstat_index);

	if (display_name == NULL) {
		gchar *href_decoded = soup_uri_decode (href_encoded);

		if (href_decoded) {
			gchar *cp;

			/* Use the last non-empty path segment. */
			while ((cp = strrchr (href_decoded, '/')) != NULL) {
				if (*(cp + 1) == '\0')
					*cp = '\0';
				else {
					display_name = g_strdup (cp + 1);
					break;
				}
			}
		}

		g_free (href_decoded);
	}

	description = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response[%d]"
		"/D:propstat[%d]"
		"/D:prop"
		"/A:addressbook-description",
		response_index,
		propstat_index);

	discovered_source = g_new0 (EWebDAVDiscoveredSource, 1);
	discovered_source->href = e_webdav_discover_make_href_full_uri (soup_message_get_uri (message), href_encoded);
	discovered_source->supports = E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS;
	discovered_source->display_name = g_strdup (display_name);
	discovered_source->description = g_strdup (description);
	discovered_source->color = NULL;

	*out_discovered_sources = g_slist_prepend (*out_discovered_sources, discovered_source);

	g_free (href_encoded);
	g_free (display_name);
	g_free (description);
}

static gboolean
e_webdav_discover_get_addressbook_collection_details (SoupSession *session,
						      SoupMessage *message,
						      const gchar *path_or_uri,
						      ESource *source,
						      gchar **out_certificate_pem,
						      GTlsCertificateFlags *out_certificate_errors,
						      GSList **out_discovered_sources,
						      GCancellable *cancellable,
						      GError **error)
{
	xmlDocPtr doc;
	xmlXPathContextPtr xp_ctx;
	SoupURI *soup_uri;
	GError *local_error = NULL;

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	soup_uri = soup_uri_new (path_or_uri);
	if (!soup_uri ||
	    !soup_uri_get_scheme (soup_uri) ||
	    !soup_uri_get_host (soup_uri) ||
	    !soup_uri_get_path (soup_uri) ||
	    !*soup_uri_get_scheme (soup_uri) ||
	    !*soup_uri_get_host (soup_uri) ||
	    !*soup_uri_get_path (soup_uri)) {
		/* it's a path only, not full uri */
		if (soup_uri)
			soup_uri_free (soup_uri);
		soup_uri = soup_uri_copy (soup_message_get_uri (message));
		soup_uri_set_path (soup_uri, path_or_uri);
	}

	message = e_webdav_discover_new_propfind (
		session, soup_uri, DEPTH_1,
		NS_WEBDAV, XC ("displayname"),
		NS_WEBDAV, XC ("resourcetype"),
		NS_CARDDAV, XC ("addressbook-description"),
		NULL);

	e_soup_ssl_trust_connect (message, source);

	/* This takes ownership of the message. */
	soup_session_send_message (session, message);

	soup_uri_free (soup_uri);

	doc = e_webdav_discover_parse_xml (message, "multistatus", out_certificate_pem, out_certificate_errors, &local_error);
	if (!doc) {
		g_clear_object (&message);

		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			/* Ignore these errors, but still propagate them. */
			g_propagate_error (error, local_error);
			return TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		return FALSE;
	}

	xp_ctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (xp_ctx, XC ("D"), XC (NS_WEBDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("C"), XC (NS_CALDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("A"), XC (NS_CARDDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("IC"), XC (NS_ICAL));

	e_webdav_discover_traverse_responses (message, xp_ctx, out_discovered_sources,
		e_webdav_discover_process_addressbook_response_propstat);

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	g_clear_object (&message);

	return TRUE;
}

static gboolean
e_webdav_discover_process_addressbook_home_set (SoupSession *session,
						SoupMessage *message,
						ESource *source,
						gchar **out_certificate_pem,
						GTlsCertificateFlags *out_certificate_errors,
						GSList **out_discovered_sources,
						GCancellable *cancellable,
						GError **error)
{
	SoupURI *soup_uri;
	xmlDocPtr doc;
	xmlXPathContextPtr xp_ctx;
	xmlXPathObjectPtr xp_obj;
	gchar *addressbook_home_set;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (SOUP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);
	g_return_val_if_fail (out_discovered_sources != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	doc = e_webdav_discover_parse_xml (message, "multistatus", out_certificate_pem, out_certificate_errors, &local_error);
	if (!doc) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			/* Ignore these errors, but still propagate them. */
			g_propagate_error (error, local_error);
			return TRUE;
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		return FALSE;
	}

	xp_ctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (xp_ctx, XC ("D"), XC (NS_WEBDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("C"), XC (NS_CALDAV));
	xmlXPathRegisterNs (xp_ctx, XC ("A"), XC (NS_CARDDAV));

	/* Try to find the addressbook home URL using the
	 * following properties in order of preference:
	 *
	 *   "A:addressbook-home-set"
	 *   "D:current-user-principal"
	 *   "D:principal-URL"
	 *
	 * If the second or third URL preference is used, rerun
	 * the PROPFIND method on that URL at Depth=1 in hopes
	 * of getting a proper "A:addressbook-home-set" property.
	 */

	/* FIXME There can be multiple "D:href" elements for a
	 *       "A:addressbook-home-set".  We're only processing
	 *       the first one.  Need to iterate over them. */

	addressbook_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/A:addressbook-home-set"
		"/D:href");

	if (addressbook_home_set != NULL)
		goto get_collection_details;

	g_free (addressbook_home_set);

	addressbook_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:current-user-principal"
		"/D:href");

	if (addressbook_home_set != NULL)
		goto retry_propfind;

	g_free (addressbook_home_set);

	addressbook_home_set = e_webdav_discover_get_xpath_string (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:principal-URL"
		"/D:href");

	if (addressbook_home_set != NULL)
		goto retry_propfind;

	g_free (addressbook_home_set);
	addressbook_home_set = NULL;

	/* None of the aforementioned properties are present.  If the
	 * user-supplied CardDAV URL is an addressbook resource, use that. */

	xp_obj = e_webdav_discover_get_xpath (
		xp_ctx,
		"/D:multistatus"
		"/D:response"
		"/D:propstat"
		"/D:prop"
		"/D:resourcetype"
		"/A:addressbook");

	if (xp_obj != NULL) {
		soup_uri = soup_message_get_uri (message);

		if (soup_uri->path != NULL && *soup_uri->path != '\0') {
			gchar *slash;

			soup_uri = soup_uri_copy (soup_uri);

			slash = strrchr (soup_uri->path, '/');
			while (slash != NULL && slash != soup_uri->path) {

				if (slash[1] != '\0') {
					slash[1] = '\0';
					addressbook_home_set =
						g_strdup (soup_uri->path);
					break;
				}

				slash[0] = '\0';
				slash = strrchr (soup_uri->path, '/');
			}

			soup_uri_free (soup_uri);
		}

		xmlXPathFreeObject (xp_obj);
	}

	if (addressbook_home_set == NULL || *addressbook_home_set == '\0') {
		g_free (addressbook_home_set);
		xmlXPathFreeContext (xp_ctx);
		xmlFreeDoc (doc);
		return TRUE;
	}

 get_collection_details:

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	if (!e_webdav_discover_get_addressbook_collection_details (
		session, message, addressbook_home_set, source,
		out_certificate_pem, out_certificate_errors, out_discovered_sources,
		cancellable, error)) {
		g_free (addressbook_home_set);
		return FALSE;
	}

	g_free (addressbook_home_set);

	return TRUE;

 retry_propfind:

	xmlXPathFreeContext (xp_ctx);
	xmlFreeDoc (doc);

	soup_uri = soup_uri_copy (soup_message_get_uri (message));
	soup_uri_set_path (soup_uri, addressbook_home_set);

	/* Note that we omit "D:resourcetype", "D:current-user-principal"
	 * and "D:principal-URL" in order to short-circuit the recursion. */
	message = e_webdav_discover_new_propfind (
		session, soup_uri, DEPTH_1,
		NS_CARDDAV, XC ("addressbook-home-set"),
		NULL);

	e_soup_ssl_trust_connect (message, source);

	/* This takes ownership of the message. */
	soup_session_send_message (session, message);

	soup_uri_free (soup_uri);

	g_free (addressbook_home_set);

	success = e_webdav_discover_process_addressbook_home_set (session, message, source,
		out_certificate_pem, out_certificate_errors, out_discovered_sources,
		cancellable, error);

	g_object_unref (message);

	return success;
}

static void
e_webdav_discover_source_free (gpointer ptr)
{
	EWebDAVDiscoveredSource *discovered_source = ptr;

	if (discovered_source) {
		g_free (discovered_source->href);
		g_free (discovered_source->display_name);
		g_free (discovered_source->description);
		g_free (discovered_source->color);
		g_free (discovered_source);
	}
}

/**
 * e_webdav_discover_free_discovered_sources:
 * @discovered_sources: (element-type EWebDAVDiscoveredSource): A #GSList of discovered sources
 *
 * Frees a @GSList of discovered sources returned from
 * e_webdav_discover_sources_finish() or e_webdav_discover_sources_sync().
 *
 * Since: 3.18
 **/
void
e_webdav_discover_free_discovered_sources (GSList *discovered_sources)
{
	g_slist_free_full (discovered_sources, e_webdav_discover_source_free);
}

static void
e_webdav_discover_sources_thread (GTask *task,
				  gpointer source_object,
				  gpointer task_data,
				  GCancellable *cancellable)
{
	EWebDAVDiscoverContext *context = task_data;
	gboolean success;
	GError *local_error = NULL;

	g_return_if_fail (context != NULL);
	g_return_if_fail (E_IS_SOURCE (source_object));

	success = e_webdav_discover_sources_sync (E_SOURCE (source_object),
		context->url_use_path, context->only_supports, context->credentials,
		&context->out_certificate_pem, &context->out_certificate_errors,
		&context->out_discovered_sources, &context->out_calendar_user_addresses,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

/**
 * e_webdav_discover_sources:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (allow-none): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (allow-none): credentials to use for authentication to the server
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously runs discovery of the WebDAV sources (CalDAV and CardDAV), eventually
 * limited by the @only_supports filter, which can be %E_WEBDAV_DISCOVER_SUPPORTS_NONE
 * to search all types. Note that the list of returned calendars can be more general,
 * thus check for its actual support type for further filtering of the results.
 * The @url_use_path can be used to override actual server path, or even complete URL,
 * for the given @source.
 *
 * When the operation is finished, @callback will be called. You can then
 * call e_webdav_discover_sources_finish() to get the result of the operation.
 *
 * Since: 3.18
 **/
void
e_webdav_discover_sources (ESource *source,
			   const gchar *url_use_path,
			   guint32 only_supports,
			   const ENamedParameters *credentials,
			   GCancellable *cancellable,
			   GAsyncReadyCallback callback,
			   gpointer user_data)
{
	EWebDAVDiscoverContext *context;
	GTask *task;

	g_return_if_fail (E_IS_SOURCE (source));

	context = e_webdav_discover_context_new (source, url_use_path, only_supports, credentials);

	task = g_task_new (source, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_webdav_discover_sources);
	g_task_set_task_data (task, context, e_webdav_discover_context_free);

	g_task_run_in_thread (task, e_webdav_discover_sources_thread);

	g_object_unref (task);
}

/**
 * e_webdav_discover_sources_finish:
 * @source: an #ESource on which the operation was started
 * @result: a #GAsyncResult
 * @out_certificate_pem: (out) (allow-none): optional return location
 *   for a server SSL certificate in PEM format, when the operation failed
 *   with an SSL error
 * @out_certificate_errors: (out) (allow-none): optional #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with SSL error
 * @out_discovered_sources: (out) (element-type EWebDAVDiscoveredSource): a #GSList
 *   of all discovered sources
 * @out_calendar_user_addresses: (out) (allow-none) (element-type utf8): a #GSList of
 *   all discovered mail addresses for calendar sources
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_webdav_discover_sources(). If an
 * error occurred, the function will set @error and return %FALSE. The function
 * can return success and no discovered sources, the same as it can return failure,
 * but still set some output arguments, like the certificate related output
 * arguments with SOUP_STATUS_SSL_FAILED error.
 *
 * The return value of @out_certificate_pem should be freed with g_free()
 * when no longer needed.
 *
 * The return value of @out_discovered_sources should be freed
 * with e_webdav_discover_free_discovered_sources() when no longer needed.
 *
 * The return value of @out_calendar_user_addresses should be freed
 * with g_slist_free_full (calendar_user_addresses, g_free); when
 * no longer needed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.18
 **/
gboolean
e_webdav_discover_sources_finish (ESource *source,
				  GAsyncResult *result,
				  gchar **out_certificate_pem,
				  GTlsCertificateFlags *out_certificate_errors,
				  GSList **out_discovered_sources,
				  GSList **out_calendar_user_addresses,
				  GError **error)
{
	EWebDAVDiscoverContext *context;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, source), FALSE);

	g_return_val_if_fail (
		g_async_result_is_tagged (
		result, e_webdav_discover_sources), FALSE);

	context = g_task_get_task_data (G_TASK (result));
	g_return_val_if_fail (context != NULL, FALSE);

	if (out_certificate_pem) {
		*out_certificate_pem = context->out_certificate_pem;
		context->out_certificate_pem = NULL;
	}

	if (out_certificate_errors)
		*out_certificate_errors = context->out_certificate_errors;

	if (out_discovered_sources) {
		*out_discovered_sources = context->out_discovered_sources;
		context->out_discovered_sources = NULL;
	}

	if (out_calendar_user_addresses) {
		*out_calendar_user_addresses = context->out_calendar_user_addresses;
		context->out_calendar_user_addresses = NULL;
	}

	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
e_webdav_discover_cancelled_cb (GCancellable *cancellable,
				SoupSession *session)
{
	soup_session_abort (session);
}

/**
 * e_webdav_discover_sources_sync:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (allow-none): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (allow-none): credentials to use for authentication to the server
 * @out_certificate_pem: (out) (allow-none): optional return location
 *   for a server SSL certificate in PEM format, when the operation failed
 *   with an SSL error
 * @out_certificate_errors: (out) (allow-none): optional #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with SSL error
 * @out_discovered_sources: (out) (element-type EWebDAVDiscoveredSource): a #GSList
 *   of all discovered sources
 * @out_calendar_user_addresses: (out) (allow-none) (element-type utf8): a #GSList of
 *   all discovered mail addresses for calendar sources
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: (allow-none): return location for a #GError, or %NULL
 *
 * Synchronously runs discovery of the WebDAV sources (CalDAV and CardDAV), eventually
 * limited by the @only_supports filter, which can be %E_WEBDAV_DISCOVER_SUPPORTS_NONE
 * to search all types. Note that the list of returned calendars can be more general,
 * thus check for its actual support type for further filtering of the results.
 * The @url_use_path can be used to override actual server path, or even complete URL,
 * for the given @source.
 *
 * If an error occurred, the function will set @error and return %FALSE. The function
 * can return success and no discovered sources, the same as it can return failure,
 * but still set some output arguments, like the certificate related output
 * arguments with SOUP_STATUS_SSL_FAILED error.
 *
 * The return value of @out_certificate_pem should be freed with g_free()
 * when no longer needed.
 *
 * The return value of @out_discovered_sources should be freed
 * with e_webdav_discover_free_discovered_sources() when no longer needed.
 *
 * The return value of @out_calendar_user_addresses should be freed
 * with g_slist_free_full (calendar_user_addresses, g_free); when
 * no longer needed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.18
 **/
gboolean
e_webdav_discover_sources_sync (ESource *source,
				const gchar *url_use_path,
				guint32 only_supports,
				const ENamedParameters *credentials,
				gchar **out_certificate_pem,
				GTlsCertificateFlags *out_certificate_errors,
				GSList **out_discovered_sources,
				GSList **out_calendar_user_addresses,
				GCancellable *cancellable,
				GError **error)
{
	ESourceWebdav *webdav_extension;
	AuthenticateData auth_data;
	SoupSession *session;
	SoupMessage *message;
	SoupURI *soup_uri;
	gulong cancelled_handler_id = 0, authenticate_handler_id;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (url_use_path && (g_ascii_strncasecmp (url_use_path, "http://", 7) == 0 ||
	    g_ascii_strncasecmp (url_use_path, "https://", 8) == 0)) {
		soup_uri = soup_uri_new (url_use_path);
		url_use_path = NULL;
	} else {
		g_return_val_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND), FALSE);

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		soup_uri = e_source_webdav_dup_soup_uri (webdav_extension);
	}

	g_return_val_if_fail (soup_uri != NULL, FALSE);

	if (url_use_path) {
		GString *new_path;

		/* Absolute path overrides whole path, while relative path is only appended. */
		if (*url_use_path == '/') {
			new_path = g_string_new (url_use_path);
		} else {
			const gchar *current_path;

			current_path = soup_uri_get_path (soup_uri);
			new_path = g_string_new (current_path ? current_path : "");
			if (!new_path->len || new_path->str[new_path->len - 1] != '/')
				g_string_append_c (new_path, '/');
			g_string_append (new_path, url_use_path);
		}

		if (!new_path->len || new_path->str[new_path->len - 1] != '/')
			g_string_append_c (new_path, '/');

		soup_uri_set_path (soup_uri, new_path->str);

		g_string_free (new_path, TRUE);
	}

	session = soup_session_new ();
	g_object_set (
		session,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	message = e_webdav_discover_new_propfind (
		session, soup_uri, DEPTH_0,
		NS_WEBDAV, XC ("resourcetype"),
		NS_WEBDAV, XC ("current-user-principal"),
		NS_WEBDAV, XC ("principal-URL"),
		NS_CALDAV, XC ("calendar-home-set"),
		NS_CALDAV, XC ("calendar-user-address-set"),
		NS_CARDDAV, XC ("addressbook-home-set"),
		NS_CARDDAV, XC ("principal-address"),
		NULL);

	if (!message) {
		soup_uri_free (soup_uri);
		g_object_unref (session);
		return FALSE;
	}

	if (g_getenv ("WEBDAV_DEBUG") != NULL) {
		SoupLogger *logger;

		logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, 100 * 1024 * 1024);
		soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
		g_object_unref (logger);
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		SoupSessionFeature *feature;
		ESourceAuthentication *auth_extension;
		gchar *auth_method;

		feature = soup_session_get_feature (session, SOUP_TYPE_AUTH_MANAGER);

		success = TRUE;

		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_method = e_source_authentication_dup_method (auth_extension);

		if (g_strcmp0 (auth_method, "OAuth2") == 0 || g_strcmp0 (auth_method, "Google") == 0) {
			SoupAuth *soup_auth;

			soup_auth = g_object_new (E_TYPE_SOUP_AUTH_BEARER, SOUP_AUTH_HOST, soup_uri->host, NULL);

			success = e_webdav_discover_setup_bearer_auth (source, credentials,
				E_SOUP_AUTH_BEARER (soup_auth), cancellable, error);

			if (success) {
				soup_session_feature_add_feature (feature, E_TYPE_SOUP_AUTH_BEARER);
				soup_auth_manager_use_auth (
					SOUP_AUTH_MANAGER (feature),
					soup_uri, soup_auth);
			}

			g_object_unref (soup_auth);
		}

		g_free (auth_method);

		if (!success) {
			soup_uri_free (soup_uri);
			g_object_unref (message);
			g_object_unref (session);
			return FALSE;
		}
	}

	auth_data.source = source;
	auth_data.credentials = credentials;

	authenticate_handler_id = g_signal_connect (session, "authenticate",
		G_CALLBACK (e_webdav_discover_authenticate_cb), &auth_data);

	if (cancellable)
		cancelled_handler_id = g_cancellable_connect (cancellable, G_CALLBACK (e_webdav_discover_cancelled_cb), session, NULL);

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		GSList *calendars = NULL, *addressbooks = NULL;
		GError *local_error = NULL;

		e_soup_ssl_trust_connect (message, source);
		soup_session_send_message (session, message);

		success = TRUE;

		if (only_supports == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
		   (only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_EVENTS | E_WEBDAV_DISCOVER_SUPPORTS_MEMOS | E_WEBDAV_DISCOVER_SUPPORTS_TASKS)) != 0) {
			success = e_webdav_discover_process_calendar_home_set (session, message, source, out_certificate_pem,
				out_certificate_errors, &calendars, out_calendar_user_addresses, cancellable, &local_error);

			if (!calendars && !g_cancellable_is_cancelled (cancellable) && (!soup_uri_get_path (soup_uri) ||
			    !strstr (soup_uri_get_path (soup_uri), "/.well-known/"))) {
				SoupMessage *well_known_message;
				gchar *saved_path;

				saved_path = g_strdup (soup_uri_get_path (soup_uri));

				soup_uri_set_path (soup_uri, "/.well-known/caldav");

				well_known_message = e_webdav_discover_new_propfind (
					session, soup_uri, DEPTH_0,
					NS_WEBDAV, XC ("resourcetype"),
					NS_WEBDAV, XC ("current-user-principal"),
					NS_WEBDAV, XC ("principal-URL"),
					NS_CALDAV, XC ("calendar-home-set"),
					NS_CALDAV, XC ("calendar-user-address-set"),
					NULL);

				soup_uri_set_path (soup_uri, saved_path);
				g_free (saved_path);

				if (well_known_message) {
					e_soup_ssl_trust_connect (well_known_message, source);
					soup_session_send_message (session, well_known_message);

					/* Ignore errors here */
					e_webdav_discover_process_calendar_home_set (session, well_known_message, source, out_certificate_pem,
						out_certificate_errors, &calendars, out_calendar_user_addresses, cancellable, NULL);

					g_clear_object (&well_known_message);
				}
			}
		}

		if (success && (only_supports == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
		    (only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS)) != 0)) {
			success = e_webdav_discover_process_addressbook_home_set (session, message, source, out_certificate_pem,
				out_certificate_errors, &addressbooks, cancellable, local_error ? NULL : &local_error);

			if (!addressbooks && !g_cancellable_is_cancelled (cancellable) && (!soup_uri_get_path (soup_uri) ||
			    !strstr (soup_uri_get_path (soup_uri), "/.well-known/"))) {
				g_clear_object (&message);

				soup_uri_set_path (soup_uri, "/.well-known/carddav");

				message = e_webdav_discover_new_propfind (
					session, soup_uri, DEPTH_0,
					NS_WEBDAV, XC ("resourcetype"),
					NS_WEBDAV, XC ("current-user-principal"),
					NS_WEBDAV, XC ("principal-URL"),
					NS_CARDDAV, XC ("addressbook-home-set"),
					NS_CARDDAV, XC ("principal-address"),
					NULL);

				if (message) {
					e_soup_ssl_trust_connect (message, source);
					soup_session_send_message (session, message);

					/* Ignore errors here */
					e_webdav_discover_process_addressbook_home_set (session, message, source, out_certificate_pem,
						out_certificate_errors, &addressbooks, cancellable, NULL);
				}
			}
		}

		if (calendars || addressbooks) {
			success = TRUE;
			g_clear_error (&local_error);
		} else if (local_error) {
			g_propagate_error (error, local_error);
		}

		if (out_discovered_sources) {
			if (calendars)
				*out_discovered_sources = g_slist_concat (*out_discovered_sources, calendars);
			if (addressbooks)
				*out_discovered_sources = g_slist_concat (*out_discovered_sources, addressbooks);
		} else {
			e_webdav_discover_free_discovered_sources (calendars);
			e_webdav_discover_free_discovered_sources (addressbooks);
		}

		if (out_calendar_user_addresses && *out_calendar_user_addresses)
			*out_calendar_user_addresses = g_slist_reverse (*out_calendar_user_addresses);

		if (out_discovered_sources && *out_discovered_sources)
			*out_discovered_sources = g_slist_reverse (*out_discovered_sources);
	} else {
		success = FALSE;
	}

	if (cancellable && cancelled_handler_id)
		g_cancellable_disconnect (cancellable, cancelled_handler_id);

	if (authenticate_handler_id)
		g_signal_handler_disconnect (session, authenticate_handler_id);

	soup_uri_free (soup_uri);
	g_clear_object (&message);
	g_object_unref (session);

	return success;
}

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
 * SECTION: e-webdav-session
 * @include: libedataserver/libedataserver.h
 * @short_description: A WebDAV, CalDAV and CardDAV session
 *
 * The #EWebDAVSession is a class to work with WebDAV (RFC 4918),
 * CalDAV (RFC 4791) or CardDAV (RFC 6352) servers, providing API
 * for common requests/responses, on top of an #ESoupSession.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel/camel.h"

#include "e-source-webdav.h"
#include "e-xml-utils.h"

#include "e-webdav-session.h"

struct _EWebDAVSessionPrivate {
	gboolean dummy;
};

G_DEFINE_TYPE (EWebDAVSession, e_webdav_session, E_TYPE_SOUP_SESSION)

G_DEFINE_BOXED_TYPE (EWebDAVResource, e_webdav_resource, e_webdav_resource_copy, e_webdav_resource_free)

/**
 * e_webdav_resource_new:
 * @kind: an #EWebDAVResourceKind of the resource
 * @supports: bit-or of #EWebDAVResourceSupports values
 * @href: href of the resource
 * @etag: (nullable): optional ETag of the resource, or %NULL
 * @display_name: (nullable): optional display name of the resource, or %NULL
 * @description: (nullable): optional description of the resource, or %NULL
 * @color: (nullable): optional color of the resource, or %NULL
 *
 * Some values of the resource are not always valid, depending on the @kind,
 * but also whether server stores such values and whether it had been asked
 * for them to be fetched.
 *
 * The @etag for %E_WEBDAV_RESOURCE_KIND_COLLECTION can be a change tag instead.
 *
 * Returns: (transfer full): A newly created #EWebDAVResource, prefilled with
 *    given values. Free it with e_webdav_resource_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVResource *
e_webdav_resource_new (EWebDAVResourceKind kind,
		       guint32 supports,
		       const gchar *href,
		       const gchar *etag,
		       const gchar *display_name,
		       const gchar *content_type,
		       gsize content_length,
		       glong creation_date,
		       glong last_modified,
		       const gchar *description,
		       const gchar *color)
{
	EWebDAVResource *resource;

	resource = g_new0 (EWebDAVResource, 1);
	resource->kind = kind;
	resource->supports = supports;
	resource->href = g_strdup (href);
	resource->etag = g_strdup (etag);
	resource->display_name = g_strdup (display_name);
	resource->content_type = g_strdup (content_type);
	resource->content_length = content_length;
	resource->creation_date = creation_date;
	resource->last_modified = last_modified;
	resource->description = g_strdup (description);
	resource->color = g_strdup (color);

	return resource;
}

/**
 * e_webdav_resource_copy:
 * @resource: (nullable): an #EWebDAVResource to make a copy of
 *
 * Returns: (transfer full): A new #EWebDAVResource prefilled with
 *    the same values as @resource, or %NULL, when @resource is %NULL.
 *    Free it with e_webdav_resource_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVResource *
e_webdav_resource_copy (const EWebDAVResource *resource)
{
	if (!resource)
		return NULL;

	return e_webdav_resource_new (resource->kind,
		resource->supports,
		resource->href,
		resource->etag,
		resource->display_name,
		resource->content_type,
		resource->content_length,
		resource->creation_date,
		resource->last_modified,
		resource->description,
		resource->color);
}

/**
 * e_webdav_resource_free:
 * @ptr: (nullable): an #EWebDAVResource
 *
 * Frees an #EWebDAVResource previously created with e_webdav_resource_new()
 * or e_webdav_resource_copy(). The function does nothign if @ptr is %NULL.
 *
 * Since: 3.26
 **/
void
e_webdav_resource_free (gpointer ptr)
{
	EWebDAVResource *resource = ptr;

	if (resource) {
		g_free (resource->href);
		g_free (resource->etag);
		g_free (resource->display_name);
		g_free (resource->content_type);
		g_free (resource->description);
		g_free (resource->color);
		g_free (resource);
	}
}

static void
e_webdav_session_class_init (EWebDAVSessionClass *klass)
{
	g_type_class_add_private (klass, sizeof (EWebDAVSessionPrivate));
}

static void
e_webdav_session_init (EWebDAVSession *webdav)
{
	webdav->priv = G_TYPE_INSTANCE_GET_PRIVATE (webdav, E_TYPE_WEBDAV_SESSION, EWebDAVSessionPrivate);
}

/**
 * e_webdav_session_new:
 * @source: an #ESource
 *
 * Creates a new #EWebDAVSession associated with given @source. It's
 * a user's error to try to create the #EWebDAVSession for a source
 * which doesn't have #ESourceWebdav extension properly defined.
 *
 * Returns: (transfer full): a new #EWebDAVSession; free it with g_object_unref(),
 *    when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVSession *
e_webdav_session_new (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND), NULL);

	return g_object_new (E_TYPE_WEBDAV_SESSION,
		"source", source,
		NULL);
}

static SoupRequestHTTP *
e_webdav_session_new_request (EWebDAVSession *webdav,
			      const gchar *method,
			      const gchar *uri,
			      GError **error)
{
	ESoupSession *session;
	SoupRequestHTTP *request;
	SoupURI *soup_uri;
	ESource *source;
	ESourceWebdav *webdav_extension;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), NULL);

	session = E_SOUP_SESSION (webdav);
	if (uri && *uri)
		return e_soup_session_new_request (session, method, uri, error);

	source = e_soup_session_get_source (session);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	soup_uri = e_source_webdav_dup_soup_uri (webdav_extension);

	g_return_val_if_fail (soup_uri != NULL, NULL);

	request = e_soup_session_new_request_uri (session, method, soup_uri, error);

	soup_uri_free (soup_uri);

	return request;
}

static GHashTable *
e_webdav_session_comma_header_to_hashtable (SoupMessageHeaders *headers,
					    const gchar *header_name)
{
	GHashTable *soup_params, *result;
	GHashTableIter iter;
	const gchar *value;
	gpointer key;

	g_return_val_if_fail (header_name != NULL, NULL);

	if (!headers)
		return NULL;

	value = soup_message_headers_get_list (headers, header_name);
	if (!value)
		return NULL;

	soup_params = soup_header_parse_param_list (value);
	if (!soup_params)
		return NULL;

	result = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);

	g_hash_table_iter_init (&iter, soup_params);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		value = key;

		if (value && *value)
			g_hash_table_insert (result, g_strdup (value), GINT_TO_POINTER (1));
	}

	soup_header_free_param_list (soup_params);

	return result;
}

/**
 * e_webdav_session_options_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_capabilities: (out) (transfer full): return location for DAV capabilities
 * @out_allows: (out) (transfer full): return location for allowed operations
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues OPTIONS request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource.
 *
 * The @out_capabilities contains a set of returned capabilities. Some known are
 * defined as E_WEBDAV_CAPABILITY_CLASS_1, and so on. The 'value' of the #GHashTable
 * doesn't have any particular meaning and the strings are compared case insensitively.
 * Free the hash table with g_hash_table_destroy(), when no longer needed. The returned
 * value can be %NULL on success, it's when the server doesn't provide the information.
 *
 * The @out_allows contains a set of allowed methods returned by the server. Some known
 * are defined as E_WEBDAV_ALLOW_OPTIONS, and so on. The 'value' of the #GHashTable
 * doesn't have any particular meaning and the strings are compared case insensitively.
 * Free the hash table with g_hash_table_destroy(), when no longer needed. The returned
 * value can be %NULL on success, it's when the server doesn't provide the information.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_options_sync (EWebDAVSession *webdav,
			       const gchar *uri,
			       GHashTable **out_capabilities,
			       GHashTable **out_allows,
			       GCancellable *cancellable,
			       GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_capabilities != NULL, FALSE);
	g_return_val_if_fail (out_allows != NULL, FALSE);

	*out_capabilities = NULL;
	*out_allows = NULL;

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_OPTIONS, uri, error);
	if (!request)
		return FALSE;

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	if (!bytes) {
		g_object_unref (request);
		return FALSE;
	}

	message = soup_request_http_get_message (request);

	g_byte_array_free (bytes, TRUE);
	g_object_unref (request);

	g_return_val_if_fail (message != NULL, FALSE);

	*out_capabilities = e_webdav_session_comma_header_to_hashtable (message->response_headers, "DAV");
	*out_allows = e_webdav_session_comma_header_to_hashtable (message->response_headers, "Allow");

	g_object_unref (message);

	return TRUE;
}

/**
 * e_webdav_session_propfind_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_0, %E_WEBDAV_DEPTH_1 or %E_WEBDAV_DEPTH_INFINITY
 * @xml: (nullable): the request itself, as an #EXmlDocument, the root element should be DAV:propfind, or %NULL
 * @func: an #EWebDAVPropfindFunc function to call for each DAV:propstat in the multistatus response
 * @func_user_data: user data passed to @func
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues PROPFIND request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource. On success, calls @func for each returned
 * DAV:propstat. The provided XPath context has registered %E_WEBDAV_NS_DAV namespace
 * with prefix "D". It doesn't have any other namespace registered.
 *
 * The @func is called always at least once, with %NULL xpath_prop_prefix, which
 * is meant to let the caller setup the xpath_ctx, like to register its own namespaces
 * to it with e_xml_xpath_context_register_namespaces(). All other invocations of @func
 * will have xpath_prop_prefix non-%NULL.
 *
 * The @xml can be %NULL, in which case the server should behave like DAV:allprop request.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_propfind_sync (EWebDAVSession *webdav,
				const gchar *uri,
				const gchar *depth,
				const EXmlDocument *xml,
				EWebDAVPropfindFunc func,
				gpointer func_user_data,
				GCancellable *cancellable,
				GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);
	if (xml)
		g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_PROPFIND, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	soup_message_headers_append (message->request_headers, "Depth", depth);

	if (xml) {
		gchar *content;
		gsize content_length;

		content = e_xml_document_get_content (xml, &content_length);
		if (!content) {
			g_object_unref (message);
			g_object_unref (request);

			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

			return FALSE;
		}

		soup_message_set_request (message, E_WEBDAV_CONTENT_TYPE_XML,
			SOUP_MEMORY_TAKE, content, content_length);
	}

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	g_object_unref (request);

	success = bytes != NULL;

	if (success && message->status_code != SOUP_STATUS_MULTI_STATUS) {
		success = FALSE;

		g_set_error (error, SOUP_HTTP_ERROR, message->status_code,
			_("Expected multistatus response, but %d returned (%s)"), message->status_code,
			message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
			(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error")));
	}

	if (success) {
		const gchar *content_type;

		content_type = soup_message_headers_get_content_type (message->response_headers, NULL);
		success = content_type &&
			(g_ascii_strcasecmp (content_type, "application/xml") == 0 ||
			 g_ascii_strcasecmp (content_type, "text/xml") == 0);

		if (!success) {
			if (!content_type) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but none returned"));
			} else {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but %s returned"), content_type);
			}
		}
	}

	if (success) {
		xmlDocPtr doc = e_xml_parse_data ((const gchar *) bytes->data, bytes->len);

		g_byte_array_free (bytes, TRUE);
		bytes = NULL;

		if (doc) {
			xmlXPathContextPtr xpath_ctx;
			SoupURI *request_uri;

			request_uri = soup_message_get_uri (message);

			xpath_ctx = e_xml_new_xpath_context_with_namespaces (doc,
				"D", E_WEBDAV_NS_DAV,
				NULL);

			if (xpath_ctx &&
			    func (webdav, xpath_ctx, NULL, request_uri, SOUP_STATUS_NONE, func_user_data)) {
				xmlXPathObjectPtr xpath_obj_response;

				xpath_obj_response = e_xml_xpath_eval (xpath_ctx, "/D:multistatus/D:response");

				if (xpath_obj_response != NULL) {
					gboolean do_stop = FALSE;
					gint response_index, response_length;

					response_length = xmlXPathNodeSetGetLength (xpath_obj_response->nodesetval);

					for (response_index = 0; response_index < response_length && !do_stop; response_index++) {
						xmlXPathObjectPtr xpath_obj_propstat;

						xpath_obj_propstat = e_xml_xpath_eval (xpath_ctx,
							"/D:multistatus/D:response[%d]/D:propstat",
							response_index + 1);

						if (xpath_obj_propstat != NULL) {
							gint propstat_index, propstat_length;

							propstat_length = xmlXPathNodeSetGetLength (xpath_obj_propstat->nodesetval);

							for (propstat_index = 0; propstat_index < propstat_length && !do_stop; propstat_index++) {
								gchar *status, *propstat_prefix;
								guint status_code;

								propstat_prefix = g_strdup_printf ("/D:multistatus/D:response[%d]/D:propstat[%d]/D:prop",
									response_index + 1, propstat_index + 1);

								status = e_xml_xpath_eval_as_string (xpath_ctx, "%s/../D:status", propstat_prefix);
								if (!status || !soup_headers_parse_status_line (status, NULL, &status_code, NULL))
									status_code = 0;
								g_free (status);

								do_stop = !func (webdav, xpath_ctx, propstat_prefix, request_uri, status_code, func_user_data);

								g_free (propstat_prefix);
							}

							xmlXPathFreeObject (xpath_obj_propstat);
						}
					}

					xmlXPathFreeObject (xpath_obj_response);
				}
			}

			if (xpath_ctx)
				xmlXPathFreeContext (xpath_ctx);
			xmlFreeDoc (doc);
		} else {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("Failed to parse response as XML"));

			success = FALSE;
		}
	}

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/* This assumes ownership of 'text' */
static gchar *
e_webdav_session_maybe_dequote (gchar *text)
{
	gchar *dequoted;
	gint len;

	if (!text || *text != '\"')
		return text;

	len = strlen (text);

	if (len < 2 || text[len - 1] != '\"')
		return text;

	dequoted = g_strndup (text + 1, len - 2);
	g_free (text);

	return dequoted;
}

static gboolean
e_webdav_session_getctag_cb (EWebDAVSession *webdav,
			     xmlXPathContextPtr xpath_ctx,
			     const gchar *xpath_prop_prefix,
			     const SoupURI *request_uri,
			     guint status_code,
			     gpointer user_data)
{
	if (!xpath_prop_prefix) {
		e_xml_xpath_context_register_namespaces (xpath_ctx,
			"CS", E_WEBDAV_NS_CALENDARSERVER,
			NULL);

		return TRUE;
	}

	if (status_code == SOUP_STATUS_OK) {
		gchar **out_ctag = user_data;
		gchar *ctag;

		g_return_val_if_fail (out_ctag != NULL, FALSE);

		ctag = e_xml_xpath_eval_as_string (xpath_ctx, "%s/CS:getctag", xpath_prop_prefix);

		if (ctag && *ctag) {
			*out_ctag = e_webdav_session_maybe_dequote (ctag);
		} else {
			g_free (ctag);
		}
	}

	return FALSE;
}

/**
 * e_webdav_session_getctag_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_ctag: (out) (transfer full): return location for the ctag
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues a getctag property request for a collection identified by @uri, or
 * by the #ESource resource reference. The ctag is a collection tag, which
 * changes whenever the collection changes (similar to etag). The getctag is
 * an extension, thus the function can fail when the server doesn't support it.
 *
 * Free the returned @out_ctag with g_free(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_getctag_sync (EWebDAVSession *webdav,
			       const gchar *uri,
			       gchar **out_ctag,
			       GCancellable *cancellable,
			       GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_ctag != NULL, FALSE);

	*out_ctag = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "CS", E_WEBDAV_NS_CALENDARSERVER, NULL);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CALENDARSERVER, "getctag");
	e_xml_document_end_element (xml); /* getctag */
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_0, xml,
		e_webdav_session_getctag_cb, out_ctag, cancellable, error);

	g_object_unref (xml);

	return success && *out_ctag != NULL;
}

static EWebDAVResourceKind
e_webdav_session_extract_kind (xmlXPathContextPtr xpath_ctx,
			       const gchar *xpath_prop_prefix)
{
	g_return_val_if_fail (xpath_ctx != NULL, E_WEBDAV_RESOURCE_KIND_UNKNOWN);
	g_return_val_if_fail (xpath_prop_prefix != NULL, E_WEBDAV_RESOURCE_KIND_UNKNOWN);

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/D:resourcetype/A:addressbook", xpath_prop_prefix))
		return E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK;

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/D:resourcetype/C:calendar", xpath_prop_prefix))
		return E_WEBDAV_RESOURCE_KIND_CALENDAR;

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/D:resourcetype/D:principal", xpath_prop_prefix))
		return E_WEBDAV_RESOURCE_KIND_PRINCIPAL;

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/D:resourcetype/D:collection", xpath_prop_prefix))
		return E_WEBDAV_RESOURCE_KIND_COLLECTION;

	return E_WEBDAV_RESOURCE_KIND_RESOURCE;
}

static guint32
e_webdav_session_extract_supports (xmlXPathContextPtr xpath_ctx,
				   const gchar *xpath_prop_prefix)
{
	guint32 supports = E_WEBDAV_RESOURCE_SUPPORTS_NONE;

	g_return_val_if_fail (xpath_ctx != NULL, E_WEBDAV_RESOURCE_SUPPORTS_NONE);
	g_return_val_if_fail (xpath_prop_prefix != NULL, E_WEBDAV_RESOURCE_SUPPORTS_NONE);

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/D:resourcetype/A:addressbook", xpath_prop_prefix))
		supports = supports | E_WEBDAV_RESOURCE_SUPPORTS_CONTACTS;

	if (e_xml_xpath_eval_exists (xpath_ctx, "%s/C:supported-calendar-component-set", xpath_prop_prefix)) {
		xmlXPathObjectPtr xpath_obj;

		xpath_obj = e_xml_xpath_eval (xpath_ctx, "%s/C:supported-calendar-component-set/C:comp", xpath_prop_prefix);
		if (xpath_obj) {
			gint ii, length;

			length = xmlXPathNodeSetGetLength (xpath_obj->nodesetval);

			for (ii = 0; ii < length; ii++) {
				gchar *name;

				name = e_xml_xpath_eval_as_string (xpath_ctx, "%s/C:supported-calendar-component-set/C:comp[%d]/@name",
					xpath_prop_prefix, ii + 1);

				if (!name)
					continue;

				if (g_ascii_strcasecmp (name, "VEVENT") == 0)
					supports |= E_WEBDAV_RESOURCE_SUPPORTS_EVENTS;
				else if (g_ascii_strcasecmp (name, "VJOURNAL") == 0)
					supports |= E_WEBDAV_RESOURCE_SUPPORTS_MEMOS;
				else if (g_ascii_strcasecmp (name, "VTODO") == 0)
					supports |= E_WEBDAV_RESOURCE_SUPPORTS_TASKS;

				g_free (name);
			}

			xmlXPathFreeObject (xpath_obj);
		} else {
			/* If the property is not present, assume all component
			 * types are supported.  (RFC 4791, Section 5.2.3) */
			supports = supports |
				E_WEBDAV_RESOURCE_SUPPORTS_EVENTS |
				E_WEBDAV_RESOURCE_SUPPORTS_MEMOS |
				E_WEBDAV_RESOURCE_SUPPORTS_TASKS;
		}
	}

	return supports;
}

static gchar *
e_webdav_session_extract_nonempty (xmlXPathContextPtr xpath_ctx,
				   const gchar *xpath_prop_prefix,
				   const gchar *prop,
				   const gchar *alternative_prop)
{
	gchar *value;

	g_return_val_if_fail (xpath_ctx != NULL, NULL);
	g_return_val_if_fail (xpath_prop_prefix != NULL, NULL);
	g_return_val_if_fail (prop != NULL, NULL);

	value = e_xml_xpath_eval_as_string (xpath_ctx, "%s/%s", xpath_prop_prefix, prop);
	if (!value && alternative_prop)
		value = e_xml_xpath_eval_as_string (xpath_ctx, "%s/%s", xpath_prop_prefix, alternative_prop);
	if (!value)
		return NULL;

	if (!*value) {
		g_free (value);
		return NULL;
	}

	return e_webdav_session_maybe_dequote (value);
}

static gsize
e_webdav_session_extract_content_length (xmlXPathContextPtr xpath_ctx,
					 const gchar *xpath_prop_prefix)
{
	gchar *value;
	gsize length;

	g_return_val_if_fail (xpath_ctx != NULL, -1);
	g_return_val_if_fail (xpath_prop_prefix != NULL, -1);

	value = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "D:getcontentlength", NULL);
	if (!value)
		return -1;

	length = g_ascii_strtoll (value, NULL, 10);

	g_free (value);

	return length;
}

static glong
e_webdav_session_extract_datetime (xmlXPathContextPtr xpath_ctx,
				   const gchar *xpath_prop_prefix,
				   const gchar *prop,
				   gboolean is_iso_property)
{
	gchar *value;
	GTimeVal tv;

	g_return_val_if_fail (xpath_ctx != NULL, -1);
	g_return_val_if_fail (xpath_prop_prefix != NULL, -1);

	value = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, prop, NULL);
	if (!value)
		return -1;

	if (is_iso_property && !g_time_val_from_iso8601 (value, &tv)) {
		tv.tv_sec = -1;
	} else if (!is_iso_property) {
		tv.tv_sec = camel_header_decode_date (value, NULL);
	}

	g_free (value);

	return tv.tv_sec;
}

static gboolean
e_webdav_session_list_cb (EWebDAVSession *webdav,
			  xmlXPathContextPtr xpath_ctx,
			  const gchar *xpath_prop_prefix,
			  const SoupURI *request_uri,
			  guint status_code,
			  gpointer user_data)
{
	GSList **out_resources = user_data;

	g_return_val_if_fail (out_resources != NULL, FALSE);
	g_return_val_if_fail (request_uri != NULL, FALSE);

	if (!xpath_prop_prefix) {
		e_xml_xpath_context_register_namespaces (xpath_ctx,
			"CS", E_WEBDAV_NS_CALENDARSERVER,
			"C", E_WEBDAV_NS_CALDAV,
			"A", E_WEBDAV_NS_CARDDAV,
			"IC", E_WEBDAV_NS_ICAL,
			NULL);

		return TRUE;
	}

	if (status_code == SOUP_STATUS_OK) {
		EWebDAVResource *resource;
		EWebDAVResourceKind kind;
		guint32 supports;
		gchar *href;
		gchar *etag;
		gchar *display_name;
		gchar *content_type;
		gsize content_length;
		glong creation_date;
		glong last_modified;
		gchar *description;
		gchar *color;

		kind = e_webdav_session_extract_kind (xpath_ctx, xpath_prop_prefix);
		if (kind == E_WEBDAV_RESOURCE_KIND_UNKNOWN)
			return TRUE;

		href = e_xml_xpath_eval_as_string (xpath_ctx, "%s/../../D:href", xpath_prop_prefix);
		if (!href)
			return TRUE;

		if (!strstr (href, "://")) {
			SoupURI *soup_uri;
			gchar *full_uri;

			soup_uri = soup_uri_copy ((SoupURI *) request_uri);
			soup_uri_set_path (soup_uri, href);
			soup_uri_set_user (soup_uri, NULL);
			soup_uri_set_password (soup_uri, NULL);

			full_uri = soup_uri_to_string (soup_uri, FALSE);

			soup_uri_free (soup_uri);
			g_free (href);

			href = full_uri;
		}

		supports = e_webdav_session_extract_supports (xpath_ctx, xpath_prop_prefix);
		etag = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "D:getetag", "CS:getctag");
		display_name = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "D:displayname", NULL);
		content_type = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "D:getcontenttype", NULL);
		content_length = e_webdav_session_extract_content_length (xpath_ctx, xpath_prop_prefix);
		creation_date = e_webdav_session_extract_datetime (xpath_ctx, xpath_prop_prefix, "D:creationdate", TRUE);
		last_modified = e_webdav_session_extract_datetime (xpath_ctx, xpath_prop_prefix, "D:getlastmodified", FALSE);
		description = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "C:calendar-description", "A:addressbook-description");
		color = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "IC:calendar-color", NULL);

		resource = e_webdav_resource_new (kind, supports,
			NULL, /* href */
			NULL, /* etag */
			NULL, /* display_name */
			NULL, /* content_type */
			content_length,
			creation_date,
			last_modified,
			NULL, /* description */
			NULL); /* color */
		resource->href = href;
		resource->etag = etag;
		resource->display_name = display_name;
		resource->content_type = content_type;
		resource->description = description;
		resource->color = color;

		*out_resources = g_slist_prepend (*out_resources, resource);
	}

	return TRUE;
}

/**
 * e_webdav_session_list_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @flags: a bit-or of #EWebDAVListFlags, claiming what properties to read
 * @out_resources: (out) (transfer full) (element-type EWebDAVResource): return location for the resources
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Lists content of the @uri, or the one stored with the associated #ESource,
 * which should point to a collection. The @flags influences which properties
 * are read for the resources.
 *
 * The @out_resources is in no particular order.
 *
 * Free the returned @out_resources with
 * g_slist_free_full (resources, e_webdav_resource_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_list_sync (EWebDAVSession *webdav,
			    const gchar *uri,
			    guint32 flags,
			    GSList **out_resources,
			    GCancellable *cancellable,
			    GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_resources != NULL, FALSE);

	*out_resources = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");

	e_xml_document_start_element (xml, NULL, "resourcetype");
	e_xml_document_end_element (xml);

	if ((flags & E_WEBDAV_LIST_SUPPORTS) != 0 ||
	    (flags & E_WEBDAV_LIST_DESCRIPTION) != 0 ||
	    (flags & E_WEBDAV_LIST_COLOR) != 0) {
		e_xml_document_add_namespaces (xml, "C", E_WEBDAV_NS_CALDAV, NULL);
	}

	if ((flags & E_WEBDAV_LIST_SUPPORTS) != 0) {
		e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "supported-calendar-component-set");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_DISPLAY_NAME) != 0) {
		e_xml_document_start_element (xml, NULL, "displayname");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_ETAG) != 0) {
		e_xml_document_start_element (xml, NULL, "getetag");
		e_xml_document_end_element (xml);

		e_xml_document_add_namespaces (xml, "CS", E_WEBDAV_NS_CALENDARSERVER, NULL);

		e_xml_document_start_element (xml, E_WEBDAV_NS_CALENDARSERVER, "getctag");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_CONTENT_TYPE) != 0) {
		e_xml_document_start_element (xml, NULL, "getcontenttype");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_CONTENT_LENGTH) != 0) {
		e_xml_document_start_element (xml, NULL, "getcontentlength");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_CREATION_DATE) != 0) {
		e_xml_document_start_element (xml, NULL, "creationdate");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_LAST_MODIFIED) != 0) {
		e_xml_document_start_element (xml, NULL, "getlastmodified");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_DESCRIPTION) != 0) {
		e_xml_document_start_element (xml, E_WEBDAV_NS_CALDAV, "calendar-description");
		e_xml_document_end_element (xml);

		e_xml_document_add_namespaces (xml, "A", E_WEBDAV_NS_CARDDAV, NULL);

		e_xml_document_start_element (xml, E_WEBDAV_NS_CARDDAV, "addressbook-description");
		e_xml_document_end_element (xml);
	}

	if ((flags & E_WEBDAV_LIST_COLOR) != 0) {
		e_xml_document_add_namespaces (xml, "IC", E_WEBDAV_NS_ICAL, NULL);

		e_xml_document_start_element (xml, E_WEBDAV_NS_ICAL, "calendar-color");
		e_xml_document_end_element (xml);
	}

	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_1, xml,
		e_webdav_session_list_cb, out_resources, cancellable, error);

	g_object_unref (xml);

	/* Ensure display name in case the resource doesn't have any */
	if (success && (flags & E_WEBDAV_LIST_DISPLAY_NAME) != 0) {
		GSList *link;

		for (link = *out_resources; link; link = g_slist_next (link)) {
			EWebDAVResource *resource = link->data;

			if (resource && !resource->display_name && resource->href) {
				gchar *href_decoded = soup_uri_decode (resource->href);

				if (href_decoded) {
					gchar *cp;

					/* Use the last non-empty path segment. */
					while ((cp = strrchr (href_decoded, '/')) != NULL) {
						if (*(cp + 1) == '\0')
							*cp = '\0';
						else {
							resource->display_name = g_strdup (cp + 1);
							break;
						}
					}
				}

				g_free (href_decoded);
			}
		}
	}

	if (success) {
		/* Honour order returned by the server, even it's not significant. */
		*out_resources = g_slist_reverse (*out_resources);
	}

	return success;
}

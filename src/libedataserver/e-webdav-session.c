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

#include <stdio.h>
#include <glib/gi18n-lib.h>

#include "camel/camel.h"

#include "e-source-authentication.h"
#include "e-source-webdav.h"
#include "e-xml-utils.h"

#include "e-webdav-session.h"

#define BUFFER_SIZE 16384

struct _EWebDAVSessionPrivate {
	gboolean dummy;
};

G_DEFINE_TYPE (EWebDAVSession, e_webdav_session, E_TYPE_SOUP_SESSION)

G_DEFINE_BOXED_TYPE (EWebDAVResource, e_webdav_resource, e_webdav_resource_copy, e_webdav_resource_free)
G_DEFINE_BOXED_TYPE (EWebDAVPropertyChange, e_webdav_property_change, e_webdav_property_change_copy, e_webdav_property_change_free)

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
 * @src: (nullable): an #EWebDAVResource to make a copy of
 *
 * Returns: (transfer full): A new #EWebDAVResource prefilled with
 *    the same values as @src, or %NULL, when @src is %NULL.
 *    Free it with e_webdav_resource_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVResource *
e_webdav_resource_copy (const EWebDAVResource *src)
{
	if (!src)
		return NULL;

	return e_webdav_resource_new (src->kind,
		src->supports,
		src->href,
		src->etag,
		src->display_name,
		src->content_type,
		src->content_length,
		src->creation_date,
		src->last_modified,
		src->description,
		src->color);
}

/**
 * e_webdav_resource_free:
 * @ptr: (nullable): an #EWebDAVResource
 *
 * Frees an #EWebDAVResource previously created with e_webdav_resource_new()
 * or e_webdav_resource_copy(). The function does nothing if @ptr is %NULL.
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

static EWebDAVPropertyChange *
e_webdav_property_change_new (EWebDAVPropertyChangeKind kind,
			      const gchar *ns_uri,
			      const gchar *name,
			      const gchar *value)
{
	EWebDAVPropertyChange *change;

	change = g_new0 (EWebDAVPropertyChange, 1);
	change->kind = kind;
	change->ns_uri = g_strdup (ns_uri);
	change->name = g_strdup (name);
	change->value = g_strdup (value);

	return change;
}

/**
 * e_webdav_property_change_new_set:
 * @ns_uri: namespace URI of the property
 * @name: name of the property
 * @value: (nullable): value of the property, or %NULL for empty value
 *
 * Creates a new #EWebDAVPropertyChange of kind %E_WEBDAV_PROPERTY_SET,
 * which is used to modify or set the property value. The @value is a string
 * representation of the value to store. It can be %NULL, but it means
 * an empty value, not to remove it. To remove property use
 * e_webdav_property_change_new_remove() instead.
 *
 * Returns: (transfer full): A new #EWebDAVPropertyChange. Free it with
 *    e_webdav_property_change_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVPropertyChange *
e_webdav_property_change_new_set (const gchar *ns_uri,
				  const gchar *name,
				  const gchar *value)
{
	g_return_val_if_fail (ns_uri != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return e_webdav_property_change_new (E_WEBDAV_PROPERTY_SET, ns_uri, name, value);
}

/**
 * e_webdav_property_change_new_remove:
 * @ns_uri: namespace URI of the property
 * @name: name of the property
 *
 * Creates a new #EWebDAVPropertyChange of kind %E_WEBDAV_PROPERTY_REMOVE,
 * which is used to remove the given property. To change property value
 * use e_webdav_property_change_new_set() instead.
 *
 * Returns: (transfer full): A new #EWebDAVPropertyChange. Free it with
 *    e_webdav_property_change_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVPropertyChange *
e_webdav_property_change_new_remove (const gchar *ns_uri,
				     const gchar *name)
{
	g_return_val_if_fail (ns_uri != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return e_webdav_property_change_new (E_WEBDAV_PROPERTY_REMOVE, ns_uri, name, NULL);
}

/**
 * e_webdav_property_change_copy:
 * @src: (nullable): an #EWebDAVPropertyChange to make a copy of
 *
 * Returns: (transfer full): A new #EWebDAVPropertyChange prefilled with
 *    the same values as @src, or %NULL, when @src is %NULL.
 *    Free it with e_webdav_property_change_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVPropertyChange *
e_webdav_property_change_copy (const EWebDAVPropertyChange *src)
{
	if (!src)
		return NULL;

	return e_webdav_property_change_new (
		src->kind,
		src->ns_uri,
		src->name,
		src->value);
}

/**
 * e_webdav_property_change_free:
 * @ptr: (nullable): an #EWebDAVPropertyChange
 *
 * Frees an #EWebDAVPropertyChange previously created with e_webdav_property_change_new_set(),
 * e_webdav_property_change_new_remove() or or e_webdav_property_change_copy().
 * The function does nothing if @ptr is %NULL.
 *
 * Since: 3.26
 **/
void
e_webdav_property_change_free (gpointer ptr)
{
	EWebDAVPropertyChange *change = ptr;

	if (change) {
		g_free (change->ns_uri);
		g_free (change->name);
		g_free (change->value);
		g_free (change);
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
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS, %E_WEBDAV_DEPTH_THIS_AND_CHILDREN or %E_WEBDAV_DEPTH_INFINITY
 * @xml: (nullable): the request itself, as an #EXmlDocument, the root element should be DAV:propfind, or %NULL
 * @func: an #EWebDAVMultistatusTraverseFunc function to call for each DAV:propstat in the multistatus response
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
				EWebDAVMultistatusTraverseFunc func,
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

	soup_message_headers_replace (message->request_headers, "Depth", depth);

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

	if (success)
		success = e_webdav_session_traverse_multistatus_response (webdav, message, bytes, func, func_user_data, error);

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

static gboolean
e_webdav_session_extract_multistatus_error_cb (EWebDAVSession *webdav,
					       xmlXPathContextPtr xpath_ctx,
					       const gchar *xpath_prop_prefix,
					       const SoupURI *request_uri,
					       guint status_code,
					       gpointer user_data)
{
	GError **error = user_data;

	g_return_val_if_fail (error != NULL, FALSE);

	if (!xpath_prop_prefix)
		return TRUE;

	if (status_code != SOUP_STATUS_OK && (
	    status_code != SOUP_STATUS_FAILED_DEPENDENCY ||
	    !*error)) {
		gchar *description;

		description = e_xml_xpath_eval_as_string (xpath_ctx, "%s/../D:responsedescription", xpath_prop_prefix);
		if (!description || !*description) {
			g_free (description);

			description = e_xml_xpath_eval_as_string (xpath_ctx, "%s/../../D:responsedescription", xpath_prop_prefix);
		}

		g_clear_error (error);
		g_set_error (error, SOUP_HTTP_ERROR, status_code, _("Failed to update properties: %s"),
			e_soup_session_util_status_to_string (status_code, description));

		g_free (description);
	}

	return TRUE;
}

/**
 * e_webdav_session_proppatch_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @xml: an #EXmlDocument with request changes, its root element should be DAV:propertyupdate
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues PROPPATCH request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource, with the @changes. The order of requested changes
 * inside @xml is significant, unlike on other places.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_proppatch_sync (EWebDAVSession *webdav,
				 const gchar *uri,
				 const EXmlDocument *xml,
				 GCancellable *cancellable,
				 GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gchar *content;
	gsize content_length;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_PROPPATCH, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	content = e_xml_document_get_content (xml, &content_length);
	if (!content) {
		g_object_unref (message);
		g_object_unref (request);

		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

		return FALSE;
	}

	soup_message_set_request (message, E_WEBDAV_CONTENT_TYPE_XML,
		SOUP_MEMORY_TAKE, content, content_length);

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	g_object_unref (request);

	success = bytes != NULL;

	if (success) {
		GError *local_error = NULL;

		success = e_webdav_session_traverse_multistatus_response (webdav, message, bytes,
			e_webdav_session_extract_multistatus_error_cb, &local_error, error);

		if (success && local_error) {
			g_propagate_error (error, local_error);
			success = FALSE;
		} else if (local_error) {
			g_clear_error (&local_error);
		}
	}

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_mkcol_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the collection to create
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new collection resource identified by @uri on the server.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_mkcol_sync (EWebDAVSession *webdav,
			     const gchar *uri,
			     GCancellable *cancellable,
			     GError **error)
{
	SoupRequestHTTP *request;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_MKCOL, uri, error);
	if (!request)
		return FALSE;

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (request);

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

static void
e_webdav_session_extract_href_and_etag (SoupMessage *message,
					gchar **out_href,
					gchar **out_etag)
{
	g_return_if_fail (SOUP_IS_MESSAGE (message));

	if (out_href) {
		const gchar *header;

		*out_href = NULL;

		header = soup_message_headers_get_list (message->response_headers, "Location");
		if (header) {
			gchar *file = strrchr (header, '/');

			if (file) {
				gchar *decoded;

				decoded = soup_uri_decode (file + 1);
				*out_href = soup_uri_encode (decoded ? decoded : (file + 1), NULL);

				g_free (decoded);
			}
		}

		if (!*out_href)
			*out_href = soup_uri_to_string (soup_message_get_uri (message), FALSE);
	}

	if (out_etag) {
		const gchar *header;

		*out_etag = NULL;

		header = soup_message_headers_get_list (message->response_headers, "ETag");
		if (header)
			*out_etag = e_webdav_session_maybe_dequote (g_strdup (header));
	}
}

/**
 * e_webdav_session_get_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to read
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_stream: (out) (caller-allocates): a #GOutputStream to write data to
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads a resource identified by @uri from the server and writes it
 * to the @stream. The URI cannot reference a collection.
 *
 * Free returned pointer of @out_href and @out_etag, if not %NULL, with g_free(),
 * when no longer needed.
 *
 * The e_webdav_session_get_data_sync() can be used to read the resource data
 * directly to memory.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_sync (EWebDAVSession *webdav,
			   const gchar *uri,
			   gchar **out_href,
			   gchar **out_etag,
			   GOutputStream *out_stream,
			   GCancellable *cancellable,
			   GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GInputStream *input_stream;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (G_IS_OUTPUT_STREAM (out_stream), FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_GET, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	input_stream = e_soup_session_send_request_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	success = input_stream != NULL;

	if (success) {
		SoupLoggerLogLevel log_level = e_soup_session_get_log_level (E_SOUP_SESSION (webdav));
		gpointer buffer;
		gsize nread = 0, nwritten;

		buffer = g_malloc (BUFFER_SIZE);

		while (success = g_input_stream_read_all (input_stream, buffer, BUFFER_SIZE, &nread, cancellable, error),
		       success && nread > 0) {
			if (log_level == SOUP_LOGGER_LOG_BODY) {
				fwrite (buffer, 1, nread, stdout);
				fflush (stdout);
			}

			success = g_output_stream_write_all (out_stream, buffer, nread, &nwritten, cancellable, error);
			if (!success)
				break;
		}

		g_free (buffer);
	}

	if (success)
		e_webdav_session_extract_href_and_etag (message, out_href, out_etag);

	g_clear_object (&input_stream);
	g_object_unref (message);
	g_object_unref (request);

	return success;
}

/**
 * e_webdav_session_get_data_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to read
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_bytes: (out) (transfer full): return location for bytes being read
 * @out_length: (out) (nullable): option return location for length of bytes being read, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads a resource identified by @uri from the server. The URI cannot
 * reference a collection.
 *
 * The @out_bytes is filled by actual data being read. If not %NULL, @out_length
 * is populated with how many bytes had been read. Free the @out_bytes with g_free(),
 * when no longer needed.
 *
 * Free returned pointer of @out_href and @out_etag, if not %NULL, with g_free(),
 * when no longer needed.
 *
 * To read large data use e_webdav_session_get_sync() instead.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_data_sync (EWebDAVSession *webdav,
				const gchar *uri,
				gchar **out_href,
				gchar **out_etag,
				gchar **out_bytes,
				gsize *out_length,
				GCancellable *cancellable,
				GError **error)
{
	GOutputStream *output_stream;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (out_bytes != NULL, FALSE);

	*out_bytes = NULL;
	if (out_length)
		*out_length = 0;

	output_stream = g_memory_output_stream_new_resizable ();

	success = e_webdav_session_get_sync (webdav, uri, out_href, out_etag, output_stream, cancellable, error) &&
		g_output_stream_close (output_stream, cancellable, error);

	if (success) {
		if (out_length)
			*out_length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output_stream));
		*out_bytes = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output_stream));
	}

	g_object_unref (output_stream);

	return success;
}

typedef struct _ChunkWriteData {
	SoupSession *session;
	SoupLoggerLogLevel log_level;
	GInputStream *stream;
	goffset read_from;
	gboolean wrote_any;
	gsize buffer_size;
	gpointer buffer;
	GCancellable *cancellable;
	GError *error;
} ChunkWriteData;

static void
e_webdav_session_write_next_chunk (SoupMessage *message,
				   gpointer user_data)
{
	ChunkWriteData *cwd = user_data;
	gsize nread;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (cwd != NULL);

	if (!g_input_stream_read_all (cwd->stream, cwd->buffer, cwd->buffer_size, &nread, cwd->cancellable, &cwd->error)) {
		soup_session_cancel_message (cwd->session, message, SOUP_STATUS_CANCELLED);
		return;
	}

	if (nread == 0) {
		soup_message_body_complete (message->request_body);
	} else {
		cwd->wrote_any = TRUE;
		soup_message_body_append (message->request_body, SOUP_MEMORY_TEMPORARY, cwd->buffer, nread);

		if (cwd->log_level == SOUP_LOGGER_LOG_BODY) {
			fwrite (cwd->buffer, 1, nread, stdout);
			fflush (stdout);
		}
	}
}

static void
e_webdav_session_write_restarted (SoupMessage *message,
				  gpointer user_data)
{
	ChunkWriteData *cwd = user_data;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (cwd != NULL);

	/* The 302 redirect will turn it into a GET request and
	 * reset the body encoding back to "NONE". Fix that.
	 */
	soup_message_headers_set_encoding (message->request_headers, SOUP_ENCODING_CHUNKED);
	message->method = SOUP_METHOD_PUT;

	if (cwd->wrote_any) {
		cwd->wrote_any = FALSE;

		if (!G_IS_SEEKABLE (cwd->stream) || !g_seekable_can_seek (G_SEEKABLE (cwd->stream)) ||
		    !g_seekable_seek (G_SEEKABLE (cwd->stream), cwd->read_from, G_SEEK_SET, cwd->cancellable, &cwd->error)) {
			if (!cwd->error)
				g_set_error_literal (&cwd->error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
					_("Cannot rewind input stream: Not supported"));

			soup_session_cancel_message (cwd->session, message, SOUP_STATUS_CANCELLED);
		}
	}
}

/**
 * e_webdav_session_put_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to write
 * @etag: (nullable): an ETag of the resource, if it's an existing resource, or %NULL
 * @content_type: Content-Type of the @bytes to be written
 * @stream: a #GInputStream with data to be written
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Writes data from @stream to a resource identified by @uri to the server.
 * The URI cannot reference a collection.
 *
 * The @etag argument is used to avoid clashes when overwriting existing
 * resources. It can contain three values:
 *  - %NULL - to write completely new resource
 *  - empty string - write new resource or overwrite any existing, regardless changes on the server
 *  - valid ETag - overwrite existing resource only if it wasn't changed on the server.
 *
 * Note that the actual behaviour is also influenced by #ESourceWebdav:avoid-ifmatch
 * property of the associated #ESource.
 *
 * The @out_href, if provided, is filled with the resulting URI
 * of the written resource. It can be different from the @uri when the server
 * redirected to a different location.
 *
 * The @out_etag contains ETag of the resource after it had been saved.
 *
 * The @stream should support also #GSeekable interface, because the data
 * send can require restart of the send due to redirect or other reasons.
 *
 * The e_webdav_session_put_data_sync() can be used to write data stored in memory.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_put_sync (EWebDAVSession *webdav,
			   const gchar *uri,
			   const gchar *etag,
			   const gchar *content_type,
			   GInputStream *stream,
			   gchar **out_href,
			   gchar **out_etag,
			   GCancellable *cancellable,
			   GError **error)
{
	ChunkWriteData cwd;
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gulong restarted_id, wrote_headers_id, wrote_chunk_id;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (content_type != NULL, FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (stream), FALSE);

	if (out_href)
		*out_href = NULL;
	if (out_etag)
		*out_etag = NULL;

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_PUT, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	if (!etag || *etag) {
		ESource *source;
		gboolean avoid_ifmatch = FALSE;

		source = e_soup_session_get_source (E_SOUP_SESSION (webdav));
		if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
			ESourceWebdav *webdav_extension;

			webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			avoid_ifmatch = e_source_webdav_get_avoid_ifmatch (webdav_extension);
		}

		if (!avoid_ifmatch) {
			if (etag) {
				gint len = strlen (etag);

				if (*etag == '\"' && len > 2 && etag[len - 1] == '\"') {
					soup_message_headers_replace (message->request_headers, "If-Match", etag);
				} else {
					gchar *quoted;

					quoted = g_strconcat ("\"", etag, "\"", NULL);
					soup_message_headers_replace (message->request_headers, "If-Match", quoted);
					g_free (quoted);
				}
			} else {
				soup_message_headers_replace (message->request_headers, "If-None-Match", "*");
			}
		}
	}

	cwd.session = SOUP_SESSION (webdav);
	cwd.log_level = e_soup_session_get_log_level (E_SOUP_SESSION (webdav));
	cwd.stream = stream;
	cwd.read_from = 0;
	cwd.wrote_any = FALSE;
	cwd.buffer_size = BUFFER_SIZE;
	cwd.buffer = g_malloc (cwd.buffer_size);
	cwd.cancellable = cancellable;
	cwd.error = NULL;

	if (G_IS_SEEKABLE (stream) && g_seekable_can_seek (G_SEEKABLE (stream)))
		cwd.read_from = g_seekable_tell (G_SEEKABLE (stream));

	if (content_type && *content_type)
		soup_message_headers_replace (message->request_headers, "Content-Type", content_type);

	soup_message_headers_set_encoding (message->request_headers, SOUP_ENCODING_CHUNKED);
	soup_message_body_set_accumulate (message->request_body, FALSE);
	soup_message_set_flags (message, SOUP_MESSAGE_CAN_REBUILD);

	restarted_id = g_signal_connect (message, "restarted", G_CALLBACK (e_webdav_session_write_restarted), &cwd);
	wrote_headers_id = g_signal_connect (message, "wrote-headers", G_CALLBACK (e_webdav_session_write_next_chunk), &cwd);
	wrote_chunk_id = g_signal_connect (message, "wrote-chunk", G_CALLBACK (e_webdav_session_write_next_chunk), &cwd);

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	g_signal_handler_disconnect (message, restarted_id);
	g_signal_handler_disconnect (message, wrote_headers_id);
	g_signal_handler_disconnect (message, wrote_chunk_id);

	success = bytes != NULL;

	if (cwd.error) {
		g_clear_error (error);
		g_propagate_error (error, cwd.error);
		success = FALSE;
	}

	if (success) {
		if (success && !SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
			success = FALSE;

			g_set_error (error, SOUP_HTTP_ERROR, message->status_code,
				_("Failed to put data to server, error code %d (%s)"), message->status_code,
				e_soup_session_util_status_to_string (message->status_code, message->reason_phrase));
		}
	}

	if (success)
		e_webdav_session_extract_href_and_etag (message, out_href, out_etag);

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);
	g_object_unref (request);
	g_free (cwd.buffer);

	return success;
}

/**
 * e_webdav_session_put_data_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to write
 * @etag: (nullable): an ETag of the resource, if it's an existing resource, or %NULL
 * @content_type: Content-Type of the @bytes to be written
 * @bytes: actual bytes to be written
 * @length: how many bytes to write, or -1, when the @bytes is NUL-terminated
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Writes data to a resource identified by @uri to the server. The URI cannot
 * reference a collection.
 *
 * The @etag argument is used to avoid clashes when overwriting existing
 * resources. It can contain three values:
 *  - %NULL - to write completely new resource
 *  - empty string - write new resource or overwrite any existing, regardless changes on the server
 *  - valid ETag - overwrite existing resource only if it wasn't changed on the server.
 *
 * Note that the actual usage of @etag is also influenced by #ESourceWebdav:avoid-ifmatch
 * property of the associated #ESource.
 *
 * The @out_href, if provided, is filled with the resulting URI
 * of the written resource. It can be different from the @uri when the server
 * redirected to a different location.
 *
 * The @out_etag contains ETag of the resource after it had been saved.
 *
 * To read large data use e_webdav_session_put_sync() instead.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_put_data_sync (EWebDAVSession *webdav,
				const gchar *uri,
				const gchar *etag,
				const gchar *content_type,
				const gchar *bytes,
				gsize length,
				gchar **out_href,
				gchar **out_etag,
				GCancellable *cancellable,
				GError **error)
{
	GInputStream *input_stream;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (content_type != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);

	if (length == (gsize) -1)
		length = strlen (bytes);

	input_stream = g_memory_input_stream_new_from_data (bytes, length, NULL);

	success = e_webdav_session_put_sync (webdav, uri, etag, content_type,
		input_stream, out_href, out_etag, cancellable, error);

	g_object_unref (input_stream);

	return success;
}

/**
 * e_webdav_session_delete_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to delete
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS or %E_WEBDAV_DEPTH_INFINITY
 * @etag: (nullable): an optional ETag of the resource, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes a resource identified by @uri on the server. The URI can
 * reference a collection, in which case @depth should be %E_WEBDAV_DEPTH_INFINITY.
 * Use @depth %E_WEBDAV_DEPTH_THIS when deleting a regular resource.
 *
 * The @etag argument is used to avoid clashes when overwriting existing resources.
 * Use %NULL @etag when deleting collection resources or to force the deletion,
 * otherwise provide a valid ETag of a non-collection resource to verify that
 * the version requested to delete is the same as on the server.
 *
 * Note that the actual usage of @etag is also influenced by #ESourceWebdav:avoid-ifmatch
 * property of the associated #ESource.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_delete_sync (EWebDAVSession *webdav,
			      const gchar *uri,
			      const gchar *depth,
			      const gchar *etag,
			      GCancellable *cancellable,
			      GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_DELETE, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	if (etag) {
		ESource *source;
		gboolean avoid_ifmatch = FALSE;

		source = e_soup_session_get_source (E_SOUP_SESSION (webdav));
		if (source && e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
			ESourceWebdav *webdav_extension;

			webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			avoid_ifmatch = e_source_webdav_get_avoid_ifmatch (webdav_extension);
		}

		if (!avoid_ifmatch) {
			gint len = strlen (etag);

			if (*etag == '\"' && len > 2 && etag[len - 1] == '\"') {
				soup_message_headers_replace (message->request_headers, "If-Match", etag);
			} else {
				gchar *quoted;

				quoted = g_strconcat ("\"", etag, "\"", NULL);
				soup_message_headers_replace (message->request_headers, "If-Match", quoted);
				g_free (quoted);
			}
		}
	}

	soup_message_headers_replace (message->request_headers, "Depth", depth);

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);
	g_object_unref (request);

	return success;
}

/**
 * e_webdav_session_copy_sync:
 * @webdav: an #EWebDAVSession
 * @source_uri: URI of the resource or collection to copy
 * @destination_uri: URI of the destination
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS or %E_WEBDAV_DEPTH_INFINITY
 * @can_overwrite: whether can overwrite @destination_uri, when it exists
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Copies a resource identified by @source_uri to @destination_uri on the server.
 * The @source_uri can reference also collections, in which case the @depth influences
 * whether only the collection itself is copied (%E_WEBDAV_DEPTH_THIS) or whether
 * the collection with all its children is copied (%E_WEBDAV_DEPTH_INFINITY).
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_copy_sync (EWebDAVSession *webdav,
			    const gchar *source_uri,
			    const gchar *destination_uri,
			    const gchar *depth,
			    gboolean can_overwrite,
			    GCancellable *cancellable,
			    GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (source_uri != NULL, FALSE);
	g_return_val_if_fail (destination_uri != NULL, FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_COPY, source_uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	soup_message_headers_replace (message->request_headers, "Depth", depth);
	soup_message_headers_replace (message->request_headers, "Destination", destination_uri);
	soup_message_headers_replace (message->request_headers, "Overwrite", can_overwrite ? "T" : "F");

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);
	g_object_unref (request);

	return success;
}

/**
 * e_webdav_session_move_sync:
 * @webdav: an #EWebDAVSession
 * @source_uri: URI of the resource or collection to copy
 * @destination_uri: URI of the destination
 * @can_overwrite: whether can overwrite @destination_uri, when it exists
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Moves a resource identified by @source_uri to @destination_uri on the server.
 * The @source_uri can reference also collections.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_move_sync (EWebDAVSession *webdav,
			    const gchar *source_uri,
			    const gchar *destination_uri,
			    gboolean can_overwrite,
			    GCancellable *cancellable,
			    GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (source_uri != NULL, FALSE);
	g_return_val_if_fail (destination_uri != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_MOVE, source_uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	soup_message_headers_replace (message->request_headers, "Depth", E_WEBDAV_DEPTH_INFINITY);
	soup_message_headers_replace (message->request_headers, "Destination", destination_uri);
	soup_message_headers_replace (message->request_headers, "Overwrite", can_overwrite ? "T" : "F");

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);
	g_object_unref (request);

	return success;
}

/**
 * e_webdav_session_lock_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to lock, or %NULL to read from #ESource
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS or %E_WEBDAV_DEPTH_INFINITY
 * @lock_timeout: timeout for the lock, in seconds, on 0 to infinity
 * @xml: an XML describing the lock request, with DAV:lockinfo root element
 * @out_lock_token: (out) (transfer full): return location of the obtained or refreshed lock token
 * @out_xml_response: (out) (nullable) (transfer full): optional return location for the server response as #xmlDocPtr
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Locks a resource identified by @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource.
 *
 * The @out_lock_token can be refreshed with e_webdav_session_refresh_lock_sync().
 * Release the lock with e_webdav_session_unlock_sync().
 * Free the returned @out_lock_token with g_free(), when no longer needed.
 *
 * If provided, free the returned @out_xml_response with xmlFreeDoc(),
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_lock_sync (EWebDAVSession *webdav,
			    const gchar *uri,
			    const gchar *depth,
			    gint32 lock_timeout,
			    const EXmlDocument *xml,
			    gchar **out_lock_token,
			    xmlDocPtr *out_xml_response,
			    GCancellable *cancellable,
			    GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);
	g_return_val_if_fail (out_lock_token != NULL, FALSE);

	*out_lock_token = NULL;

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_LOCK, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	if (depth)
		soup_message_headers_replace (message->request_headers, "Depth", depth);

	if (lock_timeout) {
		gchar *value;

		value = g_strdup_printf ("Second-%d", lock_timeout);
		soup_message_headers_replace (message->request_headers, "Timeout", value);
		g_free (value);
	} else {
		soup_message_headers_replace (message->request_headers, "Timeout", "Infinite");
	}

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

	if (success && out_xml_response) {
		const gchar *content_type;

		*out_xml_response = NULL;

		content_type = soup_message_headers_get_content_type (message->response_headers, NULL);
		if (!content_type ||
		    (g_ascii_strcasecmp (content_type, "application/xml") != 0 &&
		     g_ascii_strcasecmp (content_type, "text/xml") != 0)) {
			if (!content_type) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but none returned"));
			} else {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but %s returned"), content_type);
			}

			success = FALSE;
		}

		if (success) {
			xmlDocPtr doc;

			doc = e_xml_parse_data ((const gchar *) bytes->data, bytes->len);
			if (!doc) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Failed to parse XML data"));

				success = FALSE;
			} else {
				*out_xml_response = doc;
			}
		}
	}

	if (success)
		*out_lock_token = g_strdup (soup_message_headers_get_list (message->response_headers, "Lock-Token"));

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_refresh_lock_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to lock, or %NULL to read from #ESource
 * @lock_token: token of an existing lock
 * @lock_timeout: timeout for the lock, in seconds, on 0 to infinity
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Refreshes existing lock @lock_token for a resource identified by @uri,
 * or, in case it's %NULL, on the URI defined in associated #ESource.
 * The @lock_token is returned from e_webdav_session_lock_sync() and
 * the @uri should be the same as that used with e_webdav_session_lock_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_refresh_lock_sync (EWebDAVSession *webdav,
				    const gchar *uri,
				    const gchar *lock_token,
				    gint32 lock_timeout,
				    GCancellable *cancellable,
				    GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gchar *value;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (lock_token != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_LOCK, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	if (lock_timeout) {
		value = g_strdup_printf ("Second-%d", lock_timeout);
		soup_message_headers_replace (message->request_headers, "Timeout", value);
		g_free (value);
	} else {
		soup_message_headers_replace (message->request_headers, "Timeout", "Infinite");
	}

	soup_message_headers_replace (message->request_headers, "Lock-Token", lock_token);

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	g_object_unref (request);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_unlock_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to lock, or %NULL to read from #ESource
 * @lock_token: token of an existing lock
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Releases (unlocks) existing lock @lock_token for a resource identified by @uri,
 * or, in case it's %NULL, on the URI defined in associated #ESource.
 * The @lock_token is returned from e_webdav_session_lock_sync() and
 * the @uri should be the same as that used with e_webdav_session_lock_sync().
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_unlock_sync (EWebDAVSession *webdav,
			      const gchar *uri,
			      const gchar *lock_token,
			      GCancellable *cancellable,
			      GError **error)
{
	SoupRequestHTTP *request;
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (lock_token != NULL, FALSE);

	request = e_webdav_session_new_request (webdav, SOUP_METHOD_UNLOCK, uri, error);
	if (!request)
		return FALSE;

	message = soup_request_http_get_message (request);
	if (!message) {
		g_warn_if_fail (message != NULL);
		g_object_unref (request);

		return FALSE;
	}

	soup_message_headers_replace (message->request_headers, "Lock-Token", lock_token);

	bytes = e_soup_session_send_request_simple_sync (E_SOUP_SESSION (webdav), request, cancellable, error);

	g_object_unref (request);

	success = bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_traverse_multistatus_response:
 * @webdav: an #EWebDAVSession
 * @message: (nullable): an optional #SoupMessage corresponding to the response, or %NULL
 * @xml_data: a #GByteArray containing DAV:multistatus response
 * @func: an #EWebDAVMultistatusTraverseFunc function to call for each DAV:propstat in the multistatus response
 * @func_user_data: user data passed to @func
 * @error: return location for a #GError, or %NULL
 *
 * Traverses a DAV:multistatus response and calls @func for each returned DAV:propstat.
 * The provided XPath context has registered %E_WEBDAV_NS_DAV namespace with prefix "D".
 * It doesn't have any other namespace registered.
 *
 * The @message, if provided, is used to verify that the response is a multi-status
 * and that the Content-Type is properly set. It's used to get a request URI as well.
 *
 * The @func is called always at least once, with %NULL xpath_prop_prefix, which
 * is meant to let the caller setup the xpath_ctx, like to register its own namespaces
 * to it with e_xml_xpath_context_register_namespaces(). All other invocations of @func
 * will have xpath_prop_prefix non-%NULL.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_traverse_multistatus_response (EWebDAVSession *webdav,
						const SoupMessage *message,
						const GByteArray *xml_data,
						EWebDAVMultistatusTraverseFunc func,
						gpointer func_user_data,
						GError **error)
{
	SoupURI *request_uri = NULL;
	xmlDocPtr doc;
	xmlXPathContextPtr xpath_ctx;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xml_data != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (message) {
		const gchar *content_type;

		if (message->status_code != SOUP_STATUS_MULTI_STATUS) {
			g_set_error (error, SOUP_HTTP_ERROR, message->status_code,
				_("Expected multistatus response, but %d returned (%s)"), message->status_code,
				e_soup_session_util_status_to_string (message->status_code, message->reason_phrase));

			return FALSE;
		}

		content_type = soup_message_headers_get_content_type (message->response_headers, NULL);
		if (!content_type ||
		    (g_ascii_strcasecmp (content_type, "application/xml") != 0 &&
		     g_ascii_strcasecmp (content_type, "text/xml") != 0)) {
			if (!content_type) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but none returned"));
			} else {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
					_("Expected application/xml response, but %s returned"), content_type);
			}

			return FALSE;
		}

		request_uri = soup_message_get_uri ((SoupMessage *) message);
	}

	doc = e_xml_parse_data ((const gchar *) xml_data->data, xml_data->len);

	if (!doc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("Failed to parse XML data"));

		return FALSE;
	}

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

	return TRUE;
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

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
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
				else if (g_ascii_strcasecmp (name, "VFREEBUSY") == 0)
					supports |= E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY;

				g_free (name);
			}

			xmlXPathFreeObject (xpath_obj);
		} else {
			/* If the property is not present, assume all component
			 * types are supported.  (RFC 4791, Section 5.2.3) */
			supports = supports |
				E_WEBDAV_RESOURCE_SUPPORTS_EVENTS |
				E_WEBDAV_RESOURCE_SUPPORTS_MEMOS |
				E_WEBDAV_RESOURCE_SUPPORTS_TASKS |
				E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY;
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

	g_return_val_if_fail (xpath_ctx != NULL, 0);
	g_return_val_if_fail (xpath_prop_prefix != NULL, 0);

	value = e_webdav_session_extract_nonempty (xpath_ctx, xpath_prop_prefix, "D:getcontentlength", NULL);
	if (!value)
		return 0;

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
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS, %E_WEBDAV_DEPTH_THIS_AND_CHILDREN or %E_WEBDAV_DEPTH_INFINITY
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
			    const gchar *depth,
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

	success = e_webdav_session_propfind_sync (webdav, uri, depth, xml,
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

/**
 * e_webdav_session_update_properties_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @changes: (element-type EWebDAVResource): a #GSList with request changes
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Updates proeprties (set/remove) on the provided @uri, or, in case it's %NULL,
 * on the URI defined in associated #ESource, with the @changes. The order
 * of @changes is significant, unlike on other places.
 *
 * This function supports only flat properties, those not under other element.
 * To support more complex property tries use e_webdav_session_proppatch_sync()
 * directly.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_update_properties_sync (EWebDAVSession *webdav,
					 const gchar *uri,
					 const GSList *changes,
					 GCancellable *cancellable,
					 GError **error)
{
	EXmlDocument *xml;
	GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (changes != NULL, FALSE);

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propertyupdate");
	g_return_val_if_fail (xml != NULL, FALSE);

	for (link = (GSList *) changes; link; link = g_slist_next (link)) {
		EWebDAVPropertyChange *change = link->data;

		if (!change)
			continue;

		switch (change->kind) {
		case E_WEBDAV_PROPERTY_SET:
			e_xml_document_start_element (xml, NULL, "set");
			e_xml_document_start_element (xml, NULL, "prop");
			e_xml_document_start_text_element (xml, change->ns_uri, change->name);
			if (change->value) {
				e_xml_document_write_string (xml, change->value);
			}
			e_xml_document_end_element (xml); /* change->name */
			e_xml_document_end_element (xml); /* prop */
			e_xml_document_end_element (xml); /* set */
			break;
		case E_WEBDAV_PROPERTY_REMOVE:
			e_xml_document_start_element (xml, NULL, "remove");
			e_xml_document_start_element (xml, NULL, "prop");
			e_xml_document_start_element (xml, change->ns_uri, change->name);
			e_xml_document_end_element (xml); /* change->name */
			e_xml_document_end_element (xml); /* prop */
			e_xml_document_end_element (xml); /* set */
			break;
		}
	}

	success = e_webdav_session_proppatch_sync (webdav, uri, xml, cancellable, error);

	g_object_unref (xml);

	return success;
}

/**
 * e_webdav_session_lock_resource_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to lock, or %NULL to read from #ESource
 * @lock_scope: an #EWebDAVLockScope to define the scope of the lock
 * @lock_timeout: timeout for the lock, in seconds, on 0 to infinity
 * @owner: (nullable): optional identificator of the owner of the lock, or %NULL
 * @out_lock_token: (out) (transfer full): return location of the obtained or refreshed lock token
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Locks a resource identified by @uri, or, in case it's %NULL, on the URI defined
 * in associated #ESource. It obtains a write lock with the given @lock_scope.
 *
 * The @owner is used to identify the lock owner. When it's an http:// or https://,
 * then it's referenced as DAV:href, otherwise the value is treated as plain text.
 * If it's %NULL, then the user name from the associated #ESource is used.
 *
 * The @out_lock_token can be refreshed with e_webdav_session_refresh_lock_sync().
 * Release the lock with e_webdav_session_unlock_sync().
 * Free the returned @out_lock_token with g_free(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_lock_resource_sync (EWebDAVSession *webdav,
				     const gchar *uri,
				     EWebDAVLockScope lock_scope,
				     gint32 lock_timeout,
				     const gchar *owner,
				     gchar **out_lock_token,
				     GCancellable *cancellable,
				     GError **error)
{
	EXmlDocument *xml;
	gchar *owner_ref;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_lock_token != NULL, FALSE);

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "lockinfo");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "lockscope");
	switch (lock_scope) {
	case E_WEBDAV_LOCK_EXCLUSIVE:
		e_xml_document_start_element (xml, NULL, "exclusive");
		e_xml_document_end_element (xml);
		break;
	case E_WEBDAV_LOCK_SHARED:
		e_xml_document_start_element (xml, NULL, "shared");
		e_xml_document_end_element (xml);
		break;
	}
	e_xml_document_end_element (xml); /* lockscope */

	e_xml_document_start_element (xml, NULL, "locktype");
	e_xml_document_start_element (xml, NULL, "write");
	e_xml_document_end_element (xml); /* write */
	e_xml_document_end_element (xml); /* locktype */

	e_xml_document_start_text_element (xml, NULL, "owner");
	if (owner) {
		owner_ref = g_strdup (owner);
	} else {
		ESource *source = e_soup_session_get_source (E_SOUP_SESSION (webdav));

		owner_ref = NULL;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
			owner_ref = e_source_authentication_dup_user (
				e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION));

			if (owner_ref && !*owner_ref)
				g_clear_pointer (&owner_ref, g_free);
		}

		if (!owner_ref && e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
			owner_ref = e_source_webdav_dup_email_address (
				e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND));

			if (owner_ref && !*owner_ref)
				g_clear_pointer (&owner_ref, g_free);
		}
	}

	if (!owner_ref)
		owner_ref = g_strconcat (g_get_host_name (), " / ", g_get_user_name (), NULL);

	if (owner_ref) {
		if (g_str_has_prefix (owner_ref, "http://") ||
		    g_str_has_prefix (owner_ref, "https://")) {
			e_xml_document_start_element (xml, NULL, "href");
			e_xml_document_write_string (xml, owner_ref);
			e_xml_document_end_element (xml); /* href */
		} else {
			e_xml_document_write_string (xml, owner_ref);
		}
	}

	g_free (owner_ref);
	e_xml_document_end_element (xml); /* owner */

	success = e_webdav_session_lock_sync (webdav, uri, E_WEBDAV_DEPTH_INFINITY, lock_timeout, xml,
		out_lock_token, NULL, cancellable, error);

	g_object_unref (xml);

	return success;
}

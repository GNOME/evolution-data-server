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

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>

#include "e-source-authentication.h"
#include "e-source-registry.h"
#include "e-source-webdav.h"
#include "e-webdav-session.h"
#include "e-xml-utils.h"

#include "e-webdav-discover.h"

typedef struct _WebDAVDiscoverData {
	GHashTable *covered_hrefs;
	GSList *addressbooks;
	GSList *calendars;
	guint32 only_supports;
	GSList **out_calendar_user_addresses;
	GCancellable *cancellable;
	GError **error;
} WebDAVDiscoverData;

#define CUSTOM_SUPPORTS_FLAGS (E_WEBDAV_DISCOVER_SUPPORTS_CALENDAR_AUTO_SCHEDULE | E_WEBDAV_DISCOVER_SUPPORTS_SUBSCRIBED_ICALENDAR)

G_DEFINE_BOXED_TYPE (EWebDAVDiscoveredSource, e_webdav_discovered_source, e_webdav_discovered_source_copy, e_webdav_discovered_source_free)

static gboolean
e_webdav_discovery_already_discovered (const gchar *href,
				       const GSList *discovered_sources,
				       guint32 href_supports)
{
	GSList *link;

	for (link = (GSList *) discovered_sources; link; link = g_slist_next (link)) {
		EWebDAVDiscoveredSource *discovered = link->data;

		if (discovered && g_strcmp0 (href, discovered->href) == 0) {
			discovered->supports |= href_supports;
			return TRUE;
		}
	}

	return FALSE;
}

static void
e_webdav_discover_split_resources (WebDAVDiscoverData *wdd,
				   const GSList *resources)
{
	const GSList *link;

	g_return_if_fail (wdd != NULL);

	for (link = resources; link; link = g_slist_next (link)) {
		const EWebDAVResource *resource = link->data;

		if (resource && (
		    resource->kind == E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK ||
		    resource->kind == E_WEBDAV_RESOURCE_KIND_CALENDAR ||
		    resource->kind == E_WEBDAV_RESOURCE_KIND_SUBSCRIBED_ICALENDAR ||
		    resource->kind == E_WEBDAV_RESOURCE_KIND_WEBDAV_NOTES)) {
			EWebDAVDiscoveredSource *discovered;

			if ((wdd->only_supports & (~CUSTOM_SUPPORTS_FLAGS)) != E_WEBDAV_DISCOVER_SUPPORTS_NONE &&
			    (resource->supports & wdd->only_supports) == 0)
				continue;

			if (e_webdav_discovery_already_discovered (resource->href,
				resource->kind == E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK ? wdd->addressbooks : wdd->calendars,
				resource->supports))
				continue;

			discovered = g_slice_new0 (EWebDAVDiscoveredSource);
			discovered->href = g_strdup (resource->href);
			discovered->supports = resource->supports;
			discovered->display_name = g_strdup (resource->display_name);
			discovered->description = g_strdup (resource->description);
			discovered->color = g_strdup (resource->color);
			discovered->order = resource->order;

			if (resource->kind == E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK) {
				wdd->addressbooks = g_slist_prepend (wdd->addressbooks, discovered);
			} else {
				if (resource->kind == E_WEBDAV_RESOURCE_KIND_SUBSCRIBED_ICALENDAR)
					discovered->supports |= E_WEBDAV_DISCOVER_SUPPORTS_SUBSCRIBED_ICALENDAR;

				wdd->calendars = g_slist_prepend (wdd->calendars, discovered);
			}
		}
	}
}

typedef enum {
	COVERED_LOOKUP		= 1 << 0,
	COVERED_ADDRESSBOOK	= 1 << 1,
	COVERED_CALENDAR	= 1 << 2
} ECoveredMark;

static void
e_webdav_discover_mark_covered (GHashTable *covered_hrefs,
				const gchar *href,
				ECoveredMark mark)
{
	gint value;

	if (!covered_hrefs || !href || !*href)
		return;

	value = GPOINTER_TO_INT (g_hash_table_lookup (covered_hrefs, href));

	if ((value & mark) != mark) {
		value |= mark;
		g_hash_table_insert (covered_hrefs, g_strdup (href), GINT_TO_POINTER (value));
	}
}

static gboolean
e_webdav_discover_is_covered (GHashTable *covered_hrefs,
			      const gchar *href,
			      ECoveredMark mark)
{
	gint value;

	if (!covered_hrefs || !href || !*href)
		return FALSE;

	value = GPOINTER_TO_INT (g_hash_table_lookup (covered_hrefs, href));

	return (value & mark) == mark;
}

static gboolean
e_webdav_discover_propfind_uri_sync (EWebDAVSession *webdav,
				     WebDAVDiscoverData *wdd,
				     const gchar *uri,
				     gboolean only_sets);

static gboolean
e_webdav_discover_traverse_propfind_response_cb (EWebDAVSession *webdav,
						 xmlNodePtr prop_node,
						 const GUri *request_uri,
						 const gchar *href,
						 guint status_code,
						 gpointer user_data)
{
	WebDAVDiscoverData *wdd = user_data;
	xmlNodePtr set_node, node;
	const xmlChar *href_value;
	gboolean is_calendar, is_addressbook;

	g_return_val_if_fail (wdd != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;

	set_node = e_xml_find_child (prop_node, E_WEBDAV_NS_CARDDAV, "addressbook-home-set");

	for (node = e_xml_find_child (set_node, E_WEBDAV_NS_DAV, "href");
	     node && !g_cancellable_is_cancelled (wdd->cancellable);
	     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "href")) {
		const xmlChar *home_set_href;

		home_set_href = e_xml_get_node_text (node);

		if (home_set_href && *home_set_href) {
			GSList *resources = NULL;
			GError *local_error = NULL;
			gchar *full_href;

			full_href = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) home_set_href);

			if (full_href && *full_href && !e_webdav_discover_is_covered (wdd->covered_hrefs, full_href, COVERED_ADDRESSBOOK) &&
			    e_webdav_session_list_sync (webdav, full_href, E_WEBDAV_DEPTH_THIS_AND_CHILDREN,
				E_WEBDAV_LIST_ONLY_ADDRESSBOOK | E_WEBDAV_LIST_ALL,
				&resources, wdd->cancellable, &local_error)) {
				e_webdav_discover_split_resources (wdd, resources);
				g_slist_free_full (resources, e_webdav_resource_free);
			}

			if (full_href && *full_href)
				e_webdav_discover_mark_covered (wdd->covered_hrefs, full_href, COVERED_ADDRESSBOOK);

			if (local_error && wdd->error && !*wdd->error)
				g_propagate_error (wdd->error, local_error);
			else
				g_clear_error (&local_error);

			g_free (full_href);
		}
	}

	set_node = e_xml_find_child (prop_node, E_WEBDAV_NS_CALDAV, "calendar-home-set");

	for (node = e_xml_find_child (set_node, E_WEBDAV_NS_DAV, "href");
	     node && !g_cancellable_is_cancelled (wdd->cancellable);
	     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "href")) {
		const xmlChar *home_set_href;

		home_set_href = e_xml_get_node_text (node);

		if (home_set_href && *home_set_href) {
			GSList *resources = NULL;
			GError *local_error = NULL;
			gchar *full_href;

			full_href = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) home_set_href);

			if (full_href && *full_href && !e_webdav_discover_is_covered (wdd->covered_hrefs, full_href, COVERED_CALENDAR) &&
			    e_webdav_session_list_sync (webdav, full_href, E_WEBDAV_DEPTH_THIS_AND_CHILDREN,
				E_WEBDAV_LIST_ONLY_CALENDAR | E_WEBDAV_LIST_ALL,
				&resources, wdd->cancellable, &local_error)) {
				e_webdav_discover_split_resources (wdd, resources);
				g_slist_free_full (resources, e_webdav_resource_free);
			}

			if (full_href && *full_href)
				e_webdav_discover_mark_covered (wdd->covered_hrefs, full_href, COVERED_CALENDAR);

			if (local_error && wdd->error && !*wdd->error)
				g_propagate_error (wdd->error, local_error);
			else
				g_clear_error (&local_error);

			g_free (full_href);
		}
	}

	if (wdd->out_calendar_user_addresses) {
		set_node = e_xml_find_child (prop_node, E_WEBDAV_NS_CALDAV, "calendar-user-address-set");

		for (node = e_xml_find_child (set_node, E_WEBDAV_NS_DAV, "href");
		     node && !g_cancellable_is_cancelled (wdd->cancellable);
		     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "href")) {
			const xmlChar *address_href;

			address_href = e_xml_get_node_text (node);

			if (address_href && g_ascii_strncasecmp ((const gchar *) address_href, "mailto:", 7) == 0) {
				/* Skip the "mailto:" prefix */
				const gchar *address = (const gchar *) (address_href + 7);

				/* Avoid duplicates and empty values */
				if (*address &&
				    !g_slist_find_custom (*wdd->out_calendar_user_addresses, address, (GCompareFunc) g_ascii_strcasecmp)) {
					*wdd->out_calendar_user_addresses = g_slist_prepend (
						*wdd->out_calendar_user_addresses, g_strdup (address));
				}
			}
		}
	}

	node = e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_DAV, "current-user-principal", E_WEBDAV_NS_DAV, "href", NULL, NULL);
	href_value = e_xml_get_node_text (node);

	if (href_value && *href_value && !g_cancellable_is_cancelled (wdd->cancellable)) {
		gchar *full_href;

		full_href = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) href_value);

		if (full_href && *full_href)
			e_webdav_discover_propfind_uri_sync (webdav, wdd, full_href, TRUE);

		g_free (full_href);

		return TRUE;
	}

	node = e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_DAV, "principal-URL", E_WEBDAV_NS_DAV, "href", NULL, NULL);
	href_value = e_xml_get_node_text (node);

	if (href_value && *href_value && !g_cancellable_is_cancelled (wdd->cancellable)) {
		gchar *full_href;

		full_href = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) href_value);

		if (full_href && *full_href)
			e_webdav_discover_propfind_uri_sync (webdav, wdd, full_href, TRUE);

		g_free (full_href);

		return TRUE;
	}

	node = e_xml_find_child (prop_node, E_WEBDAV_NS_DAV, "resourcetype");
	is_calendar = e_xml_find_child (node, E_WEBDAV_NS_CALDAV, "calendar") != NULL;
	is_addressbook = e_xml_find_child (node, E_WEBDAV_NS_CARDDAV, "addressbook") != NULL;

	if (is_calendar || is_addressbook) {
		gint covered_mark = (is_addressbook ? COVERED_ADDRESSBOOK : 0) | (is_calendar ? COVERED_CALENDAR : 0);
		GSList *resources = NULL;
		GError *local_error = NULL;

		if (!e_webdav_discover_is_covered (wdd->covered_hrefs, href, covered_mark) &&
		    !g_cancellable_is_cancelled (wdd->cancellable) &&
		    e_webdav_session_list_sync (webdav, href, E_WEBDAV_DEPTH_THIS,
			(is_calendar ? E_WEBDAV_LIST_ONLY_CALENDAR : 0) | (is_addressbook ? E_WEBDAV_LIST_ONLY_ADDRESSBOOK : 0) | E_WEBDAV_LIST_ALL,
			&resources, wdd->cancellable, &local_error)) {
			e_webdav_discover_split_resources (wdd, resources);
			g_slist_free_full (resources, e_webdav_resource_free);
		}

		e_webdav_discover_mark_covered (wdd->covered_hrefs, href, covered_mark);

		if (local_error && wdd->error && !*wdd->error)
			g_propagate_error (wdd->error, local_error);
		else
			g_clear_error (&local_error);
	}

	if (((wdd->only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
	    (wdd->only_supports & E_WEBDAV_DISCOVER_SUPPORTS_WEBDAV_NOTES) != 0) &&
	    (g_str_has_suffix (href, "/Notes") || g_str_has_suffix (href, "/Notes/")) &&
	    !e_webdav_discovery_already_discovered (href, wdd->calendars, 0) &&
	    e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_DAV, "resourcetype", E_WEBDAV_NS_DAV, "collection", NULL, NULL)) {
		GSList *resources = NULL;

		resources = g_slist_prepend (NULL,
			e_webdav_resource_new (E_WEBDAV_RESOURCE_KIND_WEBDAV_NOTES,
				E_WEBDAV_RESOURCE_SUPPORTS_WEBDAV_NOTES, href, NULL,
				_("Notes"),
				NULL, 0, 0, 0, NULL, NULL, (guint) -1));

		e_webdav_discover_split_resources (wdd, resources);

		g_slist_free_full (resources, e_webdav_resource_free);

		e_webdav_discover_mark_covered (wdd->covered_hrefs, href, COVERED_CALENDAR);
	}

	return TRUE;
}

static gboolean
e_webdav_discover_propfind_uri_sync (EWebDAVSession *webdav,
				     WebDAVDiscoverData *wdd,
				     const gchar *uri,
				     gboolean only_sets)
{
	EXmlDocument *xml;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (wdd != NULL, FALSE);
	g_return_val_if_fail (uri && *uri, FALSE);

	if (e_webdav_discover_is_covered (wdd->covered_hrefs, uri, COVERED_LOOKUP))
		return TRUE;

	e_webdav_discover_mark_covered (wdd->covered_hrefs, uri, COVERED_LOOKUP);

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");

	if (!only_sets) {
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "resourcetype");
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "current-user-principal");
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "principal-URL");
	}

	if (((wdd->only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
	    (wdd->only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_EVENTS | E_WEBDAV_DISCOVER_SUPPORTS_MEMOS | E_WEBDAV_DISCOVER_SUPPORTS_TASKS)) != 0)) {
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "calendar-home-set");
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "calendar-user-address-set");
	}

	if (((wdd->only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
	    (wdd->only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS)) != 0)) {
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CARDDAV, "addressbook-home-set");
	}

	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_discover_traverse_propfind_response_cb, wdd, wdd->cancellable, &local_error);

	g_clear_object (&xml);

	if (local_error && wdd->error && !*wdd->error)
		g_propagate_error (wdd->error, local_error);
	else
		g_clear_error (&local_error);

	return success;
}

typedef struct _EWebDAVDiscoverContext {
	ESource *source;
	gchar *url_use_path;
	guint32 only_supports;
	ENamedParameters *credentials;
	EWebDAVDiscoverRefSourceFunc ref_source_func;
	gpointer ref_source_func_user_data;
	gchar *out_certificate_pem;
	GTlsCertificateFlags out_certificate_errors;
	GSList *out_discovered_sources;
	GSList *out_calendar_user_addresses;
} EWebDAVDiscoverContext;

static EWebDAVDiscoverContext *
e_webdav_discover_context_new (ESource *source,
			       const gchar *url_use_path,
			       guint32 only_supports,
			       const ENamedParameters *credentials,
			       EWebDAVDiscoverRefSourceFunc ref_source_func,
			       gpointer ref_source_func_user_data)
{
	EWebDAVDiscoverContext *context;

	context = g_slice_new0 (EWebDAVDiscoverContext);
	context->source = g_object_ref (source);
	context->url_use_path = g_strdup (url_use_path);
	context->only_supports = only_supports;
	context->credentials = e_named_parameters_new_clone (credentials);
	context->ref_source_func = ref_source_func;
	context->ref_source_func_user_data = ref_source_func_user_data;
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
	g_slice_free (EWebDAVDiscoverContext, context);
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
	g_slist_free_full (discovered_sources, (GDestroyNotify) e_webdav_discovered_source_free);
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

	success = e_webdav_discover_sources_full_sync (E_SOURCE (source_object),
		context->url_use_path, context->only_supports, context->credentials,
		context->ref_source_func, context->ref_source_func_user_data,
		&context->out_certificate_pem, &context->out_certificate_errors,
		&context->out_discovered_sources, &context->out_calendar_user_addresses,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, local_error);
	} else {
		g_task_return_boolean (task, success);
	}
}

static gboolean
e_webdav_discover_setup_proxy_resolver (EWebDAVSession *webdav,
					ESource *cred_source,
					EWebDAVDiscoverRefSourceFunc ref_source_func,
					gpointer ref_source_func_user_data,
					GCancellable *cancellable,
					GError **error)
{
	ESourceAuthentication *auth_extension;
	ESource *source = NULL;
	gchar *uid;
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (cred_source), FALSE);

	if (!e_source_has_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION))
		return TRUE;

	auth_extension = e_source_get_extension (cred_source, E_SOURCE_EXTENSION_AUTHENTICATION);
	uid = e_source_authentication_dup_proxy_uid (auth_extension);

	if (uid != NULL) {
		if (ref_source_func) {
			source = ref_source_func (ref_source_func_user_data, uid);
		} else {
			ESourceRegistry *registry;

			registry = e_source_registry_new_sync (cancellable, error);
			if (!registry) {
				success = FALSE;
			} else {
				source = e_source_registry_ref_source (registry, uid);
				g_object_unref (registry);
			}
		}

		g_free (uid);
	}

	if (source != NULL) {
		GProxyResolver *proxy_resolver;

		proxy_resolver = G_PROXY_RESOLVER (source);
		if (g_proxy_resolver_is_supported (proxy_resolver))
			g_object_set (E_SOUP_SESSION (webdav), "proxy-resolver", proxy_resolver, NULL);

		g_object_unref (source);
	}

	return success;
}

/**
 * e_webdav_discover_sources:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (nullable): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (nullable): credentials to use for authentication to the server
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
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
	g_return_if_fail (E_IS_SOURCE (source));

	e_webdav_discover_sources_full (source, url_use_path, only_supports, credentials, NULL, NULL, cancellable, callback, user_data);
}

/**
 * e_webdav_discover_sources_full:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (nullable): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (nullable): credentials to use for authentication to the server
 * @ref_source_func: (nullable) (scope async): optional callback to use to get an ESource
 * @ref_source_func_user_data: (nullable): user data for @ref_source_func
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * This is the same as e_webdav_discover_sources(), it only allows to
 * provide a callback function (with its user_data), to reference an additional
 * #ESource. It's good to avoid creating its own #ESourceRegistry instance to
 * get it.
 *
 * When the operation is finished, @callback will be called. You can then
 * call e_webdav_discover_sources_finish() to get the result of the operation.
 *
 * Since: 3.30
 **/
void
e_webdav_discover_sources_full (ESource *source,
				const gchar *url_use_path,
				guint32 only_supports,
				const ENamedParameters *credentials,
				EWebDAVDiscoverRefSourceFunc ref_source_func,
				gpointer ref_source_func_user_data,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	EWebDAVDiscoverContext *context;
	GTask *task;

	g_return_if_fail (E_IS_SOURCE (source));

	context = e_webdav_discover_context_new (source, url_use_path, only_supports, credentials, ref_source_func, ref_source_func_user_data);

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
 * @out_certificate_pem: (out) (optional): optional return location
 *   for a server SSL certificate in PEM format, when the operation failed
 *   with an SSL error
 * @out_certificate_errors: (out) (optional): optional #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with SSL error
 * @out_discovered_sources: (out) (optional) (element-type EWebDAVDiscoveredSource): a #GSList
 *   of all discovered sources
 * @out_calendar_user_addresses: (out) (optional) (element-type utf8): a #GSList of
 *   all discovered mail addresses for calendar sources
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_webdav_discover_sources(). If an
 * error occurred, the function will set @error and return %FALSE. The function
 * can return success and no discovered sources, the same as it can return failure,
 * but still set some output arguments, like the certificate related output
 * arguments with G_TLS_ERROR_BAD_CERTIFICATE error.
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

/* Returns whether the target error contains an authentication error */
static gboolean
e_webdav_discover_maybe_replace_auth_error (GError **target,
					    GError **candidate)
{
	g_return_val_if_fail (target != NULL, FALSE);
	g_return_val_if_fail (candidate != NULL, FALSE);

	if (!g_error_matches (*target, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) &&
	    g_error_matches (*candidate, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
		g_clear_error (target);
		*target = *candidate;
		*candidate = NULL;

		return TRUE;
	}

	return g_error_matches (*target, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED);
}

/**
 * e_webdav_discover_sources_sync:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (nullable): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (nullable): credentials to use for authentication to the server
 * @out_certificate_pem: (out) (optional): optional return location
 *   for a server SSL certificate in PEM format, when the operation failed
 *   with an SSL error
 * @out_certificate_errors: (out) (optional): optional #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with SSL error
 * @out_discovered_sources: (out) (optional) (element-type EWebDAVDiscoveredSource): a #GSList
 *   of all discovered sources
 * @out_calendar_user_addresses: (out) (optional) (element-type utf8): a #GSList of
 *   all discovered mail addresses for calendar sources
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
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
 * arguments with G_TLS_ERROR_BAD_CERTIFICATE error.
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
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	return e_webdav_discover_sources_full_sync (source, url_use_path, only_supports, credentials, NULL, NULL,
		out_certificate_pem, out_certificate_errors, out_discovered_sources, out_calendar_user_addresses, cancellable, error);
}

static gint
e_webdav_discover_cmp_sources (gconstpointer ptr1,
			       gconstpointer ptr2)
{
	const EWebDAVDiscoveredSource *source1 = ptr1;
	const EWebDAVDiscoveredSource *source2 = ptr2;

	if (!source1 || !source2)
		return (source1 ? 1 : 0) - (source2 ? 1 : 0);

	if (source1->order != source2->order && source1->order != (guint) -1 && source2->order != (guint) -1)
		return source1->order < source2->order ? -1 : 1;

	return g_strcmp0 (source1->display_name, source2->display_name);
}

static gboolean
e_webdav_discover_is_fatal_error (const GError *error)
{
	return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
	       g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
	       g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED);
}

/**
 * e_webdav_discover_sources_full_sync:
 * @source: an #ESource from which to take connection details
 * @url_use_path: (nullable): optional URL override, or %NULL
 * @only_supports: bit-or of EWebDAVDiscoverSupports, to limit what type of sources to search
 * @credentials: (nullable): credentials to use for authentication to the server
 * @ref_source_func: (nullable) (scope call): optional callback to use to get an ESource
 * @ref_source_func_user_data: (nullable): user data for @ref_source_func
 * @out_certificate_pem: (out) (optional): optional return location
 *   for a server SSL certificate in PEM format, when the operation failed
 *   with an SSL error
 * @out_certificate_errors: (out) (optional): optional #GTlsCertificateFlags,
 *   with certificate error flags when the operation failed with SSL error
 * @out_discovered_sources: (out) (optional) (element-type EWebDAVDiscoveredSource): a #GSList
 *   of all discovered sources
 * @out_calendar_user_addresses: (out) (optional) (element-type utf8): a #GSList of
 *   all discovered mail addresses for calendar sources
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This is the same as e_webdav_discover_sources_sync(), it only allows to
 * provide a callback function (with its user_data), to reference an additional
 * #ESource. It's good to avoid creating its own #ESourceRegistry instance to
 * get it.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.30
 **/
gboolean
e_webdav_discover_sources_full_sync (ESource *source,
				     const gchar *url_use_path,
				     guint32 only_supports,
				     const ENamedParameters *credentials,
				     EWebDAVDiscoverRefSourceFunc ref_source_func,
				     gpointer ref_source_func_user_data,
				     gchar **out_certificate_pem,
				     GTlsCertificateFlags *out_certificate_errors,
				     GSList **out_discovered_sources,
				     GSList **out_calendar_user_addresses,
				     GCancellable *cancellable,
				     GError **error)
{
	ESourceWebdav *webdav_extension;
	EWebDAVSession *webdav;
	GUri *guri;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (url_use_path && (g_ascii_strncasecmp (url_use_path, "http://", 7) == 0 ||
	    g_ascii_strncasecmp (url_use_path, "https://", 8) == 0)) {
		guri = g_uri_parse (url_use_path, SOUP_HTTP_URI_FLAGS, NULL);
		url_use_path = NULL;
	} else {
		g_return_val_if_fail (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND), FALSE);

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		guri = e_source_webdav_dup_uri (webdav_extension);
	}

	g_return_val_if_fail (guri != NULL, FALSE);

	if (url_use_path) {
		GString *new_path;

		/* Absolute path overrides whole path, while relative path is only appended. */
		if (*url_use_path == '/') {
			new_path = g_string_new (url_use_path);
		} else {
			const gchar *current_path;

			current_path = g_uri_get_path (guri);
			new_path = g_string_new (current_path ? current_path : "");
			if (!new_path->len || new_path->str[new_path->len - 1] != '/')
				g_string_append_c (new_path, '/');
			g_string_append (new_path, url_use_path);
		}

		if (!new_path->len || new_path->str[new_path->len - 1] != '/')
			g_string_append_c (new_path, '/');

		e_util_change_uri_component (&guri, SOUP_URI_PATH, new_path->str);

		g_string_free (new_path, TRUE);
	}

	webdav = e_webdav_session_new (source);

	if (!e_webdav_discover_setup_proxy_resolver (webdav, source, ref_source_func, ref_source_func_user_data, cancellable, error)) {
		g_uri_unref (guri);
		g_object_unref (webdav);

		return FALSE;
	}

	e_soup_session_setup_logging (E_SOUP_SESSION (webdav), g_getenv ("WEBDAV_DEBUG"));
	e_soup_session_set_credentials (E_SOUP_SESSION (webdav), credentials);

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		WebDAVDiscoverData wdd;
		gchar *uri;
		gboolean fatal_error;
		GError *local_error = NULL;

		wdd.covered_hrefs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		wdd.addressbooks = NULL;
		wdd.calendars = NULL;
		wdd.only_supports = only_supports;
		wdd.out_calendar_user_addresses = out_calendar_user_addresses;
		wdd.cancellable = cancellable;
		wdd.error = &local_error;

		uri = g_uri_to_string_partial (guri, G_URI_HIDE_PASSWORD);

		success = uri && *uri && e_webdav_discover_propfind_uri_sync (webdav, &wdd, uri, FALSE);

		g_free (uri);

		fatal_error = e_webdav_discover_is_fatal_error (local_error);

		if (!fatal_error && !g_cancellable_is_cancelled (cancellable) && !wdd.calendars &&
		    ((only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
		    (only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_EVENTS | E_WEBDAV_DISCOVER_SUPPORTS_MEMOS | E_WEBDAV_DISCOVER_SUPPORTS_TASKS)) != 0) &&
		    (!g_uri_get_path (guri) || !strstr (g_uri_get_path (guri), "/.well-known/"))) {
			gchar *saved_path;
			GError *local_error_2nd = NULL;

			saved_path = g_strdup (g_uri_get_path (guri));

			e_util_change_uri_component (&guri, SOUP_URI_PATH, "/.well-known/caldav");

			uri = g_uri_to_string_partial (guri, SOUP_HTTP_URI_FLAGS);

			wdd.error = &local_error_2nd;
			wdd.only_supports = E_WEBDAV_DISCOVER_SUPPORTS_EVENTS | E_WEBDAV_DISCOVER_SUPPORTS_MEMOS | E_WEBDAV_DISCOVER_SUPPORTS_TASKS;
			g_hash_table_remove_all (wdd.covered_hrefs);

			success = (uri && *uri && e_webdav_discover_propfind_uri_sync (webdav, &wdd, uri, FALSE)) || success;

			g_free (uri);

			fatal_error = e_webdav_discover_is_fatal_error (local_error_2nd);

			e_util_change_uri_component (&guri, SOUP_URI_PATH, saved_path);

			g_free (saved_path);

			if (e_webdav_discover_maybe_replace_auth_error (&local_error, &local_error_2nd))
				success = FALSE;

			g_clear_error (&local_error_2nd);

			wdd.error = NULL;
		}

		if (!fatal_error && !g_cancellable_is_cancelled (cancellable) &&
		    ((only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
		    (only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_WEBDAV_NOTES)) != 0) &&
		    (!g_uri_get_path (guri) || !strstr (g_uri_get_path (guri), "/.well-known/"))) {
			gchar *saved_path;
			GError *local_error_2nd = NULL;

			saved_path = g_strdup (g_uri_get_path (guri));

			e_util_change_uri_component (&guri, SOUP_URI_PATH, "/.well-known/webdav/Notes/");

			uri = g_uri_to_string_partial (guri, G_URI_HIDE_PASSWORD);

			wdd.error = &local_error_2nd;
			wdd.only_supports = E_WEBDAV_DISCOVER_SUPPORTS_WEBDAV_NOTES;
			g_hash_table_remove_all (wdd.covered_hrefs);

			success = (uri && *uri && e_webdav_discover_propfind_uri_sync (webdav, &wdd, uri, FALSE)) || success;

			g_free (uri);

			fatal_error = e_webdav_discover_is_fatal_error (local_error_2nd);

			e_util_change_uri_component (&guri, SOUP_URI_PATH, saved_path);

			g_free (saved_path);

			if (e_webdav_discover_maybe_replace_auth_error (&local_error, &local_error_2nd))
				success = FALSE;

			g_clear_error (&local_error_2nd);

			wdd.error = NULL;
		}

		if (!fatal_error && !g_cancellable_is_cancelled (cancellable) && !wdd.addressbooks &&
		    ((only_supports & (~CUSTOM_SUPPORTS_FLAGS)) == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
		    (only_supports & (E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS)) != 0) &&
		    (!g_uri_get_path (guri) || !strstr (g_uri_get_path (guri), "/.well-known/"))) {
			gchar *saved_path;
			GError *local_error_2nd = NULL;

			saved_path = g_strdup (g_uri_get_path (guri));

			e_util_change_uri_component (&guri, SOUP_URI_PATH, "/.well-known/carddav");

			uri = g_uri_to_string_partial (guri, SOUP_HTTP_URI_FLAGS);

			wdd.error = &local_error_2nd;
			wdd.only_supports = E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS;
			g_hash_table_remove_all (wdd.covered_hrefs);

			success = (uri && *uri && e_webdav_discover_propfind_uri_sync (webdav, &wdd, uri, FALSE)) || success;

			g_free (uri);

			e_util_change_uri_component (&guri, SOUP_URI_PATH, saved_path);

			g_free (saved_path);

			fatal_error = e_webdav_discover_is_fatal_error (local_error_2nd);

			if (e_webdav_discover_maybe_replace_auth_error (&local_error, &local_error_2nd))
				success = FALSE;

			g_clear_error (&local_error_2nd);

			wdd.error = NULL;
		}

		if (wdd.calendars || wdd.addressbooks) {
			success = TRUE;
			fatal_error = FALSE;
			g_clear_error (&local_error);
		} else if (local_error) {
			success = FALSE;
			g_propagate_error (error, local_error);
		}

		if (out_discovered_sources && !fatal_error) {
			if (only_supports == E_WEBDAV_DISCOVER_SUPPORTS_NONE ||
			    (only_supports & E_WEBDAV_DISCOVER_SUPPORTS_CALENDAR_AUTO_SCHEDULE) != 0) {
				GSList *link;

				for (link = wdd.calendars; link && !fatal_error && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
					EWebDAVDiscoveredSource *discovered = link->data;
					GHashTable *allows = NULL, *capabilities = NULL;

					local_error = NULL;

					if (discovered && discovered->href &&
					    e_webdav_session_options_sync (webdav, discovered->href, &capabilities, &allows, cancellable, &local_error)) {
						if (capabilities && g_hash_table_contains (capabilities, E_WEBDAV_CAPABILITY_CALENDAR_AUTO_SCHEDULE))
							discovered->supports |= E_WEBDAV_DISCOVER_SUPPORTS_CALENDAR_AUTO_SCHEDULE;
					}

					fatal_error = e_webdav_discover_is_fatal_error (local_error);
					g_clear_error (&local_error);

					if (allows)
						g_hash_table_destroy (allows);

					if (capabilities)
						g_hash_table_destroy (capabilities);
				}
			}

			if (wdd.calendars)
				*out_discovered_sources = g_slist_concat (*out_discovered_sources, wdd.calendars);
			if (wdd.addressbooks)
				*out_discovered_sources = g_slist_concat (*out_discovered_sources, wdd.addressbooks);
		} else {
			e_webdav_discover_free_discovered_sources (wdd.calendars);
			e_webdav_discover_free_discovered_sources (wdd.addressbooks);
		}

		if (out_calendar_user_addresses && *out_calendar_user_addresses)
			*out_calendar_user_addresses = g_slist_reverse (*out_calendar_user_addresses);

		if (out_discovered_sources && *out_discovered_sources)
			*out_discovered_sources = g_slist_sort (*out_discovered_sources, e_webdav_discover_cmp_sources);

		g_hash_table_destroy (wdd.covered_hrefs);
	} else {
		success = FALSE;
	}

	if (!success)
		e_soup_session_get_ssl_error_details (E_SOUP_SESSION (webdav), out_certificate_pem, out_certificate_errors);

	g_uri_unref (guri);
	g_object_unref (webdav);

	return success;
}

/**
 * e_webdav_discovered_source_copy:
 * @discovered_source: an #EWebDAVDiscoveredSource to copy
 *
 * Copies the given EWebDAVDiscoveredSource.
 *
 * Returns: (transfer full): a copy of @discovered_source
 *
 * Since: 3.40
 **/
EWebDAVDiscoveredSource *
e_webdav_discovered_source_copy (EWebDAVDiscoveredSource *discovered_source)
{
	EWebDAVDiscoveredSource *copy;

	g_return_val_if_fail (discovered_source != NULL, NULL);

	copy = g_slice_new0 (EWebDAVDiscoveredSource);
	copy->href = g_strdup (discovered_source->href);
	copy->supports = discovered_source->supports;
	copy->display_name = g_strdup (discovered_source->display_name);
	copy->description = g_strdup (discovered_source->description);
	copy->color = g_strdup (discovered_source->color);
	copy->order = discovered_source->order;

	return copy;
}


/**
 * e_webdav_discovered_source_free:
 * @discovered_source: (nullable): an #EWebDAVDiscoveredSource to free
 *
 * Frees the @discovered_source. Function does nothing, when it's %NULL.
 *
 * Since: 3.40
 **/
void
e_webdav_discovered_source_free (EWebDAVDiscoveredSource *discovered_source)
{
	if (!discovered_source)
		return;

	g_clear_pointer (&discovered_source->href, g_free);
	g_clear_pointer (&discovered_source->display_name, g_free);
	g_clear_pointer (&discovered_source->description, g_free);
	g_clear_pointer (&discovered_source->color, g_free);
	g_slice_free (EWebDAVDiscoveredSource, discovered_source);
}

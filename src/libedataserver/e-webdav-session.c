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
 * for common requests/responses, on top of an #ESoupSession. It
 * supports also Access Control Protocol (RFC 3744).
 **/

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include "camel/camel.h"

#include "e-source-authentication.h"
#include "e-source-webdav.h"
#include "e-xml-utils.h"

#include "e-webdav-session.h"

#define BUFFER_SIZE 16384

struct _EWebDAVSessionPrivate {
	gchar *last_dav_error_code;
};

G_DEFINE_TYPE_WITH_PRIVATE (EWebDAVSession, e_webdav_session, E_TYPE_SOUP_SESSION)

G_DEFINE_BOXED_TYPE (EWebDAVResource, e_webdav_resource, e_webdav_resource_copy, e_webdav_resource_free)
G_DEFINE_BOXED_TYPE (EWebDAVPropertyChange, e_webdav_property_change, e_webdav_property_change_copy, e_webdav_property_change_free)
G_DEFINE_BOXED_TYPE (EWebDAVPrivilege, e_webdav_privilege, e_webdav_privilege_copy, e_webdav_privilege_free)
G_DEFINE_BOXED_TYPE (EWebDAVAccessControlEntry, e_webdav_access_control_entry, e_webdav_access_control_entry_copy, e_webdav_access_control_entry_free)

/**
 * e_webdav_resource_new:
 * @kind: an #EWebDAVResourceKind of the resource
 * @supports: bit-or of #EWebDAVResourceSupports values
 * @href: href of the resource
 * @etag: (nullable): optional ETag of the resource, or %NULL
 * @display_name: (nullable): optional display name of the resource, or %NULL
 * @content_type: (nullable): optional Content-Type of the resource, or %NULL
 * @content_length: optional Content-Length of the resource, or 0
 * @creation_date: optional date of creation of the resource, or 0
 * @last_modified: optional last modified time of the resource, or 0
 * @description: (nullable): optional description of the resource, or %NULL
 * @color: (nullable): optional color of the resource, or %NULL
 * @order: sort order of the resource, or (guint) -1
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
		       const gchar *color,
		       guint order)
{
	EWebDAVResource *resource;

	resource = g_slice_new0 (EWebDAVResource);
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
	resource->order = order;

	return resource;
}

/**
 * e_webdav_resource_copy:
 * @src: (nullable): an #EWebDAVResource to make a copy of
 *
 * Returns: (transfer full) (nullable): A new #EWebDAVResource prefilled with
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
		src->color,
		src->order);
}

/**
 * e_webdav_resource_free:
 * @ptr: (nullable): an #EWebDAVResource
 *
 * Frees an #EWebDAVResource previously created with e_webdav_resource_new()
 * or e_webdav_resource_copy(). The function does nothing, if @ptr is %NULL.
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
		g_slice_free (EWebDAVResource, resource);
	}
}

static EWebDAVPropertyChange *
e_webdav_property_change_new (EWebDAVPropertyChangeKind kind,
			      const gchar *ns_uri,
			      const gchar *name,
			      const gchar *value)
{
	EWebDAVPropertyChange *change;

	change = g_slice_new0 (EWebDAVPropertyChange);
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
 * Returns: (transfer full) (nullable): A new #EWebDAVPropertyChange prefilled
 *    with the same values as @src, or %NULL, when @src is %NULL.
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
 * The function does nothing, if @ptr is %NULL.
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
		g_slice_free (EWebDAVPropertyChange, change);
	}
}

/**
 * e_webdav_privilege_new:
 * @ns_uri: (nullable): a namespace URI
 * @name: (nullable): element name
 * @description: (nullable): human read-able description, or %NULL
 * @kind: an #EWebDAVPrivilegeKind
 * @hint: an #EWebDAVPrivilegeHint
 *
 * Describes one privilege entry. The @hint can be %E_WEBDAV_PRIVILEGE_HINT_UNKNOWN
 * for privileges which are not known to the #EWebDAVSession. It's possible, because
 * the servers can define their own privileges. The hint is also tried to pair with
 * known hints when it's %E_WEBDAV_PRIVILEGE_HINT_UNKNOWN.
 *
 * The @ns_uri and @name can be %NULL only if the @hint is one of the known
 * privileges. Otherwise it's an error to pass either of the two as %NULL.
 *
 * Returns: (transfer full): A newly created #EWebDAVPrivilege, prefilled with
 *    given values. Free it with e_webdav_privilege_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVPrivilege *
e_webdav_privilege_new (const gchar *ns_uri,
			const gchar *name,
			const gchar *description,
			EWebDAVPrivilegeKind kind,
			EWebDAVPrivilegeHint hint)
{
	EWebDAVPrivilege *privilege;

	if ((!ns_uri || !name) && hint != E_WEBDAV_PRIVILEGE_HINT_UNKNOWN) {
		const gchar *use_ns_uri = NULL, *use_name = NULL;

		switch (hint) {
		case E_WEBDAV_PRIVILEGE_HINT_UNKNOWN:
			break;
		case E_WEBDAV_PRIVILEGE_HINT_READ:
			use_name = "read";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_WRITE:
			use_name = "write";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_WRITE_PROPERTIES:
			use_name = "write-properties";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_WRITE_CONTENT:
			use_name = "write-content";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_UNLOCK:
			use_name = "unlock";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_READ_ACL:
			use_name = "read-acl";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_WRITE_ACL:
			use_name = "write-acl";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_READ_CURRENT_USER_PRIVILEGE_SET:
			use_name = "read-current-user-privilege-set";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_BIND:
			use_name = "bind";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_UNBIND:
			use_name = "unbind";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_ALL:
			use_name = "all";
			break;
		case E_WEBDAV_PRIVILEGE_HINT_CALDAV_READ_FREE_BUSY:
			use_ns_uri = E_WEBDAV_NS_CALDAV;
			use_name = "read-free-busy";
			break;
		}

		if (use_name) {
			ns_uri = use_ns_uri ? use_ns_uri : E_WEBDAV_NS_DAV;
			name = use_name;
		}
	}

	g_return_val_if_fail (ns_uri != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (hint == E_WEBDAV_PRIVILEGE_HINT_UNKNOWN) {
		if (g_str_equal (ns_uri, E_WEBDAV_NS_DAV)) {
			if (g_str_equal (name, "read"))
				hint = E_WEBDAV_PRIVILEGE_HINT_READ;
			else if (g_str_equal (name, "write"))
				hint = E_WEBDAV_PRIVILEGE_HINT_WRITE;
			else if (g_str_equal (name, "write-properties"))
				hint = E_WEBDAV_PRIVILEGE_HINT_WRITE_PROPERTIES;
			else if (g_str_equal (name, "write-content"))
				hint = E_WEBDAV_PRIVILEGE_HINT_WRITE_CONTENT;
			else if (g_str_equal (name, "unlock"))
				hint = E_WEBDAV_PRIVILEGE_HINT_UNLOCK;
			else if (g_str_equal (name, "read-acl"))
				hint = E_WEBDAV_PRIVILEGE_HINT_READ_ACL;
			else if (g_str_equal (name, "write-acl"))
				hint = E_WEBDAV_PRIVILEGE_HINT_WRITE_ACL;
			else if (g_str_equal (name, "read-current-user-privilege-set"))
				hint = E_WEBDAV_PRIVILEGE_HINT_READ_CURRENT_USER_PRIVILEGE_SET;
			else if (g_str_equal (name, "bind"))
				hint = E_WEBDAV_PRIVILEGE_HINT_BIND;
			else if (g_str_equal (name, "unbind"))
				hint = E_WEBDAV_PRIVILEGE_HINT_UNBIND;
			else if (g_str_equal (name, "all"))
				hint = E_WEBDAV_PRIVILEGE_HINT_ALL;
		} else if (g_str_equal (ns_uri, E_WEBDAV_NS_CALDAV)) {
			if (g_str_equal (name, "read-free-busy"))
				hint = E_WEBDAV_PRIVILEGE_HINT_CALDAV_READ_FREE_BUSY;
		}
	}

	privilege = g_slice_new (EWebDAVPrivilege);
	privilege->ns_uri = g_strdup (ns_uri);
	privilege->name = g_strdup (name);
	privilege->description = g_strdup (description);
	privilege->kind = kind;
	privilege->hint = hint;

	return privilege;
}

/**
 * e_webdav_privilege_copy:
 * @src: (nullable): an #EWebDAVPrivilege to make a copy of
 *
 * Returns: (transfer full) (nullable): A new #EWebDAVPrivilege prefilled with
 *    the same values as @src, or %NULL, when @src is %NULL.
 *    Free it with e_webdav_privilege_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVPrivilege *
e_webdav_privilege_copy (const EWebDAVPrivilege *src)
{
	if (!src)
		return NULL;

	return e_webdav_privilege_new (
		src->ns_uri,
		src->name,
		src->description,
		src->kind,
		src->hint);
}

/**
 * e_webdav_privilege_free:
 * @ptr: (nullable): an #EWebDAVPrivilege
 *
 * Frees an #EWebDAVPrivilege previously created with e_webdav_privilege_new()
 * or e_webdav_privilege_copy(). The function does nothing, if @ptr is %NULL.
 *
 * Since: 3.26
 **/
void
e_webdav_privilege_free (gpointer ptr)
{
	EWebDAVPrivilege *privilege = ptr;

	if (privilege) {
		g_free (privilege->ns_uri);
		g_free (privilege->name);
		g_free (privilege->description);
		g_slice_free (EWebDAVPrivilege, privilege);
	}
}

/**
 * e_webdav_access_control_entry_new:
 * @principal_kind: an #EWebDAVACEPrincipalKind
 * @principal_href: (nullable): principal href; should be set only if @principal_kind is @E_WEBDAV_ACE_PRINCIPAL_HREF
 * @flags: bit-or of #EWebDAVACEFlag values
 * @inherited_href: (nullable): href of the resource from which inherits; should be set only if @flags contain E_WEBDAV_ACE_FLAG_INHERITED
 *
 * Describes one Access Control Entry (ACE).
 *
 * The @flags should always contain either %E_WEBDAV_ACE_FLAG_GRANT or
 * %E_WEBDAV_ACE_FLAG_DENY value.
 *
 * Use e_webdav_access_control_entry_append_privilege() to add respective
 * privileges to the entry.
 *
 * Returns: (transfer full): A newly created #EWebDAVAccessControlEntry, prefilled with
 *    given values. Free it with e_webdav_access_control_entry_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVAccessControlEntry *
e_webdav_access_control_entry_new (EWebDAVACEPrincipalKind principal_kind,
				   const gchar *principal_href,
				   guint32 flags,
				   const gchar *inherited_href)
{
	EWebDAVAccessControlEntry *ace;

	if (principal_kind == E_WEBDAV_ACE_PRINCIPAL_HREF)
		g_return_val_if_fail (principal_href != NULL, NULL);
	else
		g_return_val_if_fail (principal_href == NULL, NULL);

	if ((flags & E_WEBDAV_ACE_FLAG_INHERITED) != 0)
		g_return_val_if_fail (inherited_href != NULL, NULL);
	else
		g_return_val_if_fail (inherited_href == NULL, NULL);

	ace = g_slice_new0 (EWebDAVAccessControlEntry);
	ace->principal_kind = principal_kind;
	ace->principal_href = g_strdup (principal_href);
	ace->flags = flags;
	ace->inherited_href = g_strdup (inherited_href);
	ace->privileges = NULL;

	return ace;
}

/**
 * e_webdav_access_control_entry_copy:
 * @src: (nullable): an #EWebDAVAccessControlEntry to make a copy of
 *
 * Returns: (transfer full) (nullable): A new #EWebDAVAccessControlEntry
 *    prefilled with the same values as @src, or %NULL, when @src is %NULL.
 *    Free it with e_webdav_access_control_entry_free(), when no longer needed.
 *
 * Since: 3.26
 **/
EWebDAVAccessControlEntry *
e_webdav_access_control_entry_copy (const EWebDAVAccessControlEntry *src)
{
	EWebDAVAccessControlEntry *ace;
	GSList *link;

	if (!src)
		return NULL;

	ace = e_webdav_access_control_entry_new (
		src->principal_kind,
		src->principal_href,
		src->flags,
		src->inherited_href);
	if (!ace)
		return NULL;

	for (link = src->privileges; link; link = g_slist_next (link)) {
		EWebDAVPrivilege *privilege = link->data;

		if (privilege)
			ace->privileges = g_slist_prepend (ace->privileges, e_webdav_privilege_copy (privilege));
	}

	ace->privileges = g_slist_reverse (ace->privileges);

	return ace;
}

/**
 * e_webdav_access_control_entry_free:
 * @ptr: (nullable): an #EWebDAVAccessControlEntry
 *
 * Frees an #EWebDAVAccessControlEntry previously created with e_webdav_access_control_entry_new()
 * or e_webdav_access_control_entry_copy(). The function does nothing, if @ptr is %NULL.
 *
 * Since: 3.26
 **/
void
e_webdav_access_control_entry_free (gpointer ptr)
{
	EWebDAVAccessControlEntry *ace = ptr;

	if (ace) {
		g_free (ace->principal_href);
		g_free (ace->inherited_href);
		g_slist_free_full (ace->privileges, e_webdav_privilege_free);
		g_slice_free (EWebDAVAccessControlEntry, ace);
	}
}

/**
 * e_webdav_access_control_entry_append_privilege:
 * @ace: an #EWebDAVAccessControlEntry
 * @privilege: (transfer full): an #EWebDAVPrivilege
 *
 * Appends a new @privilege to the list of privileges for the @ace.
 * The function assumes ownership of the @privilege, which is freed
 * together with the @ace.
 *
 * Since: 3.26
 **/
void
e_webdav_access_control_entry_append_privilege (EWebDAVAccessControlEntry *ace,
						EWebDAVPrivilege *privilege)
{
	g_return_if_fail (ace != NULL);
	g_return_if_fail (privilege != NULL);

	ace->privileges = g_slist_append (ace->privileges, privilege);
}

/**
 * e_webdav_access_control_entry_get_privileges:
 * @ace: an #EWebDAVAccessControlEntry
 *
 * Returns: (element-type EWebDAVPrivilege) (transfer none): A #GSList of #EWebDAVPrivilege
 *    with the list of privileges for the @ace. The reurned #GSList, together with its data
 *    is owned by the @ace.
 *
 * Since: 3.26
 **/
GSList *
e_webdav_access_control_entry_get_privileges (EWebDAVAccessControlEntry *ace)
{
	g_return_val_if_fail (ace != NULL, NULL);

	return ace->privileges;
}

static void
e_webdav_session_finalize (GObject *object)
{
	EWebDAVSession *webdav = E_WEBDAV_SESSION (object);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_webdav_session_parent_class)->finalize (object);
}

static void
e_webdav_session_class_init (EWebDAVSessionClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_webdav_session_finalize;
}

static void
e_webdav_session_init (EWebDAVSession *webdav)
{
	webdav->priv = e_webdav_session_get_instance_private (webdav);
}

/**
 * e_webdav_session_new:
 * @source: an #ESource
 *
 * Creates a new #EWebDAVSession associated with given @source.
 * The #EWebDAVSession uses an #ESourceWebdav extension on certain
 * places when it's defined for the @source.
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

	return g_object_new (E_TYPE_WEBDAV_SESSION,
		"source", source,
		NULL);
}

/**
 * e_webdav_session_get_last_dav_error_code:
 * @webdav: an #EWebDAVSession
 *
 * Returns last DAV error code as returned by the server. Each recognized code
 * is enclosed in "[]" in the returned string, to be able to distinguish between
 * them, in case the server returned multiple codes.
 *
 * The string is valid until the next request is executed.
 *
 * Returns: (transfer none) (nullable): a DAV error from the last request,
 *    or %NULL, when no error had been recognized.
 *
 * Since: 3.36
 **/
const gchar *
e_webdav_session_get_last_dav_error_code (EWebDAVSession *webdav)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), NULL);

	return webdav->priv->last_dav_error_code;
}

/**
 * e_webdav_session_get_last_dav_error_is_permission:
 * @webdav: an #EWebDAVSession
 *
 * Returns: whether the last recognized DAV error code contains an error
 *    which means that user doesn't have permission for the operation. If there
 *    is no DAV error stored, then returns %FALSE.
 *
 * Since: 3.36
 **/
gboolean
e_webdav_session_get_last_dav_error_is_permission (EWebDAVSession *webdav)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);

	return webdav->priv->last_dav_error_code &&
		strstr (webdav->priv->last_dav_error_code, "[need-privileges]");
}

/**
 * e_webdav_session_new_message:
 * @webdav: an #EWebDAVSession
 * @method: an HTTP method
 * @uri: (nullable): URI to create the request for, or %NULL to read from #ESource
 * @error: return location for a #GError, or %NULL
 *
 * Returns: (transfer full): A new #SoupMessage for the given @uri, or, when %NULL,
 *    for the URI stored in the associated #ESource. Free the returned structure
 *    with g_object_unref(), when no longer needed.
 *
 * Since: 3.26
 **/
SoupMessage *
e_webdav_session_new_message (EWebDAVSession *webdav,
			      const gchar *method,
			      const gchar *uri,
			      GError **error)
{
	ESoupSession *session;
	SoupMessage *message;
	GUri *guri;
	ESource *source;
	ESourceWebdav *webdav_extension;
	const gchar *path;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), NULL);

	session = E_SOUP_SESSION (webdav);
	if (uri && *uri)
		return e_soup_session_new_message (session, method, uri, error);

	source = e_soup_session_get_source (session);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			_("Cannot determine destination URL without WebDAV extension"));
		return NULL;
	}

	webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	guri = e_source_webdav_dup_uri (webdav_extension);

	g_return_val_if_fail (guri != NULL, NULL);

	/* The URI in the ESource should be to a collection, with an ending
	   forward slash, thus ensure it's there. */
	path = g_uri_get_path (guri);
	if (!path || !*path || !g_str_has_suffix (path, "/")) {
		gchar *new_path;

		new_path = g_strconcat (path ? path : "", "/", NULL);
		e_util_change_uri_component (&guri, SOUP_URI_PATH, new_path);
		g_free (new_path);
	}

	message = e_soup_session_new_message_from_uri (session, method, guri, error);

	g_uri_unref (guri);

	return message;
}

static gboolean
e_webdav_session_extract_propstat_error_cb (EWebDAVSession *webdav,
					    xmlNodePtr prop_node,
					    const GUri *request_uri,
					    const gchar *href,
					    guint status_code,
					    gpointer user_data)
{
	GError **error = user_data;

	g_return_val_if_fail (error != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK && (
	    status_code != SOUP_STATUS_FAILED_DEPENDENCY ||
	    !*error)) {
		xmlNodePtr parent;
		const xmlChar *description = NULL;

		parent = prop_node->parent;
		if (parent) {
			description = e_xml_find_child_and_get_text (parent, E_WEBDAV_NS_DAV, "responsedescription");

			if (!description || !*description) {
				description = NULL;
				parent = parent->parent;
				if (parent) {
					description = e_xml_find_child_and_get_text (parent, E_WEBDAV_NS_DAV, "responsedescription");

					if (!description || !*description)
						description = NULL;
				}
			}
		}

		g_clear_error (error);
		g_set_error_literal (error, E_SOUP_SESSION_ERROR, status_code,
			e_soup_session_util_status_to_string (status_code, (const gchar *) description));
	}

	return TRUE;
}

static gboolean
e_webdav_session_extract_dav_error (EWebDAVSession *webdav,
				    xmlXPathContextPtr xpath_ctx,
				    const gchar *xpath_prefix,
				    gchar **out_detail_text,
				    gboolean can_change_last_dav_error_code)
{
	xmlXPathObjectPtr xpath_obj;
	gchar *detail_text;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xpath_ctx != NULL, FALSE);
	g_return_val_if_fail (xpath_prefix != NULL, FALSE);
	g_return_val_if_fail (out_detail_text != NULL, FALSE);

	if (!e_xml_xpath_eval_exists (xpath_ctx, "%s/D:error", xpath_prefix))
		return FALSE;

	detail_text = e_xml_xpath_eval_as_string (xpath_ctx, "%s/D:error", xpath_prefix);

	xpath_obj = e_xml_xpath_eval (xpath_ctx, "%s/D:error", xpath_prefix);
	if (xpath_obj) {
		if (xpath_obj->type == XPATH_NODESET &&
		    xpath_obj->nodesetval &&
		    xpath_obj->nodesetval->nodeNr == 1 &&
		    xpath_obj->nodesetval->nodeTab &&
		    xpath_obj->nodesetval->nodeTab[0] &&
		    xpath_obj->nodesetval->nodeTab[0]->children) {
			GString *text = g_string_new ("");
			xmlNodePtr node;

			for (node = xpath_obj->nodesetval->nodeTab[0]->children; node; node = node->next) {
				if (node->type == XML_ELEMENT_NODE &&
				    node->name && *(node->name)) {
					g_string_append_printf (text, "[%s]", (const gchar *) node->name);
				}
			}

			if (text->len > 0) {
				if (can_change_last_dav_error_code) {
					g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);
					webdav->priv->last_dav_error_code = g_strdup (text->str);
				}

				if (detail_text) {
					g_strstrip (detail_text);
					if (*detail_text)
						g_string_prepend (text, detail_text);
					g_free (detail_text);
				}

				detail_text = g_string_free (text, FALSE);
			} else {
				g_string_free (text, TRUE);
			}
		}

		xmlXPathFreeObject (xpath_obj);
	}

	*out_detail_text = detail_text;

	return detail_text != NULL;
}

static gboolean
e_webdav_session_replace_with_detailed_error_internal (EWebDAVSession *webdav,
						       SoupMessage *message,
						       const GByteArray *response_data,
						       gboolean ignore_multistatus,
						       const gchar *prefix,
						       GError **inout_error,
						       gboolean can_change_last_dav_error_code,
						       gboolean skip_text_on_success)
{
	GByteArray byte_array = { 0 };
	const gchar *content_type, *reason_phrase;
	gchar *detail_text = NULL;
	gchar *reason_phrase_copy = NULL;
	gboolean error_set = FALSE;
	guint status_code;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (SOUP_IS_MESSAGE (message), FALSE);

	status_code = soup_message_get_status (message);
	reason_phrase = soup_message_get_reason_phrase (message);
	byte_array.data = NULL;
	byte_array.len = 0;

	if (response_data && response_data->len) {
		byte_array.data = (gpointer) response_data->data;
		byte_array.len = response_data->len;
	} else {
		GByteArray *bytes = e_soup_session_util_get_message_bytes (message);

		if (bytes) {
			byte_array.data = bytes->data;
			byte_array.len = bytes->len;
		}
	}

	if (!byte_array.data || !byte_array.len)
		goto out;

	if (status_code == SOUP_STATUS_MULTI_STATUS &&
	    !ignore_multistatus &&
	    !e_webdav_session_traverse_multistatus_response (webdav, message, &byte_array,
		e_webdav_session_extract_propstat_error_cb, &local_error, NULL)) {
		g_clear_error (&local_error);
	}

	if (local_error) {
		if (prefix)
			g_prefix_error (&local_error, "%s: ", prefix);
		g_propagate_error (inout_error, local_error);

		return TRUE;
	}

	content_type = soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL);
	if (content_type && (!skip_text_on_success || (status_code && !SOUP_STATUS_IS_SUCCESSFUL (status_code))) && (
	    (g_ascii_strcasecmp (content_type, "application/xml") == 0 ||
	     g_ascii_strcasecmp (content_type, "text/xml") == 0))) {
		xmlDocPtr doc;

		if (status_code == SOUP_STATUS_MULTI_STATUS && ignore_multistatus)
			doc = NULL;
		else
			doc = e_xml_parse_data (byte_array.data, byte_array.len);

		if (doc) {
			xmlXPathContextPtr xpath_ctx;

			xpath_ctx = e_xml_new_xpath_context_with_namespaces (doc,
				"D", E_WEBDAV_NS_DAV,
				"C", E_WEBDAV_NS_CALDAV,
				"A", E_WEBDAV_NS_CARDDAV,
				NULL);

			if (xpath_ctx &&
			    e_webdav_session_extract_dav_error (webdav, xpath_ctx, "", &detail_text, can_change_last_dav_error_code)) {
				/* do nothing, detail_text is set */
			} else if (xpath_ctx) {
				const gchar *path_prefix = NULL;

				if (e_xml_xpath_eval_exists (xpath_ctx, "/D:multistatus/D:response/D:status"))
					path_prefix = "/D:multistatus/D:response";
				else if (e_xml_xpath_eval_exists (xpath_ctx, "/C:mkcalendar-response/D:status"))
					path_prefix = "/C:mkcalendar-response";
				else if (e_xml_xpath_eval_exists (xpath_ctx, "/D:mkcol-response/D:status"))
					path_prefix = "/D:mkcol-response";

				if (path_prefix) {
					guint parsed_status = 0;
					gchar *status;

					status = e_xml_xpath_eval_as_string (xpath_ctx, "%s/D:status", path_prefix);
					if (status && soup_headers_parse_status_line (status, NULL, &parsed_status, &reason_phrase_copy) &&
					    !SOUP_STATUS_IS_SUCCESSFUL (parsed_status)) {
						status_code = parsed_status;
						reason_phrase = reason_phrase_copy;
						detail_text = e_xml_xpath_eval_as_string (xpath_ctx, "%s/D:responsedescription", path_prefix);

						if (!detail_text)
							e_webdav_session_extract_dav_error (webdav, xpath_ctx, path_prefix, &detail_text, can_change_last_dav_error_code);
					} else {
						e_webdav_session_extract_dav_error (webdav, xpath_ctx, path_prefix, &detail_text, can_change_last_dav_error_code);
					}

					g_free (status);
				}
			}

			if (xpath_ctx)
				xmlXPathFreeContext (xpath_ctx);
			xmlFreeDoc (doc);
		}
	} else if (content_type && (!skip_text_on_success || (status_code && !SOUP_STATUS_IS_SUCCESSFUL (status_code))) &&
	     g_ascii_strcasecmp (content_type, "text/plain") == 0) {
		detail_text = g_strndup ((const gchar *) byte_array.data, byte_array.len);
	} else if (content_type && (!skip_text_on_success || (status_code && !SOUP_STATUS_IS_SUCCESSFUL (status_code))) &&
	     g_ascii_strcasecmp (content_type, "text/html") == 0) {
		GUri *g_uri;
		gchar *uri = NULL;

		g_uri = soup_message_get_uri (message);
		if (g_uri) {
			uri = g_uri_to_string_partial (g_uri, G_URI_HIDE_PASSWORD);
		}

		if (uri && *uri)
			detail_text = g_strdup_printf (_("The server responded with an HTML page, which can mean there’s an error on the server or with the client request. The used URI was: %s"), uri);
		else
			detail_text = g_strdup_printf (_("The server responded with an HTML page, which can mean there’s an error on the server or with the client request."));

		g_free (uri);
	}

 out:
	if (detail_text)
		g_strstrip (detail_text);

	if (detail_text && *detail_text) {
		error_set = TRUE;

		g_clear_error (inout_error);

		if (prefix) {
			g_set_error (inout_error, E_SOUP_SESSION_ERROR, status_code,
				/* Translators: The first '%s' is replaced with error prefix, as provided
				   by the caller, which can be in a form: "Failed with something".
				   The '%d' is replaced with actual HTTP status code.
				   The second '%s' is replaced with a reason phrase of the error (user readable text).
				   The last '%s' is replaced with detailed error text, as returned by the server. */
				_("%s: HTTP error code %d (%s): %s"), prefix, status_code,
				e_soup_session_util_status_to_string (status_code, reason_phrase),
				detail_text);
		} else {
			g_set_error (inout_error, E_SOUP_SESSION_ERROR, status_code,
				/* Translators: The '%d' is replaced with actual HTTP status code.
				   The '%s' is replaced with a reason phrase of the error (user readable text).
				   The last '%s' is replaced with detailed error text, as returned by the server. */
				_("Failed with HTTP error code %d (%s): %s"), status_code,
				e_soup_session_util_status_to_string (status_code, reason_phrase),
				detail_text);
		}
	} else if (status_code && !SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		error_set = TRUE;

		g_clear_error (inout_error);

		if (prefix) {
			g_set_error (inout_error, E_SOUP_SESSION_ERROR, status_code,
				/* Translators: The first '%s' is replaced with error prefix, as provided
				   by the caller, which can be in a form: "Failed with something".
				   The '%d' is replaced with actual HTTP status code.
				   The second '%s' is replaced with a reason phrase of the error (user readable text). */
				_("%s: HTTP error code %d (%s)"), prefix, status_code,
				e_soup_session_util_status_to_string (status_code, reason_phrase));
		} else {
			g_set_error (inout_error, E_SOUP_SESSION_ERROR, status_code,
				/* Translators: The '%d' is replaced with actual HTTP status code.
				   The '%s' is replaced with a reason phrase of the error (user readable text). */
				_("Failed with HTTP error code %d (%s)"), status_code,
				e_soup_session_util_status_to_string (status_code, reason_phrase));
		}
	}

	g_free (reason_phrase_copy);
	g_free (detail_text);

	return error_set;
}

/**
 * e_webdav_session_replace_with_detailed_error:
 * @webdav: an #EWebDAVSession
 * @message: a #SoupMessage
 * @response_data: (nullable): received response data, or %NULL
 * @ignore_multistatus: whether to ignore multistatus responses
 * @prefix: (nullable): error message prefix, used when replacing, or %NULL
 * @inout_error: (inout) (nullable) (transfer full): a #GError variable to replace content to, or %NULL
 *
 * Tries to read detailed error information from @response_data,
 * if not provided, then from @message's response_body. If the detailed
 * error cannot be found, then does nothing, otherwise frees the content
 * of @inout_error, if any, and then populates it with an error message
 * prefixed with @prefix.
 *
 * The @prefix might be of form "Failed to something", because the resulting
 * error message will be:
 * "Failed to something: HTTP error code XXX (reason_phrase): detailed_error".
 * When @prefix is %NULL, the error message will be:
 * "Failed with HTTP error code XXX (reason phrase): detailed_error".
 *
 * As the caller might not be interested in errors, also the @inout_error
 * can be %NULL, in which case the function does nothing.
 *
 * Returns: Whether any detailed error had been recognized.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_replace_with_detailed_error (EWebDAVSession *webdav,
					      SoupMessage *message,
					      const GByteArray *response_data,
					      gboolean ignore_multistatus,
					      const gchar *prefix,
					      GError **inout_error)
{
	return e_webdav_session_replace_with_detailed_error_internal (webdav, message, response_data, ignore_multistatus, prefix, inout_error, FALSE, FALSE);
}

/**
 * e_webdav_session_ensure_full_uri:
 * @webdav: an #EWebDAVSession
 * @request_uri: (nullable): a #GUri to which the @href belongs, or %NULL
 * @href: a possibly path-only href
 *
 * Converts possibly path-only @href into a full URI under the @request_uri.
 * When the @request_uri is %NULL, the URI defined in associated #ESource is
 * used instead, taken from the #ESourceWebdav extension, if defined.
 *
 * Free the returned pointer with g_free(), when no longer needed.
 *
 * Returns: (transfer full): The @href as a full URI
 *
 * Since: 3.24
 **/
gchar *
e_webdav_session_ensure_full_uri (EWebDAVSession *webdav,
				  const GUri *request_uri,
				  const gchar *href)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), NULL);
	g_return_val_if_fail (href != NULL, NULL);

	if (*href == '/' || !strstr (href, "://")) {
		GUri *guri;
		gchar *full_uri;

		if (request_uri) {
			guri = g_uri_ref ((GUri *) request_uri);
		} else {
			ESource *source;
			ESourceWebdav *webdav_extension;

			source = e_soup_session_get_source (E_SOUP_SESSION (webdav));
			g_return_val_if_fail (E_IS_SOURCE (source), NULL);

			if (!e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND))
				return g_strdup (href);

			webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			guri = e_source_webdav_dup_uri (webdav_extension);
		}

		g_return_val_if_fail (guri != NULL, NULL);

		e_util_change_uri_component (&guri, SOUP_URI_PATH, href);
		full_uri = g_uri_to_string_partial (guri, G_URI_HIDE_PASSWORD | G_URI_HIDE_USERINFO);

		g_uri_unref (guri);

		return full_uri;
	}

	return g_strdup (href);
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

	if (!g_hash_table_size (result))
		g_clear_pointer (&result, g_hash_table_destroy);

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
 * are defined as %SOUP_METHOD_OPTIONS, and so on. The 'value' of the #GHashTable
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
	SoupMessage *message;
	GByteArray *bytes;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_capabilities != NULL, FALSE);
	g_return_val_if_fail (out_allows != NULL, FALSE);

	*out_capabilities = NULL;
	*out_allows = NULL;

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_OPTIONS, uri, error);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	if (!bytes) {
		g_object_unref (message);
		return FALSE;
	}

	g_byte_array_free (bytes, TRUE);

	g_return_val_if_fail (message != NULL, FALSE);

	*out_capabilities = e_webdav_session_comma_header_to_hashtable (soup_message_get_response_headers (message), "DAV");
	*out_allows = e_webdav_session_comma_header_to_hashtable (soup_message_get_response_headers (message), "Allow");

	g_object_unref (message);

	return TRUE;
}

static void
e_webdav_session_copy_request_headers (SoupMessage *message,
				       SoupMessageHeaders *headers)
{
	SoupMessageHeaders *request_headers;
	SoupMessageHeadersIter iter;
	const gchar *name, *value;

	if (!headers)
		return;

	request_headers = soup_message_get_request_headers (message);

	soup_message_headers_iter_init (&iter, headers);

	while (soup_message_headers_iter_next (&iter, &name, &value)) {
		soup_message_headers_replace (request_headers, name, value);
	}
}

/**
 * e_webdav_session_post_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @data: data to post to the server
 * @data_length: length of @data, or -1, when @data is NUL-terminated
 * @in_content_type: (nullable): a Content-Type of the @data, or %NULL, to use application/xml
 * @in_headers: (nullable): additional #SoupMessageHeaders to be added to the request, or %NULL
 * @out_content_type: (out) (nullable) (transfer full): return location for response Content-Type, or %NULL
 * @out_headers: (out) (optional) (transfer full): optional return location for response #SoupMessageHeaders, or %NULL
 * @out_content: (out) (nullable) (transfer full): return location for response content, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues POST request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource.
 *
 * The optional @in_headers can contain additional headers to be added to the request.
 * These headers replace any existing in the request headers, without support for the list-values headers.
 *
 * The optional @out_content_type can be used to get content type of the response.
 * Free it with g_free(), when no longer needed.
 *
 * The optional @out_headers contains response headers. Free it with soup_message_headers_free(),
 * when no longer needed.
 *
 * The optional @out_content can be used to get actual result content. Free it
 * with g_byte_array_free(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.32
 **/
gboolean
e_webdav_session_post_sync (EWebDAVSession *webdav,
			    const gchar *uri,
			    const gchar *data,
			    gsize data_length,
			    const gchar *in_content_type,
			    SoupMessageHeaders *in_headers,
			    gchar **out_content_type,
			    SoupMessageHeaders **out_headers,
			    GByteArray **out_content,
			    GCancellable *cancellable,
			    GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	if (out_content_type)
		*out_content_type = NULL;

	if (out_headers)
		*out_headers = NULL;

	if (out_content)
		*out_content = NULL;

	if (data_length == (gsize) -1)
		data_length = strlen (data);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_POST, uri, error);
	if (!message)
		return FALSE;

	e_webdav_session_copy_request_headers (message, in_headers);

	e_soup_session_util_set_message_request_body_from_data (message, FALSE,
		(in_content_type && *in_content_type) ? in_content_type : E_WEBDAV_CONTENT_TYPE_XML,
		data, data_length, NULL);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, TRUE, _("Failed to post data"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (success) {
		if (out_content_type) {
			*out_content_type = g_strdup (soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL));
		}

		if (out_headers)
			*out_headers = g_boxed_copy (SOUP_TYPE_MESSAGE_HEADERS, soup_message_get_response_headers (message));

		if (out_content) {
			*out_content = bytes;
			bytes = NULL;
		}
	}

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

static gboolean
e_webdav_session_propfind_internal_sync (EWebDAVSession *webdav,
					 const gchar *uri,
					 const gchar *depth,
					 const EXmlDocument *xml,
					 EWebDAVPropstatTraverseFunc func,
					 gpointer func_user_data,
					 SoupMessage **out_message,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);
	if (xml)
		g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	if (out_message)
		*out_message = NULL;

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_PROPFIND, uri, error);
	if (!message)
		return FALSE;

	soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", depth);

	if (xml) {
		gchar *content;
		gsize content_length;

		content = e_xml_document_get_content (xml, &content_length);
		if (!content) {
			g_object_unref (message);

			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

			return FALSE;
		}

		e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
			content, content_length, g_free);
	}

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, TRUE, _("Failed to get properties"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (success)
		success = e_webdav_session_traverse_multistatus_response (webdav, message, bytes, func, func_user_data, error);

	if (bytes)
		g_byte_array_free (bytes, TRUE);

	if (success && out_message)
		*out_message = message;
	else
		g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_propfind_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @depth: requested depth, can be one of %E_WEBDAV_DEPTH_THIS, %E_WEBDAV_DEPTH_THIS_AND_CHILDREN or %E_WEBDAV_DEPTH_INFINITY
 * @xml: (nullable): the request itself, as an #EXmlDocument, the root element should be DAV:propfind, or %NULL
 * @func: (scope call) (closure func_user_data): an #EWebDAVPropstatTraverseFunc function to call for each DAV:propstat in the multistatus response
 * @func_user_data: user data passed to @func
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues PROPFIND request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource. On success, calls @func for each returned
 * DAV:propstat.
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
				EWebDAVPropstatTraverseFunc func,
				gpointer func_user_data,
				GCancellable *cancellable,
				GError **error)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);
	if (xml)
		g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	return e_webdav_session_propfind_internal_sync (webdav, uri, depth, xml, func, func_user_data, NULL, cancellable, error);
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
	SoupMessage *message;
	GByteArray *bytes;
	gchar *content;
	gsize content_length;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_PROPPATCH, uri, error);
	if (!message)
		return FALSE;

	content = e_xml_document_get_content (xml, &content_length);
	if (!content) {
		g_object_unref (message);

		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

		return FALSE;
	}

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
		content, content_length, g_free);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to update properties"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_report_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @depth: (nullable): requested depth, can be %NULL, then no Depth header is sent
 * @xml: the request itself, as an #EXmlDocument
 * @func: (nullable) (scope call) (closure func_user_data): an #EWebDAVPropstatTraverseFunc function to call for each DAV:propstat in the multistatus response, or %NULL
 * @func_user_data: user data passed to @func
 * @out_content_type: (nullable) (transfer full): return location for response Content-Type, or %NULL
 * @out_content: (nullable) (transfer full): return location for response content, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues REPORT request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource. On success, calls @func for each returned
 * DAV:propstat.
 *
 * The report can result in a multistatus response, but also to raw data. In case
 * the @func is provided and the result is a multistatus response, then it is traversed
 * using this @func.
 *
 * The optional @out_content_type can be used to get content type of the response.
 * Free it with g_free(), when no longer needed.
 *
 * The optional @out_content can be used to get actual result content. Free it
 * with g_byte_array_free(), when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_report_sync (EWebDAVSession *webdav,
			      const gchar *uri,
			      const gchar *depth,
			      const EXmlDocument *xml,
			      EWebDAVPropstatTraverseFunc func,
			      gpointer func_user_data,
			      gchar **out_content_type,
			      GByteArray **out_content,
			      GCancellable *cancellable,
			      GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gchar *content;
	gsize content_length;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);

	if (out_content_type)
		*out_content_type = NULL;

	if (out_content)
		*out_content = NULL;

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, "REPORT", uri, error);
	if (!message)
		return FALSE;

	if (depth)
		soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", depth);

	content = e_xml_document_get_content (xml, &content_length);
	if (!content) {
		g_object_unref (message);

		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

		return FALSE;
	}

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
		content, content_length, g_free);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, TRUE, _("Failed to issue REPORT"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (success && func && soup_message_get_status (message) == SOUP_STATUS_MULTI_STATUS)
		success = e_webdav_session_traverse_multistatus_response (webdav, message, bytes, func, func_user_data, error);

	if (success) {
		if (out_content_type) {
			*out_content_type = g_strdup (soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL));
		}

		if (out_content) {
			*out_content = bytes;
			bytes = NULL;
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
 * Creates a new generic collection identified by @uri on the server.
 * To create specific collections use e_webdav_session_mkcalendar_sync()
 * or e_webdav_session_mkcol_addressbook_sync().
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
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_MKCOL, uri, error);
	if (!message)
		return FALSE;

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to create collection"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_mkcol_addressbook_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the collection to create
 * @display_name: (nullable): a human-readable display name to set, or %NULL
 * @description: (nullable): a human-readable description of the address book, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new address book collection identified by @uri on the server.
 *
 * Note that CardDAV RFC 6352 Section 5.2 forbids to create address book
 * resources under other address book resources (no nested address books
 * are allowed).
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_mkcol_addressbook_sync (EWebDAVSession *webdav,
					 const gchar *uri,
					 const gchar *display_name,
					 const gchar *description,
					 GCancellable *cancellable,
					 GError **error)
{
	SoupMessage *message;
	EXmlDocument *xml;
	gchar *content;
	gsize content_length;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_MKCOL, uri, error);
	if (!message)
		return FALSE;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "mkcol");
	e_xml_document_add_namespaces (xml, "A", E_WEBDAV_NS_CARDDAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "set");
	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "resourcetype");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "collection");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CARDDAV, "addressbook");
	e_xml_document_end_element (xml); /* resourcetype */

	if (display_name && *display_name) {
		e_xml_document_start_text_element (xml, E_WEBDAV_NS_DAV, "displayname");
		e_xml_document_write_string (xml, display_name);
		e_xml_document_end_element (xml);
	}

	if (description && *description) {
		e_xml_document_start_text_element (xml, E_WEBDAV_NS_CARDDAV, "addressbook-description");
		e_xml_document_write_string (xml, description);
		e_xml_document_end_element (xml);
	}

	e_xml_document_end_element (xml); /* prop */
	e_xml_document_end_element (xml); /* set */

	content = e_xml_document_get_content (xml, &content_length);
	if (!content) {
		g_object_unref (message);
		g_object_unref (xml);

		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get XML request content"));

		return FALSE;
	}

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
		content, content_length, g_free);

	g_object_unref (xml);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to create address book"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_mkcalendar_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the collection to create
 * @display_name: (nullable): a human-readable display name to set, or %NULL
 * @description: (nullable): a human-readable description of the calendar, or %NULL
 * @color: (nullable): a color to set, in format "&num;RRGGBB", or %NULL
 * @supports: a bit-or of EWebDAVResourceSupports values
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new calendar collection identified by @uri on the server.
 * The @supports defines what component types can be stored into
 * the created calendar collection. Only %E_WEBDAV_RESOURCE_SUPPORTS_NONE
 * and values related to iCalendar content can be used here.
 * Using %E_WEBDAV_RESOURCE_SUPPORTS_NONE means that everything is supported.
 *
 * Note that CalDAV RFC 4791 Section 4.2 forbids to create calendar
 * resources under other calendar resources (no nested calendars
 * are allowed).
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_mkcalendar_sync (EWebDAVSession *webdav,
				  const gchar *uri,
				  const gchar *display_name,
				  const gchar *description,
				  const gchar *color,
				  guint32 supports,
				  GCancellable *cancellable,
				  GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, "MKCALENDAR", uri, error);
	if (!message)
		return FALSE;

	supports = supports & (
		E_WEBDAV_RESOURCE_SUPPORTS_EVENTS |
		E_WEBDAV_RESOURCE_SUPPORTS_MEMOS |
		E_WEBDAV_RESOURCE_SUPPORTS_TASKS |
		E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY |
		E_WEBDAV_RESOURCE_SUPPORTS_TIMEZONE);

	if ((display_name && *display_name) ||
	    (description && *description) ||
	    (color && *color) ||
	    (supports != 0)) {
		EXmlDocument *xml;
		gchar *content;
		gsize content_length;

		xml = e_xml_document_new (E_WEBDAV_NS_CALDAV, "mkcalendar");
		e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

		e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "set");
		e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");

		if (display_name && *display_name) {
			e_xml_document_start_text_element (xml, E_WEBDAV_NS_DAV, "displayname");
			e_xml_document_write_string (xml, display_name);
			e_xml_document_end_element (xml);
		}

		if (description && *description) {
			e_xml_document_start_text_element (xml, E_WEBDAV_NS_CALDAV, "calendar-description");
			e_xml_document_write_string (xml, description);
			e_xml_document_end_element (xml);
		}

		if (color && *color) {
			e_xml_document_add_namespaces (xml, "IC", E_WEBDAV_NS_ICAL, NULL);

			e_xml_document_start_text_element (xml, E_WEBDAV_NS_ICAL, "calendar-color");
			e_xml_document_write_string (xml, color);
			e_xml_document_end_element (xml);
		}

		if (supports != 0 && supports != (E_WEBDAV_RESOURCE_SUPPORTS_EVENTS |
		    E_WEBDAV_RESOURCE_SUPPORTS_TASKS | E_WEBDAV_RESOURCE_SUPPORTS_MEMOS)) {
			/* If the user has selected Events, Tasks and Memos, all of them offered by
			 * Evolution, then user actually wants the new collection to contain whatever
			 * components the server supports, including VAVAILABILITY, or anything invented
			 * in the future, for which EDS and Evolution have no knowledge. */
			struct SupportValues {
				guint32 mask;
				const gchar *value;
			} values[] = {
				{ E_WEBDAV_RESOURCE_SUPPORTS_EVENTS, "VEVENT" },
				{ E_WEBDAV_RESOURCE_SUPPORTS_MEMOS, "VJOURNAL" },
				{ E_WEBDAV_RESOURCE_SUPPORTS_TASKS, "VTODO" },
				{ E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY, "VFREEBUSY" },
				{ E_WEBDAV_RESOURCE_SUPPORTS_TIMEZONE, "TIMEZONE" }
			};
			gint ii;

			e_xml_document_start_text_element (xml, E_WEBDAV_NS_CALDAV, "supported-calendar-component-set");

			for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
				if ((supports & values[ii].mask) != 0) {
					e_xml_document_start_text_element (xml, E_WEBDAV_NS_CALDAV, "comp");
					e_xml_document_add_attribute (xml, NULL, "name", values[ii].value);
					e_xml_document_end_element (xml); /* comp */
				}
			}

			e_xml_document_end_element (xml); /* supported-calendar-component-set */
		}

		e_xml_document_end_element (xml); /* prop */
		e_xml_document_end_element (xml); /* set */

		content = e_xml_document_get_content (xml, &content_length);
		if (!content) {
			g_object_unref (message);
			g_object_unref (xml);

			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get XML request content"));

			return FALSE;
		}

		e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
			content, content_length, g_free);

		g_object_unref (xml);
	}

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to create calendar"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
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

		header = soup_message_headers_get_one (soup_message_get_response_headers (message), "Location");
		if (header) {
			GUri *uri;

			uri = g_uri_parse_relative (soup_message_get_uri (message), header, SOUP_HTTP_URI_FLAGS | G_URI_FLAGS_PARSE_RELAXED, NULL);

			if (uri && g_uri_get_host (uri))
				*out_href = g_uri_to_string_partial (uri, G_URI_HIDE_PASSWORD);

			if (uri)
				g_uri_unref (uri);
		}

		if (!*out_href)
			*out_href = g_uri_to_string_partial (soup_message_get_uri (message), G_URI_HIDE_PASSWORD);
	}

	if (out_etag) {
		const gchar *header;

		*out_etag = NULL;

		header = soup_message_headers_get_list (soup_message_get_response_headers (message), "ETag");
		if (header)
			*out_etag = e_webdav_session_util_maybe_dequote (g_strdup (header));
	}
}

/**
 * e_webdav_session_get_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to read
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_headers: (out) (optional) (transfer full): optional return location for response #SoupMessageHeaders, or %NULL
 * @out_stream: (out caller-allocates): a #GOutputStream to write data to
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads a resource identified by @uri from the server and writes it
 * to the @stream. The URI cannot reference a collection.
 *
 * Free returned pointer of @out_href and @out_etag, if not %NULL, with g_free(),
 * when no longer needed.
 *
 * The optional @out_headers contains response headers. Free it with soup_message_headers_free(),
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
			   SoupMessageHeaders **out_headers,
			   GOutputStream *out_stream,
			   GCancellable *cancellable,
			   GError **error)
{
	SoupMessage *message;
	GInputStream *input_stream;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (G_IS_OUTPUT_STREAM (out_stream), FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	if (out_headers)
		*out_headers = NULL;

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_GET, uri, error);
	if (!message)
		return FALSE;

	input_stream = e_soup_session_send_message_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = input_stream != NULL;

	if (success) {
		gpointer buffer;
		gsize nread = 0, nwritten;
		gboolean first_chunk = TRUE;

		buffer = g_malloc (BUFFER_SIZE);

		while (success = g_input_stream_read_all (input_stream, buffer, BUFFER_SIZE, &nread, cancellable, error),
		       success && nread > 0) {
			if (first_chunk) {
				GByteArray tmp_bytes;

				first_chunk = FALSE;

				tmp_bytes.data = buffer;
				tmp_bytes.len = nread;

				success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, &tmp_bytes, FALSE, _("Failed to read resource"), error, TRUE, TRUE);
				if (!success)
					break;
			}

			success = g_output_stream_write_all (out_stream, buffer, nread, &nwritten, cancellable, error);
			if (!success)
				break;
		}

		if (success && first_chunk) {
			success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, NULL, FALSE, _("Failed to read resource"), error, TRUE, TRUE);
		}

		g_free (buffer);
	}

	if (success) {
		e_webdav_session_extract_href_and_etag (message, out_href, out_etag);

		if (out_headers)
			*out_headers = g_boxed_copy (SOUP_TYPE_MESSAGE_HEADERS, soup_message_get_response_headers (message));
	}

	g_clear_object (&input_stream);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_get_data_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to read
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_headers: (out) (optional) (transfer full): optional return location for response #SoupMessageHeaders, or %NULL
 * @out_bytes: (out) (transfer full): return location for bytes being read
 * @out_length: (out) (nullable): option return location for length of bytes being read, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reads a resource identified by @uri from the server. The URI cannot
 * reference a collection.
 *
 * The @out_bytes is filled by actual data being read. If not %NULL, @out_length
 * is populated with how many bytes had been read. The @out_bytes is always
 * NUL-terminated, while this termination byte is not part of @out_length.
 * Free the @out_bytes with g_free(), when no longer needed.
 *
 * Free returned pointer of @out_href and @out_etag, if not %NULL, with g_free(),
 * when no longer needed.
 *
 * The optional @out_headers contains response headers. Free it with soup_message_headers_free(),
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
				SoupMessageHeaders **out_headers,
				gchar **out_bytes,
				gsize *out_length,
				GCancellable *cancellable,
				GError **error)
{
	GOutputStream *output_stream;
	gsize bytes_written = 0;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (out_bytes != NULL, FALSE);

	*out_bytes = NULL;

	if (out_length)
		*out_length = 0;

	if (out_headers)
		*out_headers = NULL;

	output_stream = g_memory_output_stream_new_resizable ();

	success = e_webdav_session_get_sync (webdav, uri, out_href, out_etag, out_headers, output_stream, cancellable, error) &&
		g_output_stream_write_all (output_stream, "", 1, &bytes_written, cancellable, error) &&
		g_output_stream_close (output_stream, cancellable, error);

	if (success) {
		if (out_length)
			*out_length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (output_stream)) - 1;

		*out_bytes = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (output_stream));
	}

	g_object_unref (output_stream);

	return success;
}

static void
e_webdav_session_set_if_match_header (SoupMessage *message,
				      const gchar *etag)
{
	gint len;

	g_return_if_fail (SOUP_IS_MESSAGE (message));
	g_return_if_fail (etag != NULL);

	len = strlen (etag);

	if ((*etag == '\"' && len > 2 && etag[len - 1] == '\"') || strchr (etag, '\"')) {
		soup_message_headers_replace (soup_message_get_request_headers (message), "If-Match", etag);
	} else {
		gchar *quoted;

		quoted = g_strconcat ("\"", etag, "\"", NULL);
		soup_message_headers_replace (soup_message_get_request_headers (message), "If-Match", quoted);
		g_free (quoted);
	}
}

/**
 * e_webdav_session_put_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to write
 * @etag: (nullable): an ETag of the resource, if it's an existing resource, or %NULL
 * @content_type: Content-Type of the @bytes to be written
 * @in_headers: (nullable): additional #SoupMessageHeaders to be added to the request, or %NULL
 * @stream: a #GInputStream with data to be written
 * @stream_length: length of the @stream, or -1 if unknown
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_headers: (out) (optional) (transfer full): optional return location for response #SoupMessageHeaders, or %NULL
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
 * The optional @in_headers can contain additional headers to be added to the request.
 * These headers replace any existing in the request headers, without support for the list-values headers.
 *
 * The @out_href, if provided, is filled with the resulting URI
 * of the written resource. It can be different from the @uri when the server
 * redirected to a different location.
 *
 * The @out_etag contains ETag of the resource after it had been saved.
 *
 * The optional @out_headers contains response headers. Free it with soup_message_headers_free(),
 * when no longer needed.
 *
 * The @stream should support also #GSeekable interface, because the data
 * send can require restart of the send due to redirect or other reasons.
 *
 * This method uses Transfer-Encoding:chunked, in contrast to the
 * e_webdav_session_put_data_sync(), which writes data stored in memory
 * like any other request.
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
			   SoupMessageHeaders *in_headers,
			   GInputStream *stream,
			   gssize stream_length,
			   gchar **out_href,
			   gchar **out_etag,
			   SoupMessageHeaders **out_headers,
			   GCancellable *cancellable,
			   GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (content_type != NULL, FALSE);
	g_return_val_if_fail (G_IS_INPUT_STREAM (stream), FALSE);

	if (out_href)
		*out_href = NULL;
	if (out_etag)
		*out_etag = NULL;
	if (out_headers)
		*out_headers = NULL;

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_PUT, uri, error);
	if (!message)
		return FALSE;

	e_webdav_session_copy_request_headers (message, in_headers);

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
				e_webdav_session_set_if_match_header (message, etag);
			} else {
				soup_message_headers_replace (soup_message_get_request_headers (message), "If-None-Match", "*");
			}
		}
	}

	e_soup_session_util_set_message_request_body (message, NULL, stream, stream_length);

	if (content_type && *content_type)
		soup_message_headers_replace (soup_message_get_request_headers (message), "Content-Type", content_type);

	soup_message_headers_set_encoding (soup_message_get_request_headers (message), SOUP_ENCODING_CHUNKED);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to put data"), error, TRUE, TRUE) &&
		bytes != NULL;

	if (success) {
		if (success && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
			success = FALSE;

			g_set_error (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
				_("Failed to put data to server, error code %d (%s)"), soup_message_get_status (message),
				e_soup_session_util_status_to_string (soup_message_get_status (message), soup_message_get_reason_phrase (message)));
		}
	}

	if (success) {
		e_webdav_session_extract_href_and_etag (message, out_href, out_etag);

		if (out_headers)
			*out_headers = g_boxed_copy (SOUP_TYPE_MESSAGE_HEADERS, soup_message_get_response_headers (message));
	}

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_put_data_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to write
 * @etag: (nullable): an ETag of the resource, if it's an existing resource, or %NULL
 * @content_type: Content-Type of the @bytes to be written
 * @in_headers: (nullable): additional #SoupMessageHeaders to be added to the request, or %NULL
 * @bytes: actual bytes to be written
 * @length: how many bytes to write, or -1, when the @bytes is NUL-terminated
 * @out_href: (out) (nullable) (transfer full): optional return location for href of the resource, or %NULL
 * @out_etag: (out) (nullable) (transfer full): optional return location for etag of the resource, or %NULL
 * @out_headers: (out) (optional) (transfer full): optional return location for response #SoupMessageHeaders, or %NULL
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
 * The optional @in_headers can contain additional headers to be added to the request.
 * These headers replace any existing in the request headers, without support for the list-values headers.
 *
 * The @out_href, if provided, is filled with the resulting URI
 * of the written resource. It can be different from the @uri when the server
 * redirected to a different location.
 *
 * The @out_etag contains ETag of the resource after it had been saved.
 *
 * The optional @out_headers contains response headers. Free it with soup_message_headers_free(),
 * when no longer needed.
 *
 * To write large data use e_webdav_session_put_sync() instead.
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
				SoupMessageHeaders *in_headers,
				const gchar *bytes,
				gsize length,
				gchar **out_href,
				gchar **out_etag,
				SoupMessageHeaders **out_headers,
				GCancellable *cancellable,
				GError **error)
{
	SoupMessage *message;
	GByteArray *ret_bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (content_type != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);

	if (length == (gsize) -1)
		length = strlen (bytes);
	if (out_href)
		*out_href = NULL;
	if (out_etag)
		*out_etag = NULL;
	if (out_headers)
		*out_headers = NULL;

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_PUT, uri, error);
	if (!message)
		return FALSE;

	e_webdav_session_copy_request_headers (message, in_headers);

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
				e_webdav_session_set_if_match_header (message, etag);
			} else {
				soup_message_headers_replace (soup_message_get_request_headers (message), "If-None-Match", "*");
			}
		}
	}

	if (content_type && *content_type)
		soup_message_headers_replace (soup_message_get_request_headers (message), "Content-Type", content_type);

	soup_message_headers_replace (soup_message_get_request_headers (message), "Prefer", "return=minimal");

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, content_type, bytes, length, NULL);

	ret_bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, ret_bytes, FALSE, _("Failed to put data"), error, TRUE, TRUE) &&
		ret_bytes != NULL;

	if (success) {
		if (success && !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
			success = FALSE;

			g_set_error (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
				_("Failed to put data to server, error code %d (%s)"), soup_message_get_status (message),
				e_soup_session_util_status_to_string (soup_message_get_status (message), soup_message_get_reason_phrase (message)));
		}
	}

	if (success) {
		e_webdav_session_extract_href_and_etag (message, out_href, out_etag);

		if (out_headers)
			*out_headers = g_boxed_copy (SOUP_TYPE_MESSAGE_HEADERS, soup_message_get_response_headers (message));
	}

	if (ret_bytes)
		g_byte_array_free (ret_bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_delete_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to delete
 * @depth: (nullable): optional requested depth, can be one of %E_WEBDAV_DEPTH_THIS or %E_WEBDAV_DEPTH_INFINITY, or %NULL
 * @etag: (nullable): an optional ETag of the resource, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes a resource identified by @uri on the server. The URI can
 * reference a collection, in which case @depth should be %E_WEBDAV_DEPTH_INFINITY.
 * Use @depth %E_WEBDAV_DEPTH_THIS when deleting a regular resource, or %NULL,
 * to let the server use default Depth.
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
	return e_webdav_session_delete_with_headers_sync (webdav, uri, depth, etag, NULL, cancellable, error);
}

/**
 * e_webdav_session_delete_with_headers_sync:
 * @webdav: an #EWebDAVSession
 * @uri: URI of the resource to delete
 * @depth: (nullable): optional requested depth, can be one of %E_WEBDAV_DEPTH_THIS or %E_WEBDAV_DEPTH_INFINITY, or %NULL
 * @etag: (nullable): an optional ETag of the resource, or %NULL
 * @in_headers: (nullable): additional #SoupMessageHeaders to be added to the request, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Deletes a resource identified by @uri on the server. The URI can
 * reference a collection, in which case @depth should be %E_WEBDAV_DEPTH_INFINITY.
 * Use @depth %E_WEBDAV_DEPTH_THIS when deleting a regular resource, or %NULL,
 * to let the server use default Depth.
 *
 * The @etag argument is used to avoid clashes when overwriting existing resources.
 * Use %NULL @etag when deleting collection resources or to force the deletion,
 * otherwise provide a valid ETag of a non-collection resource to verify that
 * the version requested to delete is the same as on the server.
 *
 * The optional @in_headers can contain additional headers to be added to the request.
 * These headers replace any existing in the request headers, without support for the list-values headers.
 *
 * Note that the actual usage of @etag is also influenced by #ESourceWebdav:avoid-ifmatch
 * property of the associated #ESource.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.50
 **/
gboolean
e_webdav_session_delete_with_headers_sync (EWebDAVSession *webdav,
					   const gchar *uri,
					   const gchar *depth,
					   const gchar *etag,
					   SoupMessageHeaders *in_headers,
					   GCancellable *cancellable,
					   GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_DELETE, uri, error);
	if (!message)
		return FALSE;

	e_webdav_session_copy_request_headers (message, in_headers);

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
			e_webdav_session_set_if_match_header (message, etag);
		}
	}

	if (depth)
		soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", depth);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to delete resource"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

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
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (source_uri != NULL, FALSE);
	g_return_val_if_fail (destination_uri != NULL, FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_COPY, source_uri, error);
	if (!message)
		return FALSE;

	soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", depth);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Destination", destination_uri);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Overwrite", can_overwrite ? "T" : "F");

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to copy resource"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

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
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (source_uri != NULL, FALSE);
	g_return_val_if_fail (destination_uri != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_MOVE, source_uri, error);
	if (!message)
		return FALSE;

	soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", E_WEBDAV_DEPTH_INFINITY);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Destination", destination_uri);
	soup_message_headers_replace (soup_message_get_request_headers (message), "Overwrite", can_overwrite ? "T" : "F");

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to move resource"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

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
			    xmlDoc **out_xml_response,
			    GCancellable *cancellable,
			    GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (depth != NULL, FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);
	g_return_val_if_fail (out_lock_token != NULL, FALSE);

	*out_lock_token = NULL;

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_LOCK, uri, error);
	if (!message)
		return FALSE;

	if (depth)
		soup_message_headers_replace (soup_message_get_request_headers (message), "Depth", depth);

	if (lock_timeout) {
		gchar *value;

		value = g_strdup_printf ("Second-%d", lock_timeout);
		soup_message_headers_replace (soup_message_get_request_headers (message), "Timeout", value);
		g_free (value);
	} else {
		soup_message_headers_replace (soup_message_get_request_headers (message), "Timeout", "Infinite");
	}

	if (xml) {
		gchar *content;
		gsize content_length;

		content = e_xml_document_get_content (xml, &content_length);
		if (!content) {
			g_object_unref (message);

			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

			return FALSE;
		}

		e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
			content, content_length, g_free);
	}

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to lock resource"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (success && out_xml_response) {
		const gchar *content_type;

		*out_xml_response = NULL;

		content_type = soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL);
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

			doc = e_xml_parse_data (bytes->data, bytes->len);
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
		*out_lock_token = g_strdup (soup_message_headers_get_list (soup_message_get_response_headers (message), "Lock-Token"));

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
	SoupMessage *message;
	GByteArray *bytes;
	gchar *value;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (lock_token != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_LOCK, uri, error);
	if (!message)
		return FALSE;

	if (lock_timeout) {
		value = g_strdup_printf ("Second-%d", lock_timeout);
		soup_message_headers_replace (soup_message_get_request_headers (message), "Timeout", value);
		g_free (value);
	} else {
		soup_message_headers_replace (soup_message_get_request_headers (message), "Timeout", "Infinite");
	}

	soup_message_headers_replace (soup_message_get_request_headers (message), "Lock-Token", lock_token);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to refresh lock"), error, TRUE, FALSE) &&
		bytes != NULL;

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
	SoupMessage *message;
	GByteArray *bytes;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (lock_token != NULL, FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, SOUP_METHOD_UNLOCK, uri, error);
	if (!message)
		return FALSE;

	soup_message_headers_replace (soup_message_get_request_headers (message), "Lock-Token", lock_token);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, FALSE, _("Failed to unlock"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

static gboolean
e_webdav_session_traverse_propstat_response (EWebDAVSession *webdav,
					     SoupMessage *message,
					     const GByteArray *xml_data,
					     gboolean require_multistatus,
					     const gchar *top_path_ns_href1,
					     const gchar *top_path_name1,
					     const gchar *top_path_ns_href2,
					     const gchar *top_path_name2,
					     EWebDAVPropstatTraverseFunc func,
					     gpointer func_user_data,
					     GError **error)
{
	GUri *request_uri = NULL;
	xmlDocPtr doc;
	xmlNodePtr top_node, node;
	gboolean do_stop = FALSE;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xml_data != NULL, FALSE);
	g_return_val_if_fail (top_path_name1 != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (message) {
		const gchar *content_type;

		if (require_multistatus && soup_message_get_status (message) != SOUP_STATUS_MULTI_STATUS) {
			g_set_error (error, E_SOUP_SESSION_ERROR, soup_message_get_status (message),
				_("Expected multistatus response, but %d returned (%s)"), soup_message_get_status (message),
				e_soup_session_util_status_to_string (soup_message_get_status (message), soup_message_get_reason_phrase (message)));

			return FALSE;
		}

		content_type = soup_message_headers_get_content_type (soup_message_get_response_headers (message), NULL);
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

	doc = e_xml_parse_data (xml_data->data, xml_data->len);

	if (!doc) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("Failed to parse XML data"));

		return FALSE;
	}

	top_node = xmlDocGetRootElement (doc);

	if (!top_node) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("XML data does not have root node"));

		xmlFreeDoc (doc);

		return FALSE;
	}

	top_node = e_xml_find_sibling (top_node, top_path_ns_href1, top_path_name1);

	if (!top_node) {
		gchar *tmp;

		tmp = g_strconcat (
			top_path_ns_href1 ? top_path_ns_href1 : "",
			top_path_ns_href1 ? ":" : "",
			top_path_name1,
			top_path_name2 ? "/" : "",
			(top_path_name2 && top_path_ns_href2) ? top_path_ns_href2 : "",
			(top_path_name2 && top_path_ns_href2) ? ":" : "",
			top_path_name2 ? top_path_name2 : "",
			NULL);

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			_("XML data doesn't have required structure (%s)"), tmp);

		xmlFreeDoc (doc);
		g_free (tmp);

		return FALSE;
	}

	/* The server can return 'multistatus' with no 'response' children, which is not a problem */
	if (top_path_name2)
		top_node = e_xml_find_child (top_node, top_path_ns_href2, top_path_name2);

	for (node = top_node; node && !do_stop; node = xmlNextElementSibling (node)) {
		xmlNodePtr href_node = NULL, propstat_node = NULL;
		xmlNodePtr status_node = NULL, prop_node = NULL;
		const xmlChar *href_content, *status_content;
		guint status_code;
		gchar *full_uri;

		e_xml_find_children_nodes (node, 2,
			E_WEBDAV_NS_DAV, "href", &href_node,
			E_WEBDAV_NS_DAV, "propstat", &propstat_node);

		if (!href_node || !propstat_node) {
			if (href_node) {
				e_xml_find_children_nodes (node, 1,
					E_WEBDAV_NS_DAV, "status", &status_node);

				if (status_node) {
					href_content = e_xml_get_node_text (href_node);
					g_warn_if_fail (href_content != NULL);

					if (!href_content)
						continue;

					full_uri = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) href_content);
					status_content = e_xml_get_node_text (status_node);

					if (!status_content || !soup_headers_parse_status_line ((const gchar *) status_content, NULL, &status_code, NULL))
						status_code = 0;

					/* The 'status_node' is not the right node, but let it be something non-NULL */
					do_stop = !func (webdav, status_node, request_uri, full_uri ? full_uri : (const gchar *) href_content, status_code, func_user_data);

					g_free (full_uri);
				}
			}
			continue;
		}

		href_content = e_xml_get_node_text (href_node);
		g_warn_if_fail (href_content != NULL);

		if (!href_content)
			continue;

		full_uri = e_webdav_session_ensure_full_uri (webdav, request_uri, (const gchar *) href_content);

		while (propstat_node && !do_stop) {
			e_xml_find_children_nodes (propstat_node, 2,
				E_WEBDAV_NS_DAV, "status", &status_node,
				E_WEBDAV_NS_DAV, "prop", &prop_node);

			status_content = e_xml_get_node_text (status_node);

			if (!status_content || !soup_headers_parse_status_line ((const gchar *) status_content, NULL, &status_code, NULL))
				status_code = 0;

			if (prop_node && prop_node->children)
				do_stop = !func (webdav, prop_node, request_uri, full_uri ? full_uri : (const gchar *) href_content, status_code, func_user_data);

			propstat_node = e_xml_find_next_sibling (propstat_node, E_WEBDAV_NS_DAV, "propstat");
		}

		g_free (full_uri);
	}

	xmlFreeDoc (doc);

	return TRUE;
}

/**
 * e_webdav_session_traverse_multistatus_response:
 * @webdav: an #EWebDAVSession
 * @message: (nullable): an optional #SoupMessage corresponding to the response, or %NULL
 * @xml_data: a #GByteArray containing DAV:multistatus response
 * @func: (scope call) (closure func_user_data): an #EWebDAVPropstatTraverseFunc function to call for each DAV:propstat in the multistatus response
 * @func_user_data: user data passed to @func
 * @error: return location for a #GError, or %NULL
 *
 * Traverses a DAV:multistatus response and calls @func for each returned DAV:propstat.
 *
 * The @message, if provided, is used to verify that the response is a multi-status
 * and that the Content-Type is properly set. It's used to get a request URI as well.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_traverse_multistatus_response (EWebDAVSession *webdav,
						SoupMessage *message,
						const GByteArray *xml_data,
						EWebDAVPropstatTraverseFunc func,
						gpointer func_user_data,
						GError **error)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xml_data != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	return e_webdav_session_traverse_propstat_response (webdav, message, xml_data, TRUE,
		E_WEBDAV_NS_DAV, "multistatus",
		E_WEBDAV_NS_DAV, "response",
		func, func_user_data, error);
}

/**
 * e_webdav_session_traverse_mkcol_response:
 * @webdav: an #EWebDAVSession
 * @message: (nullable): an optional #SoupMessage corresponding to the response, or %NULL
 * @xml_data: a #GByteArray containing DAV:mkcol-response response
 * @func: (scope call) (closure func_user_data): an #EWebDAVPropstatTraverseFunc function to call for each DAV:propstat in the response
 * @func_user_data: user data passed to @func
 * @error: return location for a #GError, or %NULL
 *
 * Traverses a DAV:mkcol-response response and calls @func for each returned DAV:propstat.
 *
 * The @message, if provided, is used to verify that the response is an XML Content-Type.
 * It's used to get the request URI as well.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_traverse_mkcol_response (EWebDAVSession *webdav,
					  SoupMessage *message,
					  const GByteArray *xml_data,
					  EWebDAVPropstatTraverseFunc func,
					  gpointer func_user_data,
					  GError **error)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xml_data != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	return e_webdav_session_traverse_propstat_response (webdav, message, xml_data, FALSE,
		E_WEBDAV_NS_DAV, "mkcol-response",
		NULL, NULL,
		func, func_user_data, error);
}

/**
 * e_webdav_session_traverse_mkcalendar_response:
 * @webdav: an #EWebDAVSession
 * @message: (nullable): an optional #SoupMessage corresponding to the response, or %NULL
 * @xml_data: a #GByteArray containing CALDAV:mkcalendar-response response
 * @func: (scope call) (closure func_user_data): an #EWebDAVPropstatTraverseFunc function to call for each DAV:propstat in the response
 * @func_user_data: user data passed to @func
 * @error: return location for a #GError, or %NULL
 *
 * Traverses a CALDAV:mkcalendar-response response and calls @func for each returned DAV:propstat.
 *
 * The @message, if provided, is used to verify that the response is an XML Content-Type.
 * It's used to get the request URI as well.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_traverse_mkcalendar_response (EWebDAVSession *webdav,
					       SoupMessage *message,
					       const GByteArray *xml_data,
					       EWebDAVPropstatTraverseFunc func,
					       gpointer func_user_data,
					       GError **error)
{
	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (xml_data != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	return e_webdav_session_traverse_propstat_response (webdav, message, xml_data, FALSE,
		E_WEBDAV_NS_CALDAV, "mkcalendar-response",
		NULL, NULL,
		func, func_user_data, error);
}

static gboolean
e_webdav_session_getctag_cb (EWebDAVSession *webdav,
			     xmlNodePtr prop_node,
			     const GUri *request_uri,
			     const gchar *href,
			     guint status_code,
			     gpointer user_data)
{
	if (status_code == SOUP_STATUS_OK) {
		const xmlChar *ctag_content;
		gchar **out_ctag = user_data;

		g_return_val_if_fail (out_ctag != NULL, FALSE);

		ctag_content = e_xml_find_child_and_get_text (prop_node, E_WEBDAV_NS_CALENDARSERVER, "getctag");

		if (ctag_content && *ctag_content)
			*out_ctag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) ctag_content));
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
 * Issues a getctag property request for a collection identified by @uri, or,
 * in case it's %NULL, on the URI defined in associated #ESource. The ctag is
 * a collection tag, which changes whenever the collection changes (similar
 * to etag). The getctag is an extension, thus the function can fail when
 * the server doesn't support it.
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
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALENDARSERVER, "getctag");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_getctag_cb, out_ctag, cancellable, error);

	g_object_unref (xml);

	return success && *out_ctag != NULL;
}

static EWebDAVResourceKind
e_webdav_session_extract_kind (xmlNodePtr parent_node)
{
	xmlNodePtr resourcetype;

	g_return_val_if_fail (parent_node != NULL, E_WEBDAV_RESOURCE_KIND_UNKNOWN);

	resourcetype = e_xml_find_child (parent_node, E_WEBDAV_NS_DAV, "resourcetype");

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_CARDDAV, "addressbook"))
		return E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK;

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_CALDAV, "calendar"))
		return E_WEBDAV_RESOURCE_KIND_CALENDAR;

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_CALDAV, "schedule-inbox"))
		return E_WEBDAV_RESOURCE_KIND_SCHEDULE_INBOX;

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_CALDAV, "schedule-outbox"))
		return E_WEBDAV_RESOURCE_KIND_SCHEDULE_OUTBOX;

	/* These are subscribed iCalendar files, aka 'On The Web' calendars */
	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_DAV, "collection") &&
	    e_xml_find_child (resourcetype, E_WEBDAV_NS_CALENDARSERVER, "subscribed") &&
	    e_xml_find_in_hierarchy (parent_node, E_WEBDAV_NS_CALENDARSERVER, "source", E_WEBDAV_NS_DAV, "href", NULL, NULL))
		return E_WEBDAV_RESOURCE_KIND_SUBSCRIBED_ICALENDAR;

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_DAV, "principal"))
		return E_WEBDAV_RESOURCE_KIND_PRINCIPAL;

	if (e_xml_find_child (resourcetype, E_WEBDAV_NS_DAV, "collection"))
		return E_WEBDAV_RESOURCE_KIND_COLLECTION;

	return E_WEBDAV_RESOURCE_KIND_RESOURCE;
}

static guint32
e_webdav_session_extract_supports (xmlNodePtr prop_node,
				   EWebDAVResourceKind kind)
{
	guint32 supports = E_WEBDAV_RESOURCE_SUPPORTS_NONE;

	g_return_val_if_fail (prop_node != NULL, E_WEBDAV_RESOURCE_SUPPORTS_NONE);

	if (kind == E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK &&
	    e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_DAV, "resourcetype", E_WEBDAV_NS_CARDDAV, "addressbook", NULL, NULL))
		supports = supports | E_WEBDAV_RESOURCE_SUPPORTS_CONTACTS;

	if (kind == E_WEBDAV_RESOURCE_KIND_CALENDAR || kind == E_WEBDAV_RESOURCE_KIND_SCHEDULE_INBOX ||
	    kind == E_WEBDAV_RESOURCE_KIND_SCHEDULE_OUTBOX) {
		xmlNodePtr calendar_components;

		calendar_components = e_xml_find_child (prop_node, E_WEBDAV_NS_CALDAV, "supported-calendar-component-set");

		if (calendar_components) {
			xmlNodePtr node;
			gint found_comps = 0;

			for (node = calendar_components->children; node; node = xmlNextElementSibling (node)) {
				if (e_xml_is_element_name (node, E_WEBDAV_NS_CALDAV, "comp")) {
					xmlChar *name;

					found_comps++;

					name = xmlGetProp (node, (const xmlChar *) "name");

					if (!name)
						continue;

					if (g_ascii_strcasecmp ((const gchar *) name, "VEVENT") == 0)
						supports |= E_WEBDAV_RESOURCE_SUPPORTS_EVENTS;
					else if (g_ascii_strcasecmp ((const gchar *) name, "VJOURNAL") == 0)
						supports |= E_WEBDAV_RESOURCE_SUPPORTS_MEMOS;
					else if (g_ascii_strcasecmp ((const gchar *) name, "VTODO") == 0)
						supports |= E_WEBDAV_RESOURCE_SUPPORTS_TASKS;
					else if (g_ascii_strcasecmp ((const gchar *) name, "VFREEBUSY") == 0)
						supports |= E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY;
					else if (g_ascii_strcasecmp ((const gchar *) name, "VTIMEZONE") == 0)
						supports |= E_WEBDAV_RESOURCE_SUPPORTS_TIMEZONE;

					xmlFree (name);
				}
			}

			if (!found_comps) {
				/* If the property is not present, assume all component
				 * types are supported.  (RFC 4791, Section 5.2.3) */
				supports = supports |
					E_WEBDAV_RESOURCE_SUPPORTS_EVENTS |
					E_WEBDAV_RESOURCE_SUPPORTS_MEMOS |
					E_WEBDAV_RESOURCE_SUPPORTS_TASKS |
					E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY |
					E_WEBDAV_RESOURCE_SUPPORTS_TIMEZONE;
			}
		}
	}

	return supports;
}

static gchar *
e_webdav_session_extract_nonempty (xmlNodePtr parent,
				   const gchar *prop_ns_href,
				   const gchar *prop_name,
				   const gchar *alternative_prop_ns_href,
				   const gchar *alternative_prop_name)
{
	const xmlChar *x_value;
	gchar *value = NULL;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	x_value = e_xml_find_child_and_get_text (parent, prop_ns_href, prop_name);

	if (x_value && *x_value)
		value = g_strdup ((const gchar *) x_value);

	if (!value && alternative_prop_name) {
		x_value = e_xml_find_child_and_get_text (parent, alternative_prop_ns_href, alternative_prop_name);

		if (x_value && *x_value)
			value = g_strdup ((const gchar *) x_value);
	}

	if (!value)
		return NULL;

	if (!*value) {
		g_free (value);
		return NULL;
	}

	return e_webdav_session_util_maybe_dequote (value);
}

static gsize
e_webdav_session_extract_content_length (xmlNodePtr parent)
{
	gchar *value;
	gsize length;

	g_return_val_if_fail (parent != NULL, 0);

	value = e_webdav_session_extract_nonempty (parent, E_WEBDAV_NS_DAV, "getcontentlength", NULL, NULL);
	if (!value)
		return 0;

	length = g_ascii_strtoll (value, NULL, 10);

	g_free (value);

	return length;
}

static guint
e_webdav_session_extract_uint (xmlNodePtr parent,
			       const gchar *prop_ns_href,
			       const gchar *prop_name)
{
	gchar *value_str, *end_ptr = NULL;
	guint64 value;

	g_return_val_if_fail (parent != NULL, (guint) -1);

	value_str = e_webdav_session_extract_nonempty (parent, prop_ns_href, prop_name, NULL, NULL);
	if (!value_str)
		return (guint) -1;

	value = g_ascii_strtoull (value_str, &end_ptr, 10);

	g_free (value_str);

	if (end_ptr == value_str)
		return (guint) -1;

	return (guint) value;
}

static gint64
e_webdav_session_extract_datetime (xmlNodePtr parent,
				   const gchar *ns_href,
				   const gchar *prop,
				   gboolean is_iso_property)
{
	gchar *value;
	gint64 result = -1;

	g_return_val_if_fail (parent != NULL, -1);
	g_return_val_if_fail (prop != NULL, -1);

	value = e_webdav_session_extract_nonempty (parent, ns_href, prop, NULL, NULL);
	if (!value)
		return -1;

	if (is_iso_property) {
		GDateTime *datetime;

		datetime = g_date_time_new_from_iso8601 (value, NULL);
		if (datetime) {
			result = g_date_time_to_unix (datetime);
			g_date_time_unref (datetime);
		}
	} else if (!is_iso_property) {
		result = (gint64) camel_header_decode_date (value, NULL);
	}

	g_free (value);
	return result;
}

static gboolean
e_webdav_session_list_cb (EWebDAVSession *webdav,
			  xmlNodePtr prop_node,
			  const GUri *request_uri,
			  const gchar *href,
			  guint status_code,
			  gpointer user_data)
{
	GSList **out_resources = user_data;

	g_return_val_if_fail (out_resources != NULL, FALSE);
	g_return_val_if_fail (request_uri != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		EWebDAVResource *resource;
		EWebDAVResourceKind kind;
		guint32 supports;
		gchar *etag;
		gchar *display_name;
		gchar *content_type;
		gsize content_length;
		gint64 creation_date;
		gint64 last_modified;
		gchar *description;
		gchar *color;
		gchar *source_href = NULL;
		guint order;

		kind = e_webdav_session_extract_kind (prop_node);
		if (kind == E_WEBDAV_RESOURCE_KIND_UNKNOWN)
			return TRUE;

		if (kind == E_WEBDAV_RESOURCE_KIND_SUBSCRIBED_ICALENDAR) {
			xmlNodePtr source_href_node;
			const xmlChar *x_source_href = NULL;


			source_href_node = e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_CALENDARSERVER, "source", E_WEBDAV_NS_DAV, "href", NULL, NULL);

			if (!source_href_node)
				return TRUE;

			x_source_href = e_xml_get_node_text (source_href_node);

			if (!x_source_href || !*x_source_href)
				return TRUE;

			source_href = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) x_source_href));
		}

		supports = e_webdav_session_extract_supports (prop_node, kind);
		etag = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_DAV, "getetag", E_WEBDAV_NS_CALENDARSERVER, "getctag");
		display_name = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_DAV, "displayname", NULL, NULL);
		content_type = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_DAV, "getcontenttype", NULL, NULL);
		content_length = e_webdav_session_extract_content_length (prop_node);
		creation_date = e_webdav_session_extract_datetime (prop_node, E_WEBDAV_NS_DAV, "creationdate", TRUE);
		last_modified = e_webdav_session_extract_datetime (prop_node, E_WEBDAV_NS_DAV, "getlastmodified", FALSE);
		description = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_CALDAV, "calendar-description",
			E_WEBDAV_NS_CARDDAV, "addressbook-description");
		color = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_ICAL, "calendar-color", NULL, NULL);
		order = e_webdav_session_extract_uint (prop_node, E_WEBDAV_NS_ICAL, "calendar-order");

		resource = e_webdav_resource_new (kind, supports,
			source_href ? source_href : href,
			NULL, /* etag */
			NULL, /* display_name */
			NULL, /* content_type */
			content_length,
			(glong) creation_date,
			(glong) last_modified,
			NULL, /* description */
			NULL, /* color */
			order);
		resource->etag = etag;
		resource->display_name = display_name;
		resource->content_type = content_type;
		resource->description = description;
		resource->color = color;

		*out_resources = g_slist_prepend (*out_resources, resource);

		g_free (source_href);
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
 * Lists content of the @uri, or, in case it's %NULL, of the URI defined
 * in associated #ESource, which should point to a collection. The @flags
 * influences which properties are read for the resources.
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
	gboolean calendar_props, addressbook_props;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_resources != NULL, FALSE);

	*out_resources = NULL;

	if (!(flags & (E_WEBDAV_LIST_ONLY_CALENDAR | E_WEBDAV_LIST_ONLY_ADDRESSBOOK))) {
		calendar_props = TRUE;
		addressbook_props = TRUE;
	} else {
		calendar_props = (flags & E_WEBDAV_LIST_ONLY_CALENDAR) != 0;
		addressbook_props = (flags & E_WEBDAV_LIST_ONLY_ADDRESSBOOK) != 0;
	}

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");

	e_xml_document_add_empty_element (xml, NULL, "resourcetype");

	if (calendar_props) {
		e_xml_document_add_namespaces (xml, "CS", E_WEBDAV_NS_CALENDARSERVER, NULL);

		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALENDARSERVER, "source");
	}

	if (calendar_props && (
	    (flags & E_WEBDAV_LIST_SUPPORTS) != 0 ||
	    (flags & E_WEBDAV_LIST_DESCRIPTION) != 0)) {
		e_xml_document_add_namespaces (xml, "C", E_WEBDAV_NS_CALDAV, NULL);
	}

	if (calendar_props && (flags & E_WEBDAV_LIST_SUPPORTS) != 0) {
		e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "supported-calendar-component-set");
	}

	if ((flags & E_WEBDAV_LIST_DISPLAY_NAME) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "displayname");
	}

	if ((flags & E_WEBDAV_LIST_ETAG) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "getetag");

		if (calendar_props)
			e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALENDARSERVER, "getctag");
	}

	if ((flags & E_WEBDAV_LIST_CONTENT_TYPE) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "getcontenttype");
	}

	if ((flags & E_WEBDAV_LIST_CONTENT_LENGTH) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "getcontentlength");
	}

	if ((flags & E_WEBDAV_LIST_CREATION_DATE) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "creationdate");
	}

	if ((flags & E_WEBDAV_LIST_LAST_MODIFIED) != 0) {
		e_xml_document_add_empty_element (xml, NULL, "getlastmodified");
	}

	if ((flags & E_WEBDAV_LIST_DESCRIPTION) != 0) {
		if (calendar_props)
			e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALDAV, "calendar-description");

		if (addressbook_props) {
			e_xml_document_add_namespaces (xml, "A", E_WEBDAV_NS_CARDDAV, NULL);

			e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CARDDAV, "addressbook-description");
		}
	}

	if (calendar_props && ((flags & E_WEBDAV_LIST_COLOR) != 0 || (flags & E_WEBDAV_LIST_ORDER) != 0)) {
		e_xml_document_add_namespaces (xml, "IC", E_WEBDAV_NS_ICAL, NULL);

		if ((flags & E_WEBDAV_LIST_COLOR) != 0)
			e_xml_document_add_empty_element (xml, E_WEBDAV_NS_ICAL, "calendar-color");

		if ((flags & E_WEBDAV_LIST_ORDER) != 0)
			e_xml_document_add_empty_element (xml, E_WEBDAV_NS_ICAL, "calendar-order");
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
				gchar *href_decoded = g_uri_unescape_string (resource->href, NULL);

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
 * @changes: (element-type EWebDAVPropertyChange): a #GSList with request changes
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Updates properties (set/remove) on the provided @uri, or, in case it's %NULL,
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
			e_xml_document_add_empty_element (xml, change->ns_uri, change->name);
			e_xml_document_end_element (xml); /* prop */
			e_xml_document_end_element (xml); /* remove */
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
 * Locks a resource identified by @uri, or, in case it's %NULL, by the URI defined
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
		e_xml_document_add_empty_element (xml, NULL, "exclusive");
		break;
	case E_WEBDAV_LOCK_SHARED:
		e_xml_document_add_empty_element (xml, NULL, "shared");
		break;
	}
	e_xml_document_end_element (xml); /* lockscope */

	e_xml_document_start_element (xml, NULL, "locktype");
	e_xml_document_add_empty_element (xml, NULL, "write");
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

static void
e_webdav_session_traverse_privilege_level (xmlNodePtr parent_node,
					   GNode *parent)
{
	xmlNodePtr node;

	g_return_if_fail (parent_node != NULL);
	g_return_if_fail (parent != NULL);

	for (node = e_xml_find_child (parent_node, E_WEBDAV_NS_DAV, "supported-privilege");
	     node;
	     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "supported-privilege")) {
		xmlNodePtr privilege_node;

		privilege_node = e_xml_find_child (node, E_WEBDAV_NS_DAV, "privilege");

		if (privilege_node) {
			GNode *child;
			const xmlChar *description;
			EWebDAVPrivilegeKind kind = E_WEBDAV_PRIVILEGE_KIND_COMMON;
			EWebDAVPrivilegeHint hint = E_WEBDAV_PRIVILEGE_HINT_UNKNOWN;
			EWebDAVPrivilege *privilege;

			if (e_xml_find_child (privilege_node, E_WEBDAV_NS_DAV, "abstract"))
				kind = E_WEBDAV_PRIVILEGE_KIND_ABSTRACT;
			else if (e_xml_find_child (privilege_node, E_WEBDAV_NS_DAV, "aggregate"))
				kind = E_WEBDAV_PRIVILEGE_KIND_AGGREGATE;

			description = e_xml_find_child_and_get_text (privilege_node, E_WEBDAV_NS_DAV, "description");
			privilege = e_webdav_privilege_new ((const gchar *) ((privilege_node->ns && privilege_node->ns->href) ? privilege_node->ns->href : NULL),
				(const gchar *) privilege_node->name,
				(const gchar *) description,
				kind,
				hint);
			child = g_node_new (privilege);
			g_node_append (parent, child);

			privilege_node = e_xml_find_child (privilege_node, E_WEBDAV_NS_DAV, "supported-privilege");

			if (privilege_node)
				e_webdav_session_traverse_privilege_level (privilege_node, child);
		}
	}
}

static gboolean
e_webdav_session_supported_privilege_set_cb (EWebDAVSession *webdav,
					     xmlNodePtr prop_node,
					     const GUri *request_uri,
					     const gchar *href,
					     guint status_code,
					     gpointer user_data)
{
	GNode **out_privileges = user_data;

	g_return_val_if_fail (out_privileges != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK &&
	    e_xml_find_in_hierarchy (prop_node, E_WEBDAV_NS_DAV, "supported-privilege-set", E_WEBDAV_NS_DAV, "supported-privilege", NULL, NULL)) {
		GNode *root;

		root = g_node_new (NULL);

		e_webdav_session_traverse_privilege_level (prop_node, root);

		*out_privileges = root;
	}

	return TRUE;
}

/**
 * e_webdav_session_acl_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @xml:the request itself, as an #EXmlDocument, the root element should be DAV:acl
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues ACL request on the provided @uri, or, in case it's %NULL, on the URI
 * defined in associated #ESource.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_acl_sync (EWebDAVSession *webdav,
			   const gchar *uri,
			   const EXmlDocument *xml,
			   GCancellable *cancellable,
			   GError **error)
{
	SoupMessage *message;
	GByteArray *bytes;
	gchar *content;
	gsize content_length;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (E_IS_XML_DOCUMENT (xml), FALSE);

	g_clear_pointer (&webdav->priv->last_dav_error_code, g_free);

	message = e_webdav_session_new_message (webdav, "ACL", uri, error);
	if (!message)
		return FALSE;

	content = e_xml_document_get_content (xml, &content_length);
	if (!content) {
		g_object_unref (message);

		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Failed to get input XML content"));

		return FALSE;
	}

	e_soup_session_util_set_message_request_body_from_data (message, FALSE, E_WEBDAV_CONTENT_TYPE_XML,
		content, content_length, g_free);

	bytes = e_soup_session_send_message_simple_sync (E_SOUP_SESSION (webdav), message, cancellable, error);

	success = !e_webdav_session_replace_with_detailed_error_internal (webdav, message, bytes, TRUE, _("Failed to get access control list"), error, TRUE, FALSE) &&
		bytes != NULL;

	if (bytes)
		g_byte_array_free (bytes, TRUE);
	g_object_unref (message);

	return success;
}

/**
 * e_webdav_session_get_supported_privilege_set_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_privileges: (out) (transfer full) (element-type EWebDAVPrivilege): return location for the tree of supported privileges
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets supported privileges for the @uri, or, in case it's %NULL, for the URI
 * defined in associated #ESource.
 *
 * The root node of @out_privileges has always %NULL data.
 *
 * Free the returned @out_privileges with e_webdav_session_util_free_privileges()
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_supported_privilege_set_sync (EWebDAVSession *webdav,
						   const gchar *uri,
						   GNode **out_privileges,
						   GCancellable *cancellable,
						   GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_privileges != NULL, FALSE);

	*out_privileges = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "supported-privilege-set");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_supported_privilege_set_cb, out_privileges, cancellable, error);

	g_object_unref (xml);

	return success;
}

static void
e_webdav_session_extract_privilege_simple (xmlNodePtr privilege_node,
					   GSList **out_privileges)
{
	if (privilege_node) {
		xmlNodePtr node;

		for (node = privilege_node->children; node; node = node->next) {
			if (node->type == XML_ELEMENT_NODE &&
			    node->name && *(node->name) &&
			    node->ns && node->ns->href && *(node->ns->href)) {
				EWebDAVPrivilege *privilege;

				privilege = e_webdav_privilege_new ((const gchar *) node->ns->href, (const gchar *) node->name,
					NULL, E_WEBDAV_PRIVILEGE_KIND_COMMON, E_WEBDAV_PRIVILEGE_HINT_UNKNOWN);

				if (privilege)
					*out_privileges = g_slist_prepend (*out_privileges, privilege);
			}
		}
	}
}

typedef struct _PrivilegeSetData {
	gboolean any_found;
	GSList **out_privileges;
} PrivilegeSetData;

static gboolean
e_webdav_session_current_user_privilege_set_cb (EWebDAVSession *webdav,
						xmlNodePtr prop_node,
						const GUri *request_uri,
						const gchar *href,
						guint status_code,
						gpointer user_data)
{
	PrivilegeSetData *psd = user_data;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (psd != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		xmlNodePtr privilege_set_node;

		privilege_set_node = e_xml_find_child (prop_node, E_WEBDAV_NS_DAV, "current-user-privilege-set");

		if (privilege_set_node) {
			xmlNodePtr privilege_node;

			psd->any_found = TRUE;

			for (privilege_node = e_xml_find_child (privilege_set_node, E_WEBDAV_NS_DAV, "privilege");
			     privilege_node;
			     privilege_node = e_xml_find_next_sibling (privilege_node, E_WEBDAV_NS_DAV, "privilege")) {
				e_webdav_session_extract_privilege_simple (privilege_node, psd->out_privileges);
			}
		}
	}

	return TRUE;
}

/**
 * e_webdav_session_get_current_user_privilege_set_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_privileges: (out) (transfer full) (element-type EWebDAVPrivilege): return location for a %GSList of #EWebDAVPrivilege
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets current user privileges for the @uri, or, in case it's %NULL, for the URI
 * defined in associated #ESource.
 *
 * Free the returned @out_privileges with
 * g_slist_free_full (privileges, e_webdav_privilege_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_current_user_privilege_set_sync (EWebDAVSession *webdav,
						      const gchar *uri,
						      GSList **out_privileges,
						      GCancellable *cancellable,
						      GError **error)
{
	return e_webdav_session_get_current_user_privilege_set_full_sync (webdav, uri, out_privileges, NULL, NULL, cancellable, error);
}

/**
 * e_webdav_session_get_current_user_privilege_set_full_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_privileges: (out) (transfer full) (element-type EWebDAVPrivilege): return location for a %GSList of #EWebDAVPrivilege
 * @out_capabilities: (out) (transfer full) (optional): return location for DAV capabilities, or %NULL
 * @out_allows: (out) (transfer full) (optional): return location for allowed operations, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets current user privileges for the @uri, or, in case it's %NULL, for the URI
 * defined in associated #ESource, with optional read of the capabilities
 * and what the user is allowed. See e_webdav_session_options_sync() for
 * more information about the @out_capabilities and @out_allows values.
 *
 * Free the returned @out_privileges with
 * g_slist_free_full (privileges, e_webdav_privilege_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_current_user_privilege_set_full_sync (EWebDAVSession *webdav,
							   const gchar *uri,
							   GSList **out_privileges,
							   GHashTable **out_capabilities,
							   GHashTable **out_allows,
							   GCancellable *cancellable,
							   GError **error)
{
	SoupMessage *message = NULL;
	EXmlDocument *xml;
	PrivilegeSetData psd;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_privileges != NULL, FALSE);

	*out_privileges = NULL;

	if (out_capabilities)
		*out_capabilities = NULL;

	if (out_allows)
		*out_allows = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "current-user-privilege-set");
	e_xml_document_end_element (xml); /* prop */

	psd.any_found = FALSE;
	psd.out_privileges = out_privileges;

	success = e_webdav_session_propfind_internal_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_current_user_privilege_set_cb, &psd, &message, cancellable, error);

	g_object_unref (xml);

	if (success && !psd.any_found) {
		success = FALSE;
		g_set_error_literal (error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND, soup_status_get_phrase (SOUP_STATUS_NOT_FOUND));
	} else if (success) {
		if (message) {
			if (out_capabilities)
				*out_capabilities = e_webdav_session_comma_header_to_hashtable (soup_message_get_response_headers (message), "DAV");

			if (out_allows)
				*out_allows = e_webdav_session_comma_header_to_hashtable (soup_message_get_response_headers (message), "Allow");
		}

		*out_privileges = g_slist_reverse (*out_privileges);
	}

	g_clear_object (&message);

	return success;
}

static gboolean
e_webdav_session_has_one_children (xmlNodePtr parent_node)
{
	xmlNodePtr node;
	gint subelements = 0;

	if (!parent_node)
		return FALSE;

	for (node = parent_node->children; node && subelements <= 1; node = node->next) {
		if (node->type == XML_ELEMENT_NODE)
			subelements++;
	}

	return subelements == 1;
}

static EWebDAVACEPrincipalKind
e_webdav_session_extract_acl_principal (xmlNodePtr principal_node,
					gchar **out_principal_href,
					GSList **out_principal_hrefs)
{
	xmlNodePtr node;

	if (!principal_node)
		return E_WEBDAV_ACE_PRINCIPAL_UNKNOWN;

	g_return_val_if_fail (out_principal_href != NULL || out_principal_hrefs != NULL, E_WEBDAV_ACE_PRINCIPAL_UNKNOWN);

	if (out_principal_href)
		*out_principal_href = NULL;

	if (out_principal_hrefs)
		*out_principal_hrefs = NULL;

	node = e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "href");

	if (node) {
		const xmlChar *href;

		href = e_xml_get_node_text (node);

		if (out_principal_href) {
			*out_principal_href = (href && *href) ? g_strdup ((const gchar *) href) : NULL;
		} else {
			for (; node; node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "href")) {

				href = e_xml_get_node_text (node);

				if (href && *href)
					*out_principal_hrefs = g_slist_prepend (*out_principal_hrefs, g_strdup ((const gchar *) href));
			}

			*out_principal_hrefs = g_slist_reverse (*out_principal_hrefs);
		}

		return E_WEBDAV_ACE_PRINCIPAL_HREF;
	}

	if (e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "all"))
		return E_WEBDAV_ACE_PRINCIPAL_ALL;

	if (e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "authenticated"))
		return E_WEBDAV_ACE_PRINCIPAL_AUTHENTICATED;

	if (e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "unauthenticated"))
		return E_WEBDAV_ACE_PRINCIPAL_UNAUTHENTICATED;

	if (e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "self"))
		return E_WEBDAV_ACE_PRINCIPAL_SELF;

	node = e_xml_find_child (principal_node, E_WEBDAV_NS_DAV, "property");

	if (node) {
		/* No details read about what properties */
		EWebDAVACEPrincipalKind kind = E_WEBDAV_ACE_PRINCIPAL_PROPERTY;

		/* Special-case owner */
		if (e_xml_find_child (node, E_WEBDAV_NS_DAV, "owner")) {
			/* DAV:owner is the only child and there is only one DAV:property child of the DAV:principal */
			if (e_webdav_session_has_one_children (node) &&
			    e_webdav_session_has_one_children (principal_node)) {
				kind = E_WEBDAV_ACE_PRINCIPAL_OWNER;
			}
		}

		return kind;
	}

	return E_WEBDAV_ACE_PRINCIPAL_UNKNOWN;
}

static gboolean
e_webdav_session_acl_cb (EWebDAVSession *webdav,
			 xmlNodePtr prop_node,
			 const GUri *request_uri,
			 const gchar *href,
			 guint status_code,
			 gpointer user_data)
{
	GSList **out_entries = user_data;
	xmlNodePtr acl_node, ace_node;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (out_entries != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;


	acl_node = e_xml_find_child (prop_node, E_WEBDAV_NS_DAV, "acl");

	if (acl_node) {
		for (ace_node = e_xml_find_child (acl_node, E_WEBDAV_NS_DAV, "ace");
		     ace_node;
		     ace_node = e_xml_find_next_sibling (ace_node, E_WEBDAV_NS_DAV, "ace")) {
			EWebDAVACEPrincipalKind principal_kind = E_WEBDAV_ACE_PRINCIPAL_UNKNOWN;
			xmlNodePtr node;
			gchar *principal_href = NULL;
			guint32 flags = E_WEBDAV_ACE_FLAG_UNKNOWN;
			gchar *inherited_href = NULL;

			node = e_xml_find_child (ace_node, E_WEBDAV_NS_DAV, "invert");

			if (node) {
				flags |= E_WEBDAV_ACE_FLAG_INVERT;

				principal_kind = e_webdav_session_extract_acl_principal (e_xml_find_child (node, E_WEBDAV_NS_DAV, "principal"), &principal_href, NULL);
			} else {
				principal_kind = e_webdav_session_extract_acl_principal (e_xml_find_child (ace_node, E_WEBDAV_NS_DAV, "principal"), &principal_href, NULL);
			}

			if (principal_kind == E_WEBDAV_ACE_PRINCIPAL_UNKNOWN)
				continue;

			if (e_xml_find_child (ace_node, E_WEBDAV_NS_DAV, "protected"))
				flags |= E_WEBDAV_ACE_FLAG_PROTECTED;

			node = e_xml_find_in_hierarchy (ace_node, E_WEBDAV_NS_DAV, "inherited", E_WEBDAV_NS_DAV, "href", NULL, NULL);

			if (node) {
				flags |= E_WEBDAV_ACE_FLAG_INHERITED;
				inherited_href = g_strdup ((const gchar *) e_xml_get_node_text (node));
			}

			node = e_xml_find_child (ace_node, E_WEBDAV_NS_DAV, "grant");

			if (node) {
				flags |= E_WEBDAV_ACE_FLAG_GRANT;
			} else {
				node = e_xml_find_child (ace_node, E_WEBDAV_NS_DAV, "deny");

				if (node)
					flags |= E_WEBDAV_ACE_FLAG_DENY;
			}

			if (node) {
				EWebDAVAccessControlEntry *ace;

				ace = e_webdav_access_control_entry_new	(principal_kind, principal_href, flags, inherited_href);

				if (ace) {
					for (node = e_xml_find_child (node, E_WEBDAV_NS_DAV, "privilege");
					     node;
					     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "privilege")) {
						e_webdav_session_extract_privilege_simple (node, &ace->privileges);
					}

					ace->privileges = g_slist_reverse (ace->privileges);

					*out_entries = g_slist_prepend (*out_entries, ace);
				}
			}

			g_free (principal_href);
			g_free (inherited_href);
		}
	}

	return TRUE;
}

/**
 * e_webdav_session_get_acl_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_entries: (out) (transfer full) (element-type EWebDAVAccessControlEntry): return location for a #GSList of #EWebDAVAccessControlEntry
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets Access Control List (ACL) for the @uri, or, in case it's %NULL, for the URI
 * defined in associated #ESource.
 *
 * This function doesn't read general #E_WEBDAV_ACE_PRINCIPAL_PROPERTY.
 *
 * Free the returned @out_entries with
 * g_slist_free_full (entries, e_webdav_access_control_entry_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_acl_sync (EWebDAVSession *webdav,
			       const gchar *uri,
			       GSList **out_entries,
			       GCancellable *cancellable,
			       GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_entries != NULL, FALSE);

	*out_entries = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "acl");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_acl_cb, out_entries, cancellable, error);

	g_object_unref (xml);

	if (success)
		*out_entries = g_slist_reverse (*out_entries);

	return success;
}

typedef struct _ACLRestrictionsData {
	guint32 *out_restrictions;
	EWebDAVACEPrincipalKind *out_principal_kind;
	GSList **out_principal_hrefs;
} ACLRestrictionsData;

static gboolean
e_webdav_session_acl_restrictions_cb (EWebDAVSession *webdav,
				      xmlNodePtr prop_node,
				      const GUri *request_uri,
				      const gchar *href,
				      guint status_code,
				      gpointer user_data)
{
	ACLRestrictionsData *ard = user_data;
	xmlNodePtr acl_restrictions_node;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (ard != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;

	acl_restrictions_node = e_xml_find_child (prop_node, E_WEBDAV_NS_DAV, "acl-restrictions");

	if (acl_restrictions_node) {
		xmlNodePtr required_principal;

		if (e_xml_find_child (acl_restrictions_node, E_WEBDAV_NS_DAV, "grant-only"))
			*ard->out_restrictions |= E_WEBDAV_ACL_RESTRICTION_GRANT_ONLY;

		if (e_xml_find_child (acl_restrictions_node, E_WEBDAV_NS_DAV, "no-invert"))
			*ard->out_restrictions |= E_WEBDAV_ACL_RESTRICTION_NO_INVERT;

		if (e_xml_find_child (acl_restrictions_node, E_WEBDAV_NS_DAV, "deny-before-grant"))
			*ard->out_restrictions |= E_WEBDAV_ACL_RESTRICTION_DENY_BEFORE_GRANT;

		required_principal = e_xml_find_child (acl_restrictions_node, E_WEBDAV_NS_DAV, "required-principal");

		if (required_principal) {
			*ard->out_restrictions |= E_WEBDAV_ACL_RESTRICTION_REQUIRED_PRINCIPAL;
			*ard->out_principal_kind = e_webdav_session_extract_acl_principal (required_principal, NULL, ard->out_principal_hrefs);
		}
	}

	return TRUE;
}

/**
 * e_webdav_session_get_acl_restrictions_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_restrictions: (out): return location for bit-or of #EWebDAVACLRestrictions
 * @out_principal_kind: (out): return location for principal kind
 * @out_principal_hrefs: (out) (transfer full) (element-type utf8): return location for a #GSList of principal href-s
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets Access Control List (ACL) restrictions for the @uri, or, in case it's %NULL,
 * for the URI defined in associated #ESource. The @out_principal_kind is valid only
 * if the @out_restrictions contains #E_WEBDAV_ACL_RESTRICTION_REQUIRED_PRINCIPAL.
 * The @out_principal_hrefs is valid only if the @out_principal_kind is valid and when
 * it is #E_WEBDAV_ACE_PRINCIPAL_HREF.
 *
 * Free the returned @out_principal_hrefs with
 * g_slist_free_full (entries, g_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_acl_restrictions_sync (EWebDAVSession *webdav,
					    const gchar *uri,
					    guint32 *out_restrictions,
					    EWebDAVACEPrincipalKind *out_principal_kind,
					    GSList **out_principal_hrefs,
					    GCancellable *cancellable,
					    GError **error)
{
	ACLRestrictionsData ard;
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_restrictions != NULL, FALSE);
	g_return_val_if_fail (out_principal_kind != NULL, FALSE);
	g_return_val_if_fail (out_principal_hrefs != NULL, FALSE);

	*out_restrictions = E_WEBDAV_ACL_RESTRICTION_NONE;
	*out_principal_kind = E_WEBDAV_ACE_PRINCIPAL_UNKNOWN;
	*out_principal_hrefs = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "acl-restrictions");
	e_xml_document_end_element (xml); /* prop */

	ard.out_restrictions = out_restrictions;
	ard.out_principal_kind = out_principal_kind;
	ard.out_principal_hrefs = out_principal_hrefs;

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_acl_restrictions_cb, &ard, cancellable, error);

	g_object_unref (xml);

	return success;
}

static gboolean
e_webdav_session_principal_collection_set_cb (EWebDAVSession *webdav,
					      xmlNodePtr prop_node,
					      const GUri *request_uri,
					      const gchar *href,
					      guint status_code,
					      gpointer user_data)
{
	GSList **out_principal_hrefs = user_data;
	xmlNodePtr principal_collection_set;

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (out_principal_hrefs != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;

	principal_collection_set = e_xml_find_child (prop_node, E_WEBDAV_NS_DAV, "principal-collection-set");

	if (principal_collection_set) {
		xmlNodePtr node;

		for (node = e_xml_find_child (principal_collection_set, E_WEBDAV_NS_DAV, "href");
		     node;
		     node = e_xml_find_next_sibling (node, E_WEBDAV_NS_DAV, "href")) {
			const xmlChar *got_href;

			got_href = e_xml_get_node_text (node);

			if (got_href && *got_href)
				*out_principal_hrefs = g_slist_prepend (*out_principal_hrefs, g_strdup ((const gchar *) got_href));
		}
	}

	return TRUE;
}

/**
 * e_webdav_session_get_principal_collection_set_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @out_principal_hrefs: (out) (transfer full) (element-type utf8): return location for a #GSList of principal href-s
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Gets list of principal collection href for the @uri, or, in case it's %NULL,
 * for the URI defined in associated #ESource. The @out_principal_hrefs are root
 * collections that contain the principals that are available on the server that
 * implements this resource.
 *
 * Free the returned @out_principal_hrefs with
 * g_slist_free_full (entries, g_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_get_principal_collection_set_sync (EWebDAVSession *webdav,
						    const gchar *uri,
						    GSList **out_principal_hrefs, /* gchar * */
						    GCancellable *cancellable,
						    GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_principal_hrefs != NULL, FALSE);

	*out_principal_hrefs = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "principal-collection-set");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_propfind_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_principal_collection_set_cb, out_principal_hrefs, cancellable, error);

	g_object_unref (xml);

	if (success)
		*out_principal_hrefs = g_slist_reverse (*out_principal_hrefs);

	return success;
}

/**
 * e_webdav_session_set_acl_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @entries: (element-type EWebDAVAccessControlEntry): entries to write
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Changes Access Control List (ACL) for the @uri, or, in case it's %NULL,
 * for the URI defined in associated #ESource.
 *
 * Make sure that the @entries satisfy ACL restrictions, as returned
 * by e_webdav_session_get_acl_restrictions_sync(). The order in the @entries
 * is preserved. It cannot contain any %E_WEBDAV_ACE_FLAG_PROTECTED,
 * nor @E_WEBDAV_ACE_FLAG_INHERITED, items.
 *
 * Use e_webdav_session_get_acl_sync() to read currently known ACL entries,
 * remove from the list those protected and inherited, and then modify
 * the rest with the required changed.
 *
 * Note this function doesn't support general %E_WEBDAV_ACE_PRINCIPAL_PROPERTY and
 * returns %G_IO_ERROR_NOT_SUPPORTED error when any such is tried to be written.
 *
 * In case the returned entries contain any %E_WEBDAV_ACE_PRINCIPAL_PROPERTY,
 * or there's a need to write such Access Control Entry, then do not use
 * e_webdav_session_get_acl_sync(), neither e_webdav_session_set_acl_sync(),
 * and write more generic implementation.
 *
 * Returns: Whether succeeded.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_set_acl_sync (EWebDAVSession *webdav,
			       const gchar *uri,
			       const GSList *entries,
			       GCancellable *cancellable,
			       GError **error)
{
	EXmlDocument *xml;
	GSList *link, *plink;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (entries != NULL, FALSE);

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "acl");
	g_return_val_if_fail (xml != NULL, FALSE);

	for (link = (GSList *) entries; link; link = g_slist_next (link)) {
		EWebDAVAccessControlEntry *ace = link->data;

		if (!ace) {
			g_warn_if_fail (ace != NULL);
			g_object_unref (xml);
			return FALSE;
		}

		if ((ace->flags & E_WEBDAV_ACE_FLAG_PROTECTED) != 0 ||
		    (ace->flags & E_WEBDAV_ACE_FLAG_INHERITED) != 0) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				_("Cannot store protected nor inherited Access Control Entry."));
			g_object_unref (xml);
			return FALSE;
		}

		if (ace->principal_kind == E_WEBDAV_ACE_PRINCIPAL_UNKNOWN) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				_("Provided invalid principal kind for Access Control Entry."));
			g_object_unref (xml);
			return FALSE;
		}

		if (ace->principal_kind == E_WEBDAV_ACE_PRINCIPAL_PROPERTY) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				_("Cannot store property-based Access Control Entry."));
			g_object_unref (xml);
			return FALSE;
		}

		if ((ace->flags & (E_WEBDAV_ACE_FLAG_GRANT | E_WEBDAV_ACE_FLAG_DENY)) == 0) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				_("Access Control Entry can be only to Grant or Deny, but not None."));
			g_object_unref (xml);
			return FALSE;
		}

		if ((ace->flags & E_WEBDAV_ACE_FLAG_GRANT) != 0 &&
		    (ace->flags & E_WEBDAV_ACE_FLAG_DENY) != 0) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				_("Access Control Entry can be only to Grant or Deny, but not both."));
			g_object_unref (xml);
			return FALSE;
		}

		e_xml_document_start_element (xml, NULL, "ace");

		if ((ace->flags & E_WEBDAV_ACE_FLAG_INVERT) != 0)
			e_xml_document_start_element (xml, NULL, "invert");

		e_xml_document_start_element (xml, NULL, "principal");
		switch (ace->principal_kind) {
		case E_WEBDAV_ACE_PRINCIPAL_UNKNOWN:
			g_warn_if_reached ();
			break;
		case E_WEBDAV_ACE_PRINCIPAL_HREF:
			e_xml_document_start_text_element (xml, NULL, "href");
			e_xml_document_write_string (xml, ace->principal_href);
			e_xml_document_end_element (xml);
			break;
		case E_WEBDAV_ACE_PRINCIPAL_ALL:
			e_xml_document_add_empty_element (xml, NULL, "all");
			break;
		case E_WEBDAV_ACE_PRINCIPAL_AUTHENTICATED:
			e_xml_document_add_empty_element (xml, NULL, "authenticated");
			break;
		case E_WEBDAV_ACE_PRINCIPAL_UNAUTHENTICATED:
			e_xml_document_add_empty_element (xml, NULL, "unauthenticated");
			break;
		case E_WEBDAV_ACE_PRINCIPAL_PROPERTY:
			g_warn_if_reached ();
			break;
		case E_WEBDAV_ACE_PRINCIPAL_SELF:
			e_xml_document_add_empty_element (xml, NULL, "self");
			break;
		case E_WEBDAV_ACE_PRINCIPAL_OWNER:
			e_xml_document_start_element (xml, NULL, "property");
			e_xml_document_add_empty_element (xml, NULL, "owner");
			e_xml_document_end_element (xml);
			break;

		}
		e_xml_document_end_element (xml); /* principal */

		if ((ace->flags & E_WEBDAV_ACE_FLAG_INVERT) != 0)
			e_xml_document_end_element (xml); /* invert */

		if ((ace->flags & E_WEBDAV_ACE_FLAG_GRANT) != 0)
			e_xml_document_start_element (xml, NULL, "grant");
		else if ((ace->flags & E_WEBDAV_ACE_FLAG_DENY) != 0)
			e_xml_document_start_element (xml, NULL, "deny");
		else
			g_warn_if_reached ();

		for (plink = ace->privileges; plink; plink = g_slist_next (plink)) {
			EWebDAVPrivilege *privilege = plink->data;

			if (!privilege) {
				g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
					_("Access Control Entry privilege cannot be NULL."));
				g_object_unref (xml);
				return FALSE;
			}

			e_xml_document_start_element (xml, NULL, "privilege");
			e_xml_document_add_empty_element (xml, privilege->ns_uri, privilege->name);
			e_xml_document_end_element (xml); /* privilege */
		}

		e_xml_document_end_element (xml); /* grant or deny */

		e_xml_document_end_element (xml); /* ace */
	}

	success = e_webdav_session_acl_sync (webdav, uri, xml, cancellable, error);

	g_object_unref (xml);

	return success;
}

static gboolean
e_webdav_session_principal_property_search_cb (EWebDAVSession *webdav,
					       xmlNodePtr prop_node,
					       const GUri *request_uri,
					       const gchar *href,
					       guint status_code,
					       gpointer user_data)
{
	GSList **out_principals = user_data;
	EWebDAVResource *resource;
	gchar *display_name;

	g_return_val_if_fail (out_principals != NULL, FALSE);

	if (status_code != SOUP_STATUS_OK)
		return TRUE;

	display_name = e_webdav_session_extract_nonempty (prop_node, E_WEBDAV_NS_DAV, "displayname", NULL, NULL);

	resource = e_webdav_resource_new (
		E_WEBDAV_RESOURCE_KIND_PRINCIPAL,
		0, /* supports */
		href,
		NULL, /* etag */
		NULL, /* display_name */
		NULL, /* content_type */
		0, /* content_length */
		0, /* creation_date */
		0, /* last_modified */
		NULL, /* description */
		NULL, /* color */
		(guint) -1); /* order */
	resource->display_name = display_name;

	*out_principals = g_slist_prepend (*out_principals, resource);

	return TRUE;
}

/**
 * e_webdav_session_principal_property_search_sync:
 * @webdav: an #EWebDAVSession
 * @uri: (nullable): URI to issue the request for, or %NULL to read from #ESource
 * @apply_to_principal_collection_set: whether to apply to principal-collection-set
 * @match_ns_uri: (nullable): namespace URI of the property to search in, or %NULL for %E_WEBDAV_NS_DAV
 * @match_property: name of the property to search in
 * @match_value: a string value to search for
 * @out_principals: (out) (transfer full) (element-type EWebDAVResource): return location for matching principals
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Issues a DAV:principal-property-search for the @uri, or, in case it's %NULL,
 * for the URI defined in associated #ESource. The DAV:principal-property-search
 * performs a search for all principals whose properties contain character data
 * that matches the search criteria @match_value in @match_property property
 * of namespace @match_ns_uri.
 *
 * By default, the function searches all members (at any depth) of the collection
 * identified by the @uri. If @apply_to_principal_collection_set is set to %TRUE,
 * the search is applied instead to each collection returned by
 * e_webdav_session_get_principal_collection_set_sync() for the @uri.
 *
 * The @out_principals is a #GSList of #EWebDAVResource, where the kind
 * is set to %E_WEBDAV_RESOURCE_KIND_PRINCIPAL and only href with displayname
 * are filled. All other members of #EWebDAVResource are not set.
 *
 * Free the returned @out_principals with
 * g_slist_free_full (principals, e_webdav_resource_free);
 * when no longer needed.
 *
 * Returns: Whether succeeded. Note it can report success also when no matching
 *    principal had been found.
 *
 * Since: 3.26
 **/
gboolean
e_webdav_session_principal_property_search_sync (EWebDAVSession *webdav,
						 const gchar *uri,
						 gboolean apply_to_principal_collection_set,
						 const gchar *match_ns_uri,
						 const gchar *match_property,
						 const gchar *match_value,
						 GSList **out_principals,
						 GCancellable *cancellable,
						 GError **error)
{
	EXmlDocument *xml;
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (match_property != NULL, FALSE);
	g_return_val_if_fail (match_value != NULL, FALSE);
	g_return_val_if_fail (out_principals != NULL, FALSE);

	*out_principals = NULL;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "principal-property-search");
	g_return_val_if_fail (xml != NULL, FALSE);

	if (apply_to_principal_collection_set) {
		e_xml_document_add_empty_element (xml, NULL, "apply-to-principal-collection-set");
	}

	e_xml_document_start_element (xml, NULL, "property-search");
	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, match_ns_uri, match_property);
	e_xml_document_end_element (xml); /* prop */
	e_xml_document_start_text_element (xml, NULL, "match");
	e_xml_document_write_string (xml, match_value);
	e_xml_document_end_element (xml); /* match */
	e_xml_document_end_element (xml); /* property-search */

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "displayname");
	e_xml_document_end_element (xml); /* prop */

	success = e_webdav_session_report_sync (webdav, uri, E_WEBDAV_DEPTH_THIS, xml,
		e_webdav_session_principal_property_search_cb, out_principals, NULL, NULL, cancellable, error);

	g_object_unref (xml);

	if (success)
		*out_principals = g_slist_reverse (*out_principals);

	return success;
}

/**
 * e_webdav_session_util_maybe_dequote:
 * @text: (inout) (nullable): text to dequote
 *
 * Dequotes @text, if it's enclosed in double-quotes. The function
 * changes @text, it doesn't allocate new string. The function does
 * nothing when the @text is not enclosed in double-quotes.
 *
 * Returns: possibly dequoted @text
 *
 * Since: 3.26
 **/
gchar *
e_webdav_session_util_maybe_dequote (gchar *text)
{
	gint len;

	if (!text || *text != '\"')
		return text;

	len = strlen (text);

	if (len < 2 || text[len - 1] != '\"')
		return text;

	memmove (text, text + 1, len - 2);
	text[len - 2] = '\0';

	return text;
}

static gboolean
e_webdav_session_free_in_traverse_cb (GNode *node,
				      gpointer user_data)
{
	if (node) {
		e_webdav_privilege_free (node->data);
		node->data = NULL;
	}

	return FALSE;
}

/**
 * e_webdav_session_util_free_privileges:
 * @privileges: (nullable): a tree of #EWebDAVPrivilege structures
 *
 * Frees @privileges returned by e_webdav_session_get_supported_privilege_set_sync().
 * The function does nothing, if @privileges is %NULL.
 *
 * Since: 3.26
 **/
void
e_webdav_session_util_free_privileges (GNode *privileges)
{
	if (!privileges)
		return;

	g_node_traverse (privileges, G_PRE_ORDER, G_TRAVERSE_ALL, -1, e_webdav_session_free_in_traverse_cb, NULL);
	g_node_destroy (privileges);
}

static gint
e_webdav_session_uricmp (const gchar *str1,
			 gint len1,
			 const gchar *str2,
			 gint len2,
			 gboolean case_sensitive)
{
	const gchar *p1, *p2;
	gchar c1, c2;

	g_return_val_if_fail (str1 != NULL, -1);
	g_return_val_if_fail (len1 >= 0, -1);
	g_return_val_if_fail (str2 != NULL, -1);
	g_return_val_if_fail (len2 >= 0, -1);

	if (!len1 && !len2)
		return 0;

	/* Decode %-encoded letters, if needed */
	#define get_next_char(str, ll, cc) G_STMT_START { \
		if (!*str) { \
			cc = 0; \
		} else if (*str == '%' && ll >= 2 && g_ascii_isxdigit (str[1]) && g_ascii_isxdigit (str[2])) { \
			cc = ((str[1] >= '0' && str[1] <= '9') ? (str[1] - '0') : \
			      (str[1] >= 'a' && str[1] <= 'f') ? (str[1] - 'a' + 10) : \
			      (str[1] >= 'A' && str[1] <= 'F') ? (str[1] - 'A' + 10) : 0) * 16 + \
			     ((str[2] >= '0' && str[2] <= '9') ? (str[2] - '0') : \
			      (str[2] >= 'a' && str[2] <= 'f') ? (str[2] - 'a' + 10) : \
			      (str[2] >= 'A' && str[2] <= 'F') ? (str[2] - 'A' + 10) : 0); \
			str += 3; \
			ll -= 3; \
		} else { \
			cc = *str; \
			str++; \
			ll--; \
		} \
	} G_STMT_END

	p1 = str1;
	p2 = str2;

	c1 = *p1;
	c2 = *p2;

	while (len1 > 0 && len2 > 0 && *p1 && *p2) {
		get_next_char (p1, len1, c1);
		get_next_char (p2, len2, c2);

		if ((case_sensitive && c1 != c2) || (!case_sensitive && g_ascii_tolower (c1) != g_ascii_tolower (c2)))
			return c1 - c2;
	}

	#undef get_next_char

	if (!len1 || !*p1)
		c1 = 0;

	if (!len2 || !*p2)
		c2 = 0;

	return c1 - c2;
}

/**
 * e_webdav_session_util_item_href_equal:
 * @href1: the first href
 * @href2: the second href
 *
 * Compares two hrefs and return whether they reference
 * the same item on the server. The comparison is done in
 * a relaxed way, not considering scheme part and comparing
 * the host name case insensitively, while the path
 * case sensitively. It also ignores the username/password
 * information in the hostname part, if it's included.
 * The function doesn't decode any URI-encoded characters.
 *
 * Returns: whether the two href-s reference the same item
 *
 * Since: 3.40
 **/
gboolean
e_webdav_session_util_item_href_equal (const gchar *href1,
				       const gchar *href2)
{
	const gchar *ptr, *from1, *from2, *next1, *next2;

	if (!href1 || !href2)
		return href1 == href2;

	if (g_strcmp0 (href1, href2) == 0)
		return TRUE;

	/* skip the scheme part */
	ptr = strstr (href1, "://");
	if (ptr)
		href1 = ptr + 3;

	ptr = strstr (href2, "://");
	if (ptr)
		href2 = ptr + 3;

	for (from1 = href1, from2 = href2; from1 && from2; from1 = next1, from2 = next2) {
		gint len1, len2;

		ptr = strchr (from1, '/');
		if (ptr)
			ptr++;
		next1 = ptr;

		ptr = strchr (from2, '/');
		if (ptr)
			ptr++;
		next2 = ptr;

		if ((!next1 && next2) || (next1 && !next2))
			break;

		len1 = next1 ? next1 - from1 : strlen (from1);
		len2 = next2 ? next2 - from2 : strlen (from2);

		/* it's the hostname part */
		if (from1 == href1) {
			const gchar *dash;

			/* ignore the username/password part */
			ptr = strchr (from1, '@');
			dash = strchr (from1, '/');
			if (ptr && (!dash || dash > ptr)) {
				len1 = len1 - (ptr - from1 + 1);
				from1 = ptr + 1;
			}

			ptr = strchr (from2, '@');
			dash = strchr (from2, '/');
			if (ptr && (!dash || dash > ptr)) {
				len2 = len2 - (ptr - from2 + 1);
				from2 = ptr + 1;
			}

			if (e_webdav_session_uricmp (from1, len1, from2, len2, FALSE) != 0)
				return FALSE;
		} else if (e_webdav_session_uricmp (from1, len1, from2, len2, TRUE) != 0) {
			return FALSE;
		}
	}

	return !from1 && !from2;
}

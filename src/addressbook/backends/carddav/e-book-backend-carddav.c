/*
 * Copyright (C) 2008 Matthias Braun <matze@braunis.de>
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
 * Authors: Matthias Braun <matze@braunis.de>
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include "libedataserver/libedataserver.h"

#include "e-book-backend-carddav.h"

#define E_WEBDAV_MAX_MULTIGET_AMOUNT 100 /* what's the maximum count of items to fetch within a multiget request */

#define E_WEBDAV_X_ETAG "X-EVOLUTION-WEBDAV-ETAG"
#define E_WEBDAV_X_IMG_URL "X-EVOLUTION-WEBDAV-IMG-URL"
#define E_GOOGLE_ANNIVERSARY_ITEM "X-EVOLUTION-GOOGLE-ANNIVERSARY-ITEM"

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg) e_client_error_create (_code, _msg)
#define EBC_ERROR(_code) e_book_client_error_create (_code, NULL)

struct _EBookBackendCardDAVPrivate {
	/* The main WebDAV session  */
	EWebDAVSession *webdav;
	GUri *last_uri;
	GMutex webdav_lock;

	EVCardVersion vcard_version; /* the highest supported vCard version the server can work with */

	/* If already been connected, then the connect_sync() will relax server checks,
	   to avoid unnecessary requests towards the server. */
	gboolean been_connected;

	/* support for 'getctag' extension */
	gboolean ctag_supported;

	/* Whether talking to the Google server */
	gboolean is_google;
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendCardDAV, e_book_backend_carddav, E_TYPE_BOOK_META_BACKEND)

static void
ebb_carddav_debug_print (const gchar *format,
			 ...) G_GNUC_PRINTF (1, 2);

static void
ebb_carddav_debug_print (const gchar *format,
			 ...)
{
	static gint debug_enabled = -1;
	va_list args;

	if (debug_enabled == -1)
		debug_enabled = g_strcmp0 (g_getenv ("CARDDAV_DEBUG"), "0") != 0 ? 1 : 0;

	if (!debug_enabled)
		return;

	va_start (args, format);
	e_util_debug_printv ("CardDAV", format, args);
	va_end (args);
}

static gboolean
ebb_carddav_finish_load_photologo (EBookBackendCardDAV *bbdav,
				   EWebDAVSession *webdav,
				   EVCardAttribute *attr,
				   GCancellable *cancellable,
				   gpointer user_data)
{
	GList *values;
	gboolean can_continue = TRUE;

	if (!webdav)
		return can_continue;

	values = e_vcard_attribute_get_param (attr, EVC_VALUE);
	if (values && g_ascii_strcasecmp (values->data, "uri") == 0) {
		gchar *uri;

		uri = e_vcard_attribute_get_value (attr);
		if (uri && (
		    g_ascii_strncasecmp (uri, "http://", 7) == 0 ||
		    g_ascii_strncasecmp (uri, "https://", 8) == 0)) {
			gchar *bytes = NULL;
			gsize length = 0;
			GError *local_error = NULL;

			if (e_webdav_session_get_data_sync (webdav, uri, NULL, NULL, NULL, &bytes, &length, cancellable, &local_error) && bytes) {
				if (length > 0) {
					gchar *mime_type = NULL, *content_type;
					const gchar *image_type, *pp;

					content_type = g_content_type_guess (uri, (const guchar *) bytes, length, NULL);

					if (content_type)
						mime_type = g_content_type_get_mime_type (content_type);

					g_free (content_type);

					if (mime_type && (pp = strchr (mime_type, '/'))) {
						image_type = pp + 1;
					} else {
						image_type = "X-EVOLUTION-UNKNOWN";
					}

					e_vcard_attribute_remove_param (attr, EVC_TYPE);
					e_vcard_attribute_remove_param (attr, EVC_ENCODING);
					e_vcard_attribute_remove_param (attr, EVC_VALUE);
					e_vcard_attribute_remove_param (attr, E_WEBDAV_X_IMG_URL);
					e_vcard_attribute_remove_values (attr);

					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), image_type);
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_ENCODING), "b");
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (E_WEBDAV_X_IMG_URL), uri);
					e_vcard_attribute_add_value_decoded (attr, bytes, length);

					g_free (mime_type);
				}
			} else {
				ebb_carddav_debug_print ("Failed to download '%s': %s\n", uri, local_error ? local_error->message : "Unknown error");
				can_continue = !g_cancellable_is_cancelled (cancellable);
			}

			g_clear_error (&local_error);
			g_free (bytes);
		}

		g_free (uri);
	}

	return can_continue;
}

static gboolean
ebb_carddav_prepare_save_photologo (EBookBackendCardDAV *bbdav,
				    EWebDAVSession *webdav,
				    EVCardAttribute *attr,
				    GCancellable *cancellable,
				    gpointer user_data)
{
	GList *values;

	values = e_vcard_attribute_get_param (attr, EVC_ENCODING);
	if (values && (g_ascii_strcasecmp (values->data, "b") == 0 || g_ascii_strcasecmp (values->data, "base64") == 0)) {
		values = e_vcard_attribute_get_param (attr, E_WEBDAV_X_IMG_URL);
		if (values) {
			const gchar *const_uri = values->data;

			if (const_uri && (
			    g_ascii_strncasecmp (const_uri, "http://", 7) == 0 ||
			    g_ascii_strncasecmp (const_uri, "https://", 8) == 0)) {
				gchar *uri = g_strdup (g_steal_pointer (&const_uri));
				e_vcard_attribute_remove_param (attr, EVC_TYPE);
				e_vcard_attribute_remove_param (attr, EVC_ENCODING);
				e_vcard_attribute_remove_param (attr, EVC_VALUE);
				e_vcard_attribute_remove_param (attr, E_WEBDAV_X_IMG_URL);
				e_vcard_attribute_remove_values (attr);

				/* return back the original URI */
				e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_VALUE), "uri");
				e_vcard_attribute_add_value_take (attr, g_steal_pointer (&uri));
			}
		}
	}

	return TRUE;
}

static void
ebb_carddav_foreach_photologo (EBookBackendCardDAV *bbdav,
			       EContact *contact,
			       EWebDAVSession *webdav,
			       GCancellable *cancellable,
			       gboolean (* func) (EBookBackendCardDAV *bbdav,
						  EWebDAVSession *webdav,
						  EVCardAttribute *attr,
						  GCancellable *cancellable,
						  gpointer user_data),
			       gpointer user_data)
{
	GList *link;

	for (link = e_vcard_get_attributes (E_VCARD (contact)); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		if (!e_vcard_attribute_get_name (attr))
			continue;

		if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_PHOTO) == 0 ||
		    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_LOGO) == 0) {
			if (!func (bbdav, webdav, attr, cancellable, user_data))
				break;
		}
	}
}

static EContact *
ebb_carddav_contact_from_string (EBookBackendCardDAV *bbdav,
				 const gchar *vcard_str,
				 EWebDAVSession *webdav,
				 GCancellable *cancellable)
{
	EContact *contact;

	if (!vcard_str)
		return NULL;

	contact = e_contact_new_from_vcard (vcard_str);
	if (!contact)
		return NULL;

	if (bbdav->priv->is_google) {
		/* The anniversary field is stored as a significant date, which is a non-standard thing */
		EContactDate *dt;

		dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
		if (!dt) {
			GList *attrs, *link;
			EVCardAttribute *first_ablabel = NULL, *picked_ablabel = NULL;

			attrs = e_vcard_get_attributes (E_VCARD (contact));

			for (link = attrs; link; link = g_list_next (link)) {
				EVCardAttribute *attr = link->data;

				if (e_vcard_attribute_get_group (attr) &&
				    e_vcard_attribute_get_name (attr) &&
				    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), "X-ABLabel") == 0 &&
				    g_ascii_strncasecmp (e_vcard_attribute_get_group (attr), "item", 4) == 0) {
					GString *value;

					if (!first_ablabel)
						first_ablabel = attr;

					value = e_vcard_attribute_get_value_decoded (attr);
					if (value && (e_util_utf8_strstrcase (value->str, "Anniversary") ||
					    e_util_utf8_strstrcase (value->str, _("Anniversary")))) {
						picked_ablabel = attr;
						g_string_free (value, TRUE);
						break;
					}

					if (value)
						g_string_free (value, TRUE);
				}
			}

			if (!picked_ablabel)
				picked_ablabel = first_ablabel;

			if (picked_ablabel) {
				EVCardAttribute *picked_abdate = NULL;

				for (link = attrs; link; link = g_list_next (link)) {
					EVCardAttribute *attr = link->data;

					if (e_vcard_attribute_get_group (attr) &&
					    e_vcard_attribute_get_name (attr) &&
					    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), "X-ABDATE") == 0 &&
					    g_ascii_strcasecmp (e_vcard_attribute_get_group (attr), e_vcard_attribute_get_group (picked_ablabel)) == 0) {
						picked_abdate = attr;
						break;
					}
				}

				if (picked_abdate) {
					GString *value;

					value = e_vcard_attribute_get_value_decoded (picked_abdate);
					if (value) {
						dt = e_contact_date_from_string (value->str);

						if (dt && dt->year != 0 && dt->month != 0 && dt->day != 0) {
							e_contact_set (contact, E_CONTACT_ANNIVERSARY, dt);

							e_vcard_util_set_x_attribute (E_VCARD (contact),
								E_GOOGLE_ANNIVERSARY_ITEM,
								e_vcard_attribute_get_group (picked_abdate));
						}

						g_clear_pointer (&dt, e_contact_date_free);
					}
					if (value)
						g_string_free (value, TRUE);
				}
			}
		}

		if (dt)
			e_contact_date_free (dt);
	}

	ebb_carddav_foreach_photologo (bbdav, contact, webdav, cancellable, ebb_carddav_finish_load_photologo, NULL);

	return contact;
}

static EContact * /* (transfer full) */
ebb_carddav_prepare_save (EBookBackendCardDAV *bbdav,
			  /* const */ EContact *in_contact,
			  GCancellable *cancellable)
{
	EContact *contact;
	EVCard *vcard;

	if (!in_contact)
		return NULL;

	contact = e_contact_duplicate (in_contact);
	vcard = E_VCARD (contact);

	if (bbdav->priv->is_google) {
		EContactDate *dt;

		dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
		if (dt) {
			GList *attrs, *link;
			gchar *item;
			gboolean found = FALSE;

			attrs = e_vcard_get_attributes (vcard);
			item = e_vcard_util_dup_x_attribute (vcard, E_GOOGLE_ANNIVERSARY_ITEM);
			if (item) {
				/* Write the anniversary back to the value it was taken from */
				for (link = attrs; link; link = g_list_next (link)) {
					EVCardAttribute *attr = link->data;

					if (e_vcard_attribute_get_group (attr) &&
					    e_vcard_attribute_get_name (attr) &&
					    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), "X-ABDATE") == 0 &&
					    g_ascii_strcasecmp (e_vcard_attribute_get_group (attr), item) == 0) {
						gchar *value;

						found = TRUE;

						value = g_strdup_printf ("%04u-%02u-%02u", dt->year, dt->month, dt->day);
						e_vcard_attribute_remove_values (attr);
						e_vcard_attribute_add_value_take (attr, g_steal_pointer (&value));

						break;
					}
				}
			}

			if (!found) {
				EVCardAttribute *attr;
				guint highest_item = 0;
				gchar *group, *value;

				for (link = attrs; link; link = g_list_next (link)) {
					attr = link->data;

					if (e_vcard_attribute_get_group (attr) &&
					    e_vcard_attribute_get_name (attr) &&
					    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), "X-ABDATE") == 0 &&
					    g_ascii_strncasecmp (e_vcard_attribute_get_group (attr), "item", 4) == 0) {
						const gchar *grp;
						guint num;

						grp = e_vcard_attribute_get_group (attr);
						num = g_ascii_strtoull (grp + 4, NULL, 10);

						if (num > highest_item)
							highest_item = num;
					}
				}

				group = g_strdup_printf ("item%u", highest_item + 1);
				value = g_strdup_printf ("%04u-%02u-%02u", dt->year, dt->month, dt->day);

				e_vcard_append_attribute_with_value_take (
					vcard,
					e_vcard_attribute_new (group, "X-ABDate"),
					g_steal_pointer (&value));
				e_vcard_append_attribute_with_value (
					vcard,
					e_vcard_attribute_new (group, "X-ABLabel"),
					_("Anniversary"));

				g_free (group);
			}

			g_free (item);
			e_contact_date_free (dt);
		} else {
			gchar *item;

			item = e_vcard_util_dup_x_attribute (vcard, E_GOOGLE_ANNIVERSARY_ITEM);
			if (item) {
				/* user unset anniversary, remove it also for Google */
				e_vcard_remove_attributes (vcard, item, "X-ABDATE");
				e_vcard_remove_attributes (vcard, item, "X-ABLabel");
			}

			g_free (item);
		}

		/* remove evolution-specific attributes */
		e_contact_set (contact, E_CONTACT_ANNIVERSARY, NULL);
		e_vcard_util_set_x_attribute (vcard, E_GOOGLE_ANNIVERSARY_ITEM, NULL);
	}

	ebb_carddav_foreach_photologo (bbdav, contact, NULL, cancellable, ebb_carddav_prepare_save_photologo, NULL);

	e_vcard_util_set_x_attribute (vcard, E_WEBDAV_X_ETAG, NULL);

	return contact;
}

static EWebDAVSession *
ebb_carddav_ref_session (EBookBackendCardDAV *bbdav)
{
	EWebDAVSession *webdav;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (bbdav), NULL);

	g_mutex_lock (&bbdav->priv->webdav_lock);
	if (bbdav->priv->webdav)
		webdav = g_object_ref (bbdav->priv->webdav);
	else
		webdav = NULL;
	g_mutex_unlock (&bbdav->priv->webdav_lock);

	return webdav;
}

typedef struct _CtagVersionData {
	gchar **out_ctag;
	EVCardVersion *out_vcard_version;
} CtagVersionData;

static gboolean
ebb_carddav_getctag_and_version_cb (EWebDAVSession *webdav,
				    xmlNodePtr prop_node,
				    const GUri *request_uri,
				    const gchar *href,
				    guint status_code,
				    gpointer user_data)
{
	CtagVersionData *data = user_data;

	if (status_code == SOUP_STATUS_OK) {
		xmlNodePtr child;
		const xmlChar *content;

		content = e_xml_find_child_and_get_text (prop_node, E_WEBDAV_NS_CALENDARSERVER, "getctag");

		if (content && *content) {
			g_free (*data->out_ctag);
			*data->out_ctag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) content));
		}

		child = e_xml_find_child (prop_node, E_WEBDAV_NS_CARDDAV, "supported-address-data");
		if (child && child->children) {
			/* the lowest meaningful version, used before vCard 4.0 was added */
			EVCardVersion version = E_VCARD_VERSION_30;
			xmlNodePtr parent = child;
			const gchar *address_data_node_name = "address-data";
			child = e_xml_find_sibling (parent->children, E_WEBDAV_NS_CARDDAV, "address-data");
			if (!child) {
				child = e_xml_find_sibling (parent->children, E_WEBDAV_NS_CARDDAV, "address-data-type");
				if (child) {
					address_data_node_name = "address-data-type";
				}
			}

			for (;
			     child && version == E_VCARD_VERSION_30;
			     child = e_xml_find_next_sibling (child, E_WEBDAV_NS_CARDDAV, address_data_node_name)) {
				xmlChar *value;

				value = xmlGetProp (child, (const xmlChar *) "content-type");
				if (!value || g_ascii_strcasecmp ((const gchar *) value, "text/vcard") != 0) {
					g_clear_pointer (&value, xmlFree);
					continue;
				}

				g_clear_pointer (&value, xmlFree);

				value = xmlGetProp (child, (const xmlChar *) "version");
				if (g_strcmp0 ((const gchar *) value, "4.0") == 0)
					version = E_VCARD_VERSION_40;

				g_clear_pointer (&value, xmlFree);
			}

			*data->out_vcard_version = version;
		}
	}

	return FALSE;
}

static gboolean
ebb_carddav_getctag_and_vcard_version_sync (EWebDAVSession *webdav,
					    gchar **out_ctag,
					    EVCardVersion *out_vcard_version,
					    GCancellable *cancellable,
					    GError **error)
{
	EXmlDocument *xml;
	CtagVersionData data = { 0, };
	gboolean success;

	g_return_val_if_fail (E_IS_WEBDAV_SESSION (webdav), FALSE);
	g_return_val_if_fail (out_ctag != NULL, FALSE);
	g_return_val_if_fail (out_vcard_version != NULL, FALSE);

	*out_ctag = NULL;
	*out_vcard_version = E_VCARD_VERSION_30;

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml,
		"CS", E_WEBDAV_NS_CALENDARSERVER,
		"CD", E_WEBDAV_NS_CARDDAV,
		NULL);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CALENDARSERVER, "getctag");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CARDDAV, "supported-address-data");
	e_xml_document_end_element (xml); /* prop */

	data.out_ctag = out_ctag;
	data.out_vcard_version = out_vcard_version;

	success = e_webdav_session_propfind_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS, xml,
		ebb_carddav_getctag_and_version_cb, &data, cancellable, error);

	g_object_unref (xml);

	return success;
}

static gboolean
ebb_carddav_connect_sync (EBookMetaBackend *meta_backend,
			  const ENamedParameters *credentials,
			  ESourceAuthenticationResult *out_auth_result,
			  gchar **out_certificate_pem,
			  GTlsCertificateFlags *out_certificate_errors,
			  GCancellable *cancellable,
			  GError **error)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	GHashTable *capabilities = NULL, *allows = NULL;
	ESource *source;
	gboolean success, is_writable = FALSE, uri_changed = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_auth_result != NULL, FALSE);

	*out_auth_result = E_SOURCE_AUTHENTICATION_ACCEPTED;

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);
	source = e_backend_get_source (E_BACKEND (meta_backend));

	g_mutex_lock (&bbdav->priv->webdav_lock);
	if (bbdav->priv->webdav) {
		g_mutex_unlock (&bbdav->priv->webdav_lock);
		return TRUE;
	}

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
		ESourceWebdav *webdav_extension;
		GUri *current_uri;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		current_uri = e_source_webdav_dup_uri (webdav_extension);

		#define uri_str_equal(_func) (g_strcmp0 (_func (bbdav->priv->last_uri), _func (current_uri)) == 0)

		uri_changed = bbdav->priv->last_uri && current_uri && (
			g_uri_get_port (bbdav->priv->last_uri) != g_uri_get_port (current_uri) ||
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

		if (!bbdav->priv->last_uri || uri_changed) {
			g_clear_pointer (&bbdav->priv->last_uri, g_uri_unref);
			bbdav->priv->last_uri = current_uri;
		} else if (current_uri) {
			g_uri_unref (current_uri);
		}
	}
	g_mutex_unlock (&bbdav->priv->webdav_lock);

	if (uri_changed)
		e_book_meta_backend_set_sync_tag (meta_backend, NULL);

	webdav = e_webdav_session_new (source);

	e_soup_session_setup_logging (E_SOUP_SESSION (webdav), g_getenv ("CARDDAV_DEBUG"));

	e_binding_bind_property (
		bbdav, "proxy-resolver",
		webdav, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	e_soup_session_set_credentials (E_SOUP_SESSION (webdav), credentials);

	if (bbdav->priv->been_connected) {
		g_mutex_lock (&bbdav->priv->webdav_lock);
		bbdav->priv->webdav = webdav;
		g_mutex_unlock (&bbdav->priv->webdav_lock);

		return TRUE;
	}

	/* Thinks the 'getctag' extension is available the first time, but unset it when realizes it isn't. */
	bbdav->priv->ctag_supported = TRUE;

	success = e_webdav_session_options_sync (webdav, NULL,
		&capabilities, &allows, cancellable, &local_error);

	/* iCloud and Google servers can return "404 Not Found" when issued OPTIONS on the addressbook collection */
	if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND) ||
	    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_BAD_REQUEST)) {
		ESourceWebdav *webdav_extension;
		GUri *g_uri;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		g_uri = e_source_webdav_dup_uri (webdav_extension);
		if (g_uri) {
			if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND) &&
			    g_uri_get_host (g_uri) && *g_uri_get_path (g_uri) &&
			    e_util_host_is_in_domain (g_uri_get_host (g_uri), "icloud.com")) {
				/* Try parent directory */
				gchar *path;
				gint len = strlen (g_uri_get_path (g_uri));

				if (g_uri_get_path (g_uri)[len - 1] == '/') {
					gchar *np = g_strdup (g_uri_get_path (g_uri));
					np[len - 1] = '\0';
					e_util_change_uri_component (&g_uri, SOUP_URI_PATH, np);
					g_free (np);
				}

				path = g_path_get_dirname (g_uri_get_path (g_uri));
				if (path && g_str_has_prefix (g_uri_get_path (g_uri), path)) {
					gchar *uri;

					e_util_change_uri_component (&g_uri, SOUP_URI_PATH, path);

					uri = g_uri_to_string_partial (g_uri, SOUP_HTTP_URI_FLAGS);
					if (uri) {
						g_clear_error (&local_error);

						success = e_webdav_session_options_sync (webdav, uri,
							&capabilities, &allows, cancellable, &local_error);
					}

					g_free (uri);
				}

				g_free (path);
			} else if (g_uri_get_host (g_uri) && (
				   e_util_host_is_in_domain (g_uri_get_host (g_uri), "google.com") ||
				   e_util_host_is_in_domain (g_uri_get_host (g_uri), "googleapis.com") ||
				   e_util_host_is_in_domain (g_uri_get_host (g_uri), "googleusercontent.com"))) {
				g_clear_error (&local_error);
				success = TRUE;

				/* Google's CardDAV doesn't like OPTIONS, hard-code it */
				capabilities = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
				g_hash_table_insert (capabilities, g_strdup (E_WEBDAV_CAPABILITY_ADDRESSBOOK), GINT_TO_POINTER (1));

				allows = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, NULL);
				g_hash_table_insert (allows, g_strdup (SOUP_METHOD_PUT), GINT_TO_POINTER (1));
			}

			g_uri_unref (g_uri);
		}
	}

	if (success && !g_cancellable_is_cancelled (cancellable)) {
		GSList *privileges = NULL, *link;

		/* Ignore any errors here */
		if (e_webdav_session_get_current_user_privilege_set_full_sync (webdav, NULL, &privileges,
			capabilities ? NULL : &capabilities,
			allows ? NULL : &allows, cancellable, NULL)) {
			for (link = privileges; link && !is_writable; link = g_slist_next (link)) {
				EWebDAVPrivilege *privilege = link->data;

				if (privilege) {
					is_writable = privilege->hint == E_WEBDAV_PRIVILEGE_HINT_WRITE ||
						privilege->hint == E_WEBDAV_PRIVILEGE_HINT_WRITE_CONTENT ||
						privilege->hint == E_WEBDAV_PRIVILEGE_HINT_ALL;
				}
			}

			g_slist_free_full (privileges, e_webdav_privilege_free);
		} else {
			is_writable = allows && (
				g_hash_table_contains (allows, SOUP_METHOD_PUT) ||
				g_hash_table_contains (allows, SOUP_METHOD_POST) ||
				g_hash_table_contains (allows, SOUP_METHOD_DELETE));
		}
	}

	if (success) {
		ESourceWebdav *webdav_extension;
		GUri *g_uri;
		gboolean addressbook;

		webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		g_uri = e_source_webdav_dup_uri (webdav_extension);

		addressbook = capabilities && g_hash_table_contains (capabilities, E_WEBDAV_CAPABILITY_ADDRESSBOOK);

		if (addressbook) {
			e_book_backend_set_writable (E_BOOK_BACKEND (bbdav), is_writable);

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);

			bbdav->priv->is_google = g_uri && g_uri_get_host (g_uri) && (
				e_util_host_is_in_domain (g_uri_get_host (g_uri), "google.com") ||
				e_util_host_is_in_domain (g_uri_get_host (g_uri), "googleapis.com") ||
				e_util_host_is_in_domain (g_uri_get_host (g_uri), "googleusercontent.com"));
		} else {
			gchar *uri;

			uri = g_uri_to_string_partial (g_uri, G_URI_HIDE_PASSWORD);

			success = FALSE;
			g_set_error (&local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				_("Given URL “%s” doesn’t reference CardDAV address book"), uri);

			g_free (uri);

			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
		}

		g_uri_unref (g_uri);
	}

	if (success) {
		gchar *ctag = NULL;
		EVCardVersion before = bbdav->priv->vcard_version;

		/* Some servers, notably Google, allow OPTIONS when not
		   authorized (aka without credentials), thus try something
		   more aggressive, just in case.

		   The 'getctag' extension is not required, thus check
		   for unauthorized error only. */
		if (!ebb_carddav_getctag_and_vcard_version_sync (webdav, &ctag, &bbdav->priv->vcard_version, cancellable, &local_error) &&
		    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED)) {
			success = FALSE;
		} else {
			g_clear_error (&local_error);
		}

		if (before != bbdav->priv->vcard_version && bbdav->priv->vcard_version != E_VCARD_VERSION_UNKNOWN) {
			e_book_backend_notify_property_changed (E_BOOK_BACKEND (bbdav), E_BOOK_BACKEND_PROPERTY_PREFER_VCARD_VERSION,
				e_vcard_version_to_string (bbdav->priv->vcard_version));
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
		g_mutex_lock (&bbdav->priv->webdav_lock);
		bbdav->priv->webdav = webdav;
		g_mutex_unlock (&bbdav->priv->webdav_lock);
		bbdav->priv->been_connected = TRUE;
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
ebb_carddav_disconnect_sync (EBookMetaBackend *meta_backend,
			     GCancellable *cancellable,
			     GError **error)
{
	EBookBackendCardDAV *bbdav;
	ESource *source;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);

	g_mutex_lock (&bbdav->priv->webdav_lock);

	if (bbdav->priv->webdav)
		soup_session_abort (SOUP_SESSION (bbdav->priv->webdav));

	g_clear_object (&bbdav->priv->webdav);

	g_mutex_unlock (&bbdav->priv->webdav_lock);

	source = e_backend_get_source (E_BACKEND (meta_backend));
	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

	return TRUE;
}

static void
ebb_carddav_update_nfo_with_contact (EBookMetaBackendInfo *nfo,
				     EContact *contact,
				     const gchar *etag,
				     EVCardVersion vcard_version)
{
	const gchar *uid;

	g_return_if_fail (nfo != NULL);
	g_return_if_fail (E_IS_CONTACT (contact));

	uid = e_contact_get_const (contact, E_CONTACT_UID);

	if (!etag || !*etag)
		etag = nfo->revision;

	e_vcard_util_set_x_attribute (E_VCARD (contact), E_WEBDAV_X_ETAG, etag);

	g_warn_if_fail (nfo->object == NULL);
	nfo->object = e_vcard_convert_to_string (E_VCARD (contact), vcard_version);

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

static void
ebb_carddav_ensure_uid (EContact *contact,
			const gchar *href)
{
	const gchar *uid;
	gchar *new_uid = NULL;

	g_return_if_fail (E_IS_CONTACT (contact));

	uid = e_contact_get_const (contact, E_CONTACT_UID);

	if (uid && *uid)
		return;

	if (href) {
		const gchar *tmp;
		gint len;

		tmp = strrchr (href, '/');

		if (tmp)
			tmp++;

		len = tmp ? strlen (tmp) : 0;

		if (len > 4 && *tmp != '.' && g_ascii_strcasecmp (tmp + len - 4, ".vcf") == 0) {
			gint ii;

			len -= 4;

			for (ii = 0; ii < len; ii++) {
				if (tmp[ii] != '-' &&
				    tmp[ii] != '.' &&
				    !g_ascii_isalnum (tmp[ii]))
					break;
			}

			if (ii == len)
				new_uid = g_strndup (tmp, len);
		}
	}

	if (!new_uid)
		new_uid = e_util_generate_uid ();

	e_contact_set (contact, E_CONTACT_UID, new_uid);

	g_free (new_uid);
}

typedef struct _GetContactData {
	EBookBackendCardDAV *bbdav;
	GCancellable *cancellable;
	GSList **inout_slist;
} GetContactData;

static gboolean
ebb_carddav_multiget_response_cb (EWebDAVSession *webdav,
				  xmlNodePtr prop_node,
				  const GUri *request_uri,
				  const gchar *href,
				  guint status_code,
				  gpointer user_data)
{
	GetContactData *gcd = user_data;
	GSList **from_link;

	g_return_val_if_fail (gcd != NULL, FALSE);

	from_link = gcd->inout_slist;
	g_return_val_if_fail (from_link != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		const xmlChar *address_data, *etag;
		xmlNodePtr address_data_node = NULL, etag_node = NULL;

		g_return_val_if_fail (href != NULL, FALSE);

		e_xml_find_children_nodes (prop_node, 2,
			E_WEBDAV_NS_CARDDAV, "address-data", &address_data_node,
			E_WEBDAV_NS_DAV, "getetag", &etag_node);

		address_data = e_xml_get_node_text (address_data_node);
		etag = e_xml_get_node_text (etag_node);

		if (address_data) {
			EContact *contact;

			contact = ebb_carddav_contact_from_string (gcd->bbdav, (const gchar *) address_data, webdav, gcd->cancellable);
			if (contact) {
				const gchar *uid;

				ebb_carddav_ensure_uid (contact, href);

				uid = e_contact_get_const (contact, E_CONTACT_UID);
				if (uid) {
					gchar *dequoted_etag;
					GSList *link;

					dequoted_etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) etag));

					for (link = *from_link; link; link = g_slist_next (link)) {
						EBookMetaBackendInfo *nfo = link->data;

						if (!nfo)
							continue;

						if (e_webdav_session_util_item_href_equal (nfo->extra, href)) {
							/* If the server returns data in the same order as it had been requested,
							   then this speeds up lookup for the matching object. */
							if (link == *from_link)
								*from_link = g_slist_next (*from_link);

							ebb_carddav_update_nfo_with_contact (nfo, contact, dequoted_etag, gcd->bbdav->priv->vcard_version);

							break;
						}
					}

					if (!link && e_soup_session_get_log_level (E_SOUP_SESSION (webdav)) != SOUP_LOGGER_LOG_NONE)
						e_util_debug_print ("CardDAV", "Failed to find item with href '%s' in known server items\n", href);

					g_free (dequoted_etag);
				}

				g_object_unref (contact);
			}
		}
	} else if (status_code == SOUP_STATUS_NOT_FOUND) {
		GSList *link;

		g_return_val_if_fail (href != NULL, FALSE);

		for (link = *from_link; link; link = g_slist_next (link)) {
			EBookMetaBackendInfo *nfo = link->data;

			if (!nfo)
				continue;

			if (e_webdav_session_util_item_href_equal (nfo->extra, href)) {
				/* If the server returns data in the same order as it had been requested,
				   then this speeds up lookup for the matching object. */
				if (link == *from_link)
					*from_link = g_slist_next (*from_link);

				e_book_meta_backend_info_free (nfo);
				link->data = NULL;

				break;
			}
		}
	}

	return TRUE;
}

static gboolean
ebb_carddav_multiget_from_sets_sync (EBookBackendCardDAV *bbdav,
				     EWebDAVSession *webdav,
				     GSList **in_link,
				     GSList **set2,
				     GCancellable *cancellable,
				     GError **error)
{
	EXmlDocument *xml;
	gint left_to_go = E_WEBDAV_MAX_MULTIGET_AMOUNT;
	GSList *link;
	gboolean success = TRUE;

	g_return_val_if_fail (in_link != NULL, FALSE);
	g_return_val_if_fail (*in_link != NULL, FALSE);
	g_return_val_if_fail (set2 != NULL, FALSE);

	xml = e_xml_document_new (E_WEBDAV_NS_CARDDAV, "addressbook-multiget");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "getetag");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_CARDDAV, "address-data");
	e_xml_document_end_element (xml); /* prop */

	link = *in_link;

	while (link && left_to_go > 0) {
		EBookMetaBackendInfo *nfo = link->data;
		GUri *suri;
		gchar *path = NULL;

		link = g_slist_next (link);
		if (!link) {
			link = *set2;
			*set2 = NULL;
		}

		if (!nfo)
			continue;

		left_to_go--;

		suri = g_uri_parse (nfo->extra, SOUP_HTTP_URI_FLAGS, NULL);
		if (suri) {
			if (g_uri_get_query (suri))
				path = g_strdup_printf ("%s?%s",
				                        *g_uri_get_path (suri) ? g_uri_get_path (suri) : "/",
				                        g_uri_get_query (suri));
			else
				path = g_strdup (*g_uri_get_path (suri) ? g_uri_get_path (suri) : "/");
			g_uri_unref (suri);
		}

		e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "href");
		e_xml_document_write_string (xml, path ? path : nfo->extra);
		e_xml_document_end_element (xml); /* href */

		g_free (path);
	}

	if (left_to_go != E_WEBDAV_MAX_MULTIGET_AMOUNT && success) {
		GetContactData gcd = { 0, };
		GSList *from_link = *in_link;

		gcd.bbdav = bbdav;
		gcd.cancellable = cancellable;
		gcd.inout_slist = &from_link;

		success = e_webdav_session_report_sync (webdav, NULL, NULL, xml,
			ebb_carddav_multiget_response_cb, &gcd, NULL, NULL, cancellable, error);
	}

	g_object_unref (xml);

	*in_link = link;

	return success;
}

static gboolean
ebb_carddav_get_contact_items_cb (EWebDAVSession *webdav,
				  xmlNodePtr prop_node,
				  const GUri *request_uri,
				  const gchar *href,
				  guint status_code,
				  gpointer user_data)
{
	GHashTable *known_items = user_data; /* gchar *href ~> EBookMetaBackendInfo * */

	g_return_val_if_fail (prop_node != NULL, FALSE);
	g_return_val_if_fail (known_items != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		EBookMetaBackendInfo *nfo;
		gchar *etag;

		g_return_val_if_fail (href != NULL, FALSE);

		/* Skip collection resource, if returned by the server (like iCloud.com does) */
		if (g_str_has_suffix (href, "/") ||
		    (request_uri && *g_uri_get_path ((GUri *) request_uri) && g_str_has_suffix (href, g_uri_get_path ((GUri *) request_uri)))) {
			return TRUE;
		}

		etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) e_xml_find_child_and_get_text (prop_node, E_WEBDAV_NS_DAV, "getetag")));
		/* Return 'TRUE' to not stop on faulty data from the server */
		g_return_val_if_fail (etag != NULL, TRUE);

		/* UID is unknown at this moment */
		nfo = e_book_meta_backend_info_new ("", etag, NULL, href);

		g_free (etag);
		g_return_val_if_fail (nfo != NULL, FALSE);

		g_hash_table_insert (known_items, g_strdup (href), nfo);
	}

	return TRUE;
}

typedef struct _CardDAVChangesData {
	GSList **out_modified_objects;
	GSList **out_removed_objects;
	GHashTable *known_items; /* gchar *href ~> EBookMetaBackendInfo * */
} CardDAVChangesData;

static gboolean
ebb_carddav_search_changes_cb (EBookCache *book_cache,
			       const gchar *uid,
			       const gchar *revision,
			       const gchar *object,
			       const gchar *extra,
			       guint32 custom_flags,
			       EOfflineState offline_state,
			       gpointer user_data)
{
	CardDAVChangesData *ccd = user_data;

	g_return_val_if_fail (ccd != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	/* The 'extra' can be NULL for added contacts in offline mode */
	if ((extra && *extra) || offline_state != E_OFFLINE_STATE_LOCALLY_CREATED) {
		EBookMetaBackendInfo *nfo;

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
					e_book_meta_backend_info_copy (nfo));

				g_hash_table_remove (ccd->known_items, extra);
			}
		} else {
			*(ccd->out_removed_objects) = g_slist_prepend (*(ccd->out_removed_objects),
				e_book_meta_backend_info_new (uid, revision, object, extra));
		}
	}

	return TRUE;
}

static void
ebb_carddav_check_credentials_error (EBookBackendCardDAV *bbdav,
				     EWebDAVSession *webdav,
				     GError *op_error)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_CARDDAV (bbdav));

	if (g_error_matches (op_error, G_TLS_ERROR, G_TLS_ERROR_BAD_CERTIFICATE) && webdav) {
		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_TLS_NOT_AVAILABLE;
	} else if (g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		   g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN)) {
		gboolean was_forbidden = g_error_matches (op_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_FORBIDDEN);

		op_error->domain = E_CLIENT_ERROR;
		op_error->code = E_CLIENT_ERROR_AUTHENTICATION_REQUIRED;

		bbdav->priv->been_connected = FALSE;

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
ebb_carddav_get_changes_sync (EBookMetaBackend *meta_backend,
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
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	EXmlDocument *xml;
	GHashTable *known_items; /* gchar *href ~> EBookMetaBackendInfo * */
	GHashTableIter iter;
	gpointer key = NULL, value = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_new_sync_tag, FALSE);
	g_return_val_if_fail (out_created_objects, FALSE);
	g_return_val_if_fail (out_modified_objects, FALSE);
	g_return_val_if_fail (out_removed_objects, FALSE);

	*out_new_sync_tag = NULL;
	*out_created_objects = NULL;
	*out_modified_objects = NULL;
	*out_removed_objects = NULL;

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);
	webdav = ebb_carddav_ref_session (bbdav);

	if (bbdav->priv->ctag_supported) {
		gchar *new_sync_tag = NULL;

		success = e_webdav_session_getctag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL);
		if (!success) {
			bbdav->priv->ctag_supported = g_cancellable_set_error_if_cancelled (cancellable, error);
			if (bbdav->priv->ctag_supported || !webdav) {
				g_clear_object (&webdav);
				return FALSE;
			}
		} else if (new_sync_tag && last_sync_tag && g_strcmp0 (last_sync_tag, new_sync_tag) == 0) {
			*out_new_sync_tag = new_sync_tag;
			g_clear_object (&webdav);
			return TRUE;
		}

		*out_new_sync_tag = new_sync_tag;
	}

	xml = e_xml_document_new (E_WEBDAV_NS_DAV, "propfind");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_start_element (xml, NULL, "prop");
	e_xml_document_add_empty_element (xml, NULL, "getetag");
	e_xml_document_end_element (xml); /* prop */

	known_items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, e_book_meta_backend_info_free);

	success = e_webdav_session_propfind_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS_AND_CHILDREN, xml,
		ebb_carddav_get_contact_items_cb, known_items, cancellable, &local_error);

	g_object_unref (xml);

	if (success) {
		EBookCache *book_cache;
		CardDAVChangesData ccd;

		ccd.out_modified_objects = out_modified_objects;
		ccd.out_removed_objects = out_removed_objects;
		ccd.known_items = known_items;

		book_cache = e_book_meta_backend_ref_cache (meta_backend);

		success = e_book_cache_search_with_callback (book_cache, NULL, ebb_carddav_search_changes_cb, &ccd, cancellable, &local_error);

		g_clear_object (&book_cache);
	}

	if (success) {
		g_hash_table_iter_init (&iter, known_items);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			*out_created_objects = g_slist_prepend (*out_created_objects, e_book_meta_backend_info_copy (value));
		}
	}

	g_hash_table_destroy (known_items);

	if (success && (*out_created_objects || *out_modified_objects)) {
		GSList *link, *set2 = *out_modified_objects;

		if (*out_created_objects) {
			link = *out_created_objects;
		} else {
			link = set2;
			set2 = NULL;
		}

		do {
			success = ebb_carddav_multiget_from_sets_sync (bbdav, webdav, &link, &set2, cancellable, &local_error);
		} while (success && link);
	}

	if (local_error) {
		ebb_carddav_check_credentials_error (bbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ebb_carddav_extract_existing_cb (EWebDAVSession *webdav,
				 xmlNodePtr prop_node,
				 const GUri *request_uri,
				 const gchar *href,
				 guint status_code,
				 gpointer user_data)
{
	GetContactData *gcd = user_data;
	GSList **out_existing_objects;

	g_return_val_if_fail (gcd != NULL, FALSE);

	out_existing_objects = gcd->inout_slist;
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	if (status_code == SOUP_STATUS_OK) {
		const xmlChar *address_data, *etag;
		xmlNodePtr address_data_node = NULL, etag_node = NULL;

		g_return_val_if_fail (href != NULL, FALSE);

		e_xml_find_children_nodes (prop_node, 2,
			E_WEBDAV_NS_CARDDAV, "address-data", &address_data_node,
			E_WEBDAV_NS_DAV, "getetag", &etag_node);

		address_data = e_xml_get_node_text (address_data_node);
		etag = e_xml_get_node_text (etag_node);

		if (address_data) {
			EContact *contact;

			contact = ebb_carddav_contact_from_string (gcd->bbdav, (const gchar *) address_data, webdav, gcd->cancellable);
			if (contact) {
				const gchar *uid;

				ebb_carddav_ensure_uid (contact, href);

				uid = e_contact_get_const (contact, E_CONTACT_UID);

				if (uid) {
					gchar *dequoted_etag;

					dequoted_etag = e_webdav_session_util_maybe_dequote (g_strdup ((const gchar *) etag));

					*out_existing_objects = g_slist_prepend (*out_existing_objects,
						e_book_meta_backend_info_new (uid, dequoted_etag, NULL, href));

					g_free (dequoted_etag);
				}

				g_object_unref (contact);
			}
		}
	}

	return TRUE;
}

static gboolean
ebb_carddav_list_existing_sync (EBookMetaBackend *meta_backend,
				gchar **out_new_sync_tag,
				GSList **out_existing_objects,
				GCancellable *cancellable,
				GError **error)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	EXmlDocument *xml;
	GetContactData gcd = { 0, };
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (out_existing_objects != NULL, FALSE);

	*out_existing_objects = NULL;

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);

	xml = e_xml_document_new (E_WEBDAV_NS_CARDDAV, "addressbook-query");
	g_return_val_if_fail (xml != NULL, FALSE);

	e_xml_document_add_namespaces (xml, "D", E_WEBDAV_NS_DAV, NULL);

	e_xml_document_start_element (xml, E_WEBDAV_NS_DAV, "prop");
	e_xml_document_add_empty_element (xml, E_WEBDAV_NS_DAV, "getetag");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CARDDAV, "address-data");
	e_xml_document_start_element (xml, E_WEBDAV_NS_CARDDAV, "prop");
	e_xml_document_add_attribute (xml, NULL, "name", "VERSION");
	e_xml_document_end_element (xml); /* prop / VERSION */
	e_xml_document_start_element (xml, E_WEBDAV_NS_CARDDAV, "prop");
	e_xml_document_add_attribute (xml, NULL, "name", "UID");
	e_xml_document_end_element (xml); /* prop / UID */
	e_xml_document_end_element (xml); /* address-data */
	e_xml_document_end_element (xml); /* prop */

	webdav = ebb_carddav_ref_session (bbdav);

	gcd.bbdav = bbdav;
	gcd.cancellable = cancellable;
	gcd.inout_slist = out_existing_objects;

	success = e_webdav_session_report_sync (webdav, NULL, E_WEBDAV_DEPTH_THIS, xml,
		ebb_carddav_extract_existing_cb, &gcd, NULL, NULL, cancellable, &local_error);

	g_object_unref (xml);

	if (success)
		*out_existing_objects = g_slist_reverse (*out_existing_objects);

	if (local_error) {
		ebb_carddav_check_credentials_error (bbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gchar *
ebb_carddav_uid_to_uri (EBookBackendCardDAV *bbdav,
		        const gchar *uid,
		        const gchar *extension)
{
	ESourceWebdav *webdav_extension;
	GUri *guri;
	gchar *uri, *tmp, *filename, *uid_hash = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (bbdav), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	webdav_extension = e_source_get_extension (e_backend_get_source (E_BACKEND (bbdav)), E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	guri = e_source_webdav_dup_uri (webdav_extension);
	g_return_val_if_fail (guri != NULL, NULL);

	/* UIDs with forward slashes can cause trouble, because the destination server
	   can consider them as a path delimiter. For example Google book backend uses
	   URL as the contact UID. Double-encode the URL doesn't always work, thus
	   rather cause a mismatch between stored UID and its href on the server. */
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

static gboolean
ebb_carddav_load_contact_sync (EBookMetaBackend *meta_backend,
			       const gchar *uid,
			       const gchar *extra,
			       EContact **out_contact,
			       gchar **out_extra,
			       GCancellable *cancellable,
			       GError **error)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	gchar *uri = NULL, *href = NULL, *etag = NULL, *bytes = NULL;
	gsize length = -1;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_contact != NULL, FALSE);
	g_return_val_if_fail (out_extra != NULL, FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);

	/* When called immediately after save and the server didn't change the vCard,
	   then the 'extra' contains "href" + "\n" + "vCard", to avoid unneeded GET
	   from the server. */
	if (extra && *extra) {
		const gchar *newline;

		newline = strchr (extra, '\n');
		if (newline && newline[1] && newline != extra) {
			EContact *contact;

			webdav = ebb_carddav_ref_session (bbdav);
			contact = ebb_carddav_contact_from_string (bbdav, newline + 1, webdav, cancellable);
			g_clear_object (&webdav);

			if (contact) {
				*out_extra = g_strndup (extra, newline - extra);
				*out_contact = contact;

				return TRUE;
			}
		}
	}

	webdav = ebb_carddav_ref_session (bbdav);

	if (extra && *extra) {
		uri = g_strdup (extra);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);

		if (!success) {
			g_free (uri);
			uri = NULL;
		}
	}

	if (!success && bbdav->priv->ctag_supported) {
		gchar *new_sync_tag = NULL;

		if (e_webdav_session_getctag_sync (webdav, NULL, &new_sync_tag, cancellable, NULL) && new_sync_tag) {
			gchar *last_sync_tag;

			last_sync_tag = e_book_meta_backend_dup_sync_tag (meta_backend);

			/* The book didn't change, thus the contact cannot be there */
			if (g_strcmp0 (last_sync_tag, new_sync_tag) == 0) {
				g_clear_object (&webdav);
				g_clear_error (&local_error);
				g_free (last_sync_tag);
				g_free (new_sync_tag);

				g_propagate_error (error, EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));

				return FALSE;
			}

			g_free (last_sync_tag);
		}

		g_free (new_sync_tag);
	}

	if (!success) {
		uri = ebb_carddav_uid_to_uri (bbdav, uid, bbdav->priv->is_google ? NULL : ".vcf");
		g_return_val_if_fail (uri != NULL, FALSE);

		g_clear_error (&local_error);

		success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);

		/* Do not try twice with Google, it's either without extension or not there.
		   The worst, it counts to the Error requests quota limit. */
		if (!success && !bbdav->priv->is_google && !g_cancellable_is_cancelled (cancellable) &&
		    g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			g_free (uri);
			uri = ebb_carddav_uid_to_uri (bbdav, uid, NULL);

			if (uri) {
				g_clear_error (&local_error);

				success = e_webdav_session_get_data_sync (webdav, uri, &href, &etag, NULL, &bytes, &length, cancellable, &local_error);
			}
		}
	}

	if (success) {
		*out_contact = NULL;

		if (href && etag && bytes && length != ((gsize) -1)) {
			EContact *contact;

			contact = ebb_carddav_contact_from_string (bbdav, bytes, webdav, cancellable);
			if (contact) {
				e_vcard_util_set_x_attribute (E_VCARD (contact), E_WEBDAV_X_ETAG, etag);
				*out_contact = contact;
			}
		}

		if (!*out_contact) {
			success = FALSE;

			if (!href)
				g_propagate_error (&local_error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Server didn’t return object’s href")));
			else if (!etag)
				g_propagate_error (&local_error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Server didn’t return object’s ETag")));
			else
				g_propagate_error (&local_error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("Received object is not a valid vCard")));
		} else if (out_extra) {
			*out_extra = g_strdup (href);
		}
	}

	g_free (uri);
	g_free (href);
	g_free (etag);
	g_free (bytes);

	if (local_error) {
		ebb_carddav_check_credentials_error (bbdav, webdav, local_error);

		if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			local_error->domain = E_BOOK_CLIENT_ERROR;
			local_error->code = E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND;
		}

		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ebb_carddav_save_contact_sync (EBookMetaBackend *meta_backend,
			       gboolean overwrite_existing,
			       EConflictResolution conflict_resolution,
			       /* const */ EContact *in_contact,
			       const gchar *extra,
			       guint32 opflags,
			       gchar **out_new_uid,
			       gchar **out_new_extra,
			       GCancellable *cancellable,
			       GError **error)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	EContact *contact;
	gchar *href = NULL, *etag = NULL, *uid = NULL;
	gchar *vcard_string = NULL;
	GError *local_error = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (in_contact), FALSE);
	g_return_val_if_fail (out_new_uid, FALSE);
	g_return_val_if_fail (out_new_extra, FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);
	webdav = ebb_carddav_ref_session (bbdav);

	uid = e_contact_get (in_contact, E_CONTACT_UID);
	etag = e_vcard_util_dup_x_attribute (E_VCARD (in_contact), E_WEBDAV_X_ETAG);

	contact = ebb_carddav_prepare_save (bbdav, in_contact, cancellable);

	vcard_string = e_vcard_convert_to_string (E_VCARD (contact), bbdav->priv->vcard_version);

	if (uid && vcard_string && (!overwrite_existing || (extra && *extra))) {
		gchar *new_extra = NULL, *new_etag = NULL;
		gboolean force_write = FALSE;

		if (!extra || !*extra)
			href = ebb_carddav_uid_to_uri (bbdav, uid, ".vcf");

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
			force_write ? "" : overwrite_existing ? etag : NULL, E_WEBDAV_CONTENT_TYPE_VCARD,
			NULL, vcard_string, -1, &new_extra, &new_etag, NULL, cancellable, &local_error);

		if (success) {
			/* Only if both are returned and it's not a weak ETag */
			if (new_extra && *new_extra && new_etag && *new_etag &&
			    g_ascii_strncasecmp (new_etag, "W/", 2) != 0) {
				gchar *tmp;

				e_vcard_util_set_x_attribute (E_VCARD (contact), E_WEBDAV_X_ETAG, new_etag);

				g_free (vcard_string);
				vcard_string = e_vcard_convert_to_string (E_VCARD (contact), bbdav->priv->vcard_version);

				/* Encodes the href and the vCard into one string, which
				   will be decoded in the load function */
				tmp = g_strconcat (new_extra, "\n", vcard_string, NULL);
				g_free (new_extra);
				new_extra = tmp;
			}

			/* To read the vCard back, either from the new_extra
			   or from the server, because the server could change it */
			*out_new_uid = g_strdup (uid);

			if (out_new_extra)
				*out_new_extra = new_extra;
			else
				g_free (new_extra);
		}

		g_free (new_etag);
	} else if (uid && vcard_string) {
		EBookCache *cache;

		cache = e_book_meta_backend_ref_cache (meta_backend);
		success = FALSE;

		g_propagate_error (error, e_client_error_create_fmt (E_CLIENT_ERROR_INVALID_ARG,
			_("Missing information about component URL, local cache is possibly incomplete or broken. You can try to remove it and restart background evolution-data-server processes. Cache file: %s"),
			e_cache_get_filename (E_CACHE (cache))));

		g_clear_object (&cache);
	} else {
		success = FALSE;
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_INVALID_ARG, _("Object to save is not a valid vCard")));
	}

	g_object_unref (contact);
	g_free (vcard_string);
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
		ebb_carddav_check_credentials_error (bbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ebb_carddav_remove_contact_sync (EBookMetaBackend *meta_backend,
				 EConflictResolution conflict_resolution,
				 const gchar *uid,
				 const gchar *extra,
				 const gchar *object,
				 guint32 opflags,
				 GCancellable *cancellable,
				 GError **error)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	EContact *contact;
	gchar *etag = NULL;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);

	if (!extra || !*extra) {
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_INVALID_ARG));
		return FALSE;
	}

	contact = e_contact_new_from_vcard (object);
	if (!contact) {
		g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_INVALID_ARG));
		return FALSE;
	}

	if (conflict_resolution == E_CONFLICT_RESOLUTION_FAIL)
		etag = e_vcard_util_dup_x_attribute (E_VCARD (contact), E_WEBDAV_X_ETAG);

	webdav = ebb_carddav_ref_session (bbdav);

	success = e_webdav_session_delete_sync (webdav, extra,
		NULL, etag, cancellable, &local_error);

	if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
		gchar *href;

		href = ebb_carddav_uid_to_uri (bbdav, uid, ".vcf");
		if (href) {
			g_clear_error (&local_error);
			success = e_webdav_session_delete_sync (webdav, href,
				NULL, etag, cancellable, &local_error);

			g_free (href);
		}

		if (g_error_matches (local_error, E_SOUP_SESSION_ERROR, SOUP_STATUS_NOT_FOUND)) {
			href = ebb_carddav_uid_to_uri (bbdav, uid, NULL);
			if (href) {
				g_clear_error (&local_error);
				success = e_webdav_session_delete_sync (webdav, href,
					NULL, etag, cancellable, &local_error);

				g_free (href);
			}
		}
	}

	g_object_unref (contact);
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
		ebb_carddav_check_credentials_error (bbdav, webdav, local_error);
		g_propagate_error (error, local_error);
	}

	g_clear_object (&webdav);

	return success;
}

static gboolean
ebb_carddav_get_ssl_error_details (EBookMetaBackend *meta_backend,
				   gchar **out_certificate_pem,
				   GTlsCertificateFlags *out_certificate_errors)
{
	EBookBackendCardDAV *bbdav;
	EWebDAVSession *webdav;
	gboolean res;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (meta_backend), FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (meta_backend);
	webdav = ebb_carddav_ref_session (bbdav);

	if (!webdav)
		return FALSE;

	res = e_soup_session_get_ssl_error_details (E_SOUP_SESSION (webdav), out_certificate_pem, out_certificate_errors);

	g_clear_object (&webdav);

	return res;
}

static gchar *
ebb_carddav_get_backend_property (EBookBackend *book_backend,
				  const gchar *prop_name)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (book_backend), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strjoin (",",
			"net",
			"do-initial-query",
			"contact-lists",
			e_book_meta_backend_get_capabilities (E_BOOK_META_BACKEND (book_backend)),
			NULL);
	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_PREFER_VCARD_VERSION)) {
		EBookBackendCardDAV *bbdav = E_BOOK_BACKEND_CARDDAV (book_backend);
		EVCardVersion version = bbdav->priv->vcard_version;

		if (version == E_VCARD_VERSION_UNKNOWN)
			version = E_VCARD_VERSION_30;

		return g_strdup (e_vcard_version_to_string (version));
	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_carddav_parent_class)->impl_get_backend_property (book_backend, prop_name);
}

static gboolean
ebb_carddav_refresh_sync (EBookBackendSync *sync_backend,
			  GCancellable *cancellable,
			  GError **error)
{
	EBookBackendCardDAV *bbdav;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CARDDAV (sync_backend), FALSE);

	bbdav = E_BOOK_BACKEND_CARDDAV (sync_backend);
	bbdav->priv->been_connected = FALSE;

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_SYNC_CLASS (e_book_backend_carddav_parent_class)->refresh_sync (sync_backend, cancellable, error);
}

static gchar *
ebb_carddav_dup_contact_revision_cb (EBookCache *book_cache,
				     EContact *contact)
{
	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	return e_vcard_util_dup_x_attribute (E_VCARD (contact), E_WEBDAV_X_ETAG);
}

static void
e_book_backend_carddav_constructed (GObject *object)
{
	EBookBackendCardDAV *bbdav = E_BOOK_BACKEND_CARDDAV (object);
	EBookCache *book_cache;

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_carddav_parent_class)->constructed (object);

	book_cache = e_book_meta_backend_ref_cache (E_BOOK_META_BACKEND (bbdav));

	g_signal_connect (book_cache, "dup-contact-revision",
		G_CALLBACK (ebb_carddav_dup_contact_revision_cb), NULL);

	g_clear_object (&book_cache);
}

static void
e_book_backend_carddav_dispose (GObject *object)
{
	EBookBackendCardDAV *bbdav = E_BOOK_BACKEND_CARDDAV (object);

	g_mutex_lock (&bbdav->priv->webdav_lock);
	g_clear_object (&bbdav->priv->webdav);
	g_clear_pointer (&bbdav->priv->last_uri, g_uri_unref);
	g_mutex_unlock (&bbdav->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_carddav_parent_class)->dispose (object);
}

static void
e_book_backend_carddav_finalize (GObject *object)
{
	EBookBackendCardDAV *bbdav = E_BOOK_BACKEND_CARDDAV (object);

	g_mutex_clear (&bbdav->priv->webdav_lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_book_backend_carddav_parent_class)->finalize (object);
}

static void
e_book_backend_carddav_init (EBookBackendCardDAV *bbdav)
{
	bbdav->priv = e_book_backend_carddav_get_instance_private (bbdav);
	bbdav->priv->vcard_version = E_VCARD_VERSION_30;

	g_mutex_init (&bbdav->priv->webdav_lock);
}

static void
e_book_backend_carddav_class_init (EBookBackendCardDAVClass *klass)
{
	GObjectClass *object_class;
	EBookBackendClass *book_backend_class;
	EBookBackendSyncClass *book_backend_sync_class;
	EBookMetaBackendClass *book_meta_backend_class;

	book_meta_backend_class = E_BOOK_META_BACKEND_CLASS (klass);
	book_meta_backend_class->backend_module_filename = "libebookbackendcarddav.so";
	book_meta_backend_class->backend_factory_type_name = "EBookBackendCardDAVFactory";
	book_meta_backend_class->connect_sync = ebb_carddav_connect_sync;
	book_meta_backend_class->disconnect_sync = ebb_carddav_disconnect_sync;
	book_meta_backend_class->get_changes_sync = ebb_carddav_get_changes_sync;
	book_meta_backend_class->list_existing_sync = ebb_carddav_list_existing_sync;
	book_meta_backend_class->load_contact_sync = ebb_carddav_load_contact_sync;
	book_meta_backend_class->save_contact_sync = ebb_carddav_save_contact_sync;
	book_meta_backend_class->remove_contact_sync = ebb_carddav_remove_contact_sync;
	book_meta_backend_class->get_ssl_error_details = ebb_carddav_get_ssl_error_details;

	book_backend_sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	book_backend_sync_class->refresh_sync = ebb_carddav_refresh_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (klass);
	book_backend_class->impl_get_backend_property = ebb_carddav_get_backend_property;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_book_backend_carddav_constructed;
	object_class->dispose = e_book_backend_carddav_dispose;
	object_class->finalize = e_book_backend_carddav_finalize;
}

/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Authors: Debarshi Ray <debarshir@gnome.org>
 */

/* Based on code by the Evolution team.
 *
 * This was originally written as a part of evolution-ews:
 * evolution-ews/src/server/e-ews-connection.c
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>
#include <libxml/xmlIO.h>

#include <libedataserver/libedataserver.h>

#include "goaewsclient.h"

#define AUTODISCOVER_MESSAGES 2
typedef struct {
	GCancellable *cancellable;
	SoupSession *session;
	gulong cancellable_id;
	gint requests_in_flight;
} AutodiscoverData;

typedef struct {
	gchar *as_url;
	gchar *oab_url;
} AutodiscoverResult;

typedef struct {
	gchar *password;
	gchar *username;
} AutodiscoverAuthData;

static void
ews_autodiscover_data_free (AutodiscoverData *data)
{
	g_cancellable_disconnect (data->cancellable, data->cancellable_id);
	data->cancellable_id = 0;
	g_clear_object (&data->cancellable);
	g_clear_object (&data->session);

	g_free (data);
}

static void
ews_autodiscover_result_free (gpointer result)
{
	AutodiscoverResult *data = result;
	g_clear_pointer (&data->as_url, g_free);
	g_clear_pointer (&data->oab_url, g_free);

	g_free (data);
}

static void
ews_autodiscover_auth_data_free (gpointer data,
                                 GClosure *closure)
{
	AutodiscoverAuthData *auth = data;

	g_clear_pointer (&auth->password, e_util_safe_free_string);
	g_clear_pointer (&auth->username, g_free);
	g_free (auth);
}

static gboolean
ews_check_node (const xmlNode *node,
                const gchar *name)
{
	g_return_val_if_fail (node != NULL, FALSE);

	return (node->type == XML_ELEMENT_NODE) &&
		(g_strcmp0 ((gchar *) node->name, name) == 0);
}

static gboolean
ews_authenticate (SoupMessage *msg,
                  SoupAuth *auth,
                  gboolean retrying,
                  AutodiscoverAuthData *data)
{
	if (!retrying)
		soup_auth_authenticate (auth, data->username, data->password);

	return FALSE;
}

static void
ews_autodiscover_cancelled_cb (GCancellable *cancellable,
                               AutodiscoverData *data)
{
	soup_session_abort (data->session);
}

static gboolean
has_suffix_icmp (const gchar *text,
                 const gchar *suffix)
{
	gint ii, tlen, slen;

	g_return_val_if_fail (text != NULL, FALSE);
	g_return_val_if_fail (suffix != NULL, FALSE);

	tlen = strlen (text);
	slen = strlen (suffix);

	if (!*text || !*suffix || tlen < slen)
		return FALSE;

	for (ii = 0; ii < slen; ii++) {
		if (g_ascii_tolower (text[tlen - ii - 1]) !=
		    g_ascii_tolower (suffix[slen - ii - 1]))
			break;
	}

	return ii == slen;
}

static gboolean
ews_autodiscover_parse_protocol (xmlNode *node,
                                 AutodiscoverResult **result)
{
	gboolean got_as_url = FALSE;
	gboolean got_oab_url = FALSE;
	AutodiscoverResult *data = g_new0 (AutodiscoverResult, 1);

	for (node = node->children; node; node = node->next) {
		xmlChar *content;

		if (ews_check_node (node, "ASUrl")) {
			content = xmlNodeGetContent (node);
			data->as_url = g_strdup ((gchar *) content);
			xmlFree (content);
			got_as_url = TRUE;

		} else if (ews_check_node (node, "OABUrl")) {
			const gchar *oab_url;

			content = xmlNodeGetContent (node);
			oab_url = (const gchar *) content;

			if (!has_suffix_icmp (oab_url, "oab.xml")) {
				gchar *tmp;

				if (g_str_has_suffix (oab_url, "/"))
					tmp = g_strconcat (oab_url, "oab.xml", NULL);
				else
					tmp = g_strconcat (oab_url, "/", "oab.xml", NULL);

				data->oab_url = g_steal_pointer (&tmp); /* takes ownership */
			} else {
				data->oab_url = g_strdup (oab_url);
			}
			xmlFree (content);
			got_oab_url = TRUE;
		}

		if (got_as_url && got_oab_url) {
			*result = g_steal_pointer (&data);
			return TRUE;
		}
	}

	*result = NULL;
	g_clear_pointer (&data, ews_autodiscover_result_free);
	return FALSE;
}

typedef struct _ResponseData {
	SoupMessage *msg;
	GTask *task;
} ResponseData;

static void
ews_autodiscover_response_cb (GObject *source_object,
                              GAsyncResult *result,
                              gpointer user_data)
{
	ResponseData *rd = user_data;
	GTask *task = g_steal_pointer (&rd->task);
	SoupMessage *msg = g_steal_pointer (&rd->msg);
	AutodiscoverData *data = g_task_get_task_data (task);
	AutodiscoverResult *discover_result = NULL;
	GBytes *bytes;
	gboolean success = FALSE;
	xmlDoc *doc = NULL;
	xmlNode *node;
	GError *error = NULL;

	g_clear_pointer (&rd, g_free);
	bytes = soup_session_send_and_read_finish (SOUP_SESSION (source_object), result, &error);

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_atomic_int_dec_and_test (&data->requests_in_flight);
		g_clear_error (&error);
		g_clear_object (&msg);
		g_clear_object (&task);
		return;
	}

	if (soup_message_get_status (msg) != SOUP_STATUS_OK) {
		if (!error) {
			g_set_error (
				&error, GOA_ERROR,
				GOA_ERROR_FAILED, /* TODO: more specific */
				_("Code: %u — Unexpected response from server"),
				soup_message_get_status (msg));
		}
		g_clear_pointer (&bytes, g_bytes_unref);
		goto out;
	}

	g_debug ("The response body");
	g_debug ("===================");
	g_debug ("%.*s", (gint) g_bytes_get_size (bytes), (const gchar *) g_bytes_get_data (bytes, NULL));

	doc = xmlReadMemory (
		g_bytes_get_data (bytes, NULL),
		g_bytes_get_size (bytes),
		"autodiscover.xml", NULL, 0);

	g_clear_pointer (&bytes, g_bytes_unref);

	if (doc == NULL) {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to parse autodiscover response XML"));
		goto out;
	}

	node = xmlDocGetRootElement (doc);
	if (g_strcmp0 ((gchar *) node->name, "Autodiscover") != 0) {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to find Autodiscover element"));
		goto out;
	}

	for (node = node->children; node; node = node->next) {
		if (ews_check_node (node, "Response"))
			break;
	}
	if (node == NULL) {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to find Response element"));
		goto out;
	}

	for (node = node->children; node; node = node->next) {
		if (ews_check_node (node, "Account"))
			break;
	}
	if (node == NULL) {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to find Account element"));
		goto out;
	}

	for (node = node->children; node; node = node->next) {
		if (ews_check_node (node, "Protocol")) {
			success = ews_autodiscover_parse_protocol (node, &discover_result);
			break;
		}
	}

	if (success) {
		g_task_return_pointer (task,
			g_steal_pointer (&discover_result),
			ews_autodiscover_result_free);
	} else {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to find ASUrl and OABUrl in autodiscover response"));
			goto out;
	}

	/* Since we are cancelling from the same thread
	 * that we queued the message, the callback (ie.
	 * this function) will be invoked before
	 * soup_session_abort returns. */
	soup_session_abort (data->session);

 out:
	if (error != NULL) {
		/* There's another request outstanding.
		 * Hope that it has better luck. */
		if (g_atomic_int_dec_and_test (&data->requests_in_flight)) {
			g_task_return_error (task, g_steal_pointer (&error));
		}

		g_clear_error (&error);
	}

	g_clear_pointer (&doc, xmlFreeDoc);
	g_clear_object (&msg);
	g_clear_object (&task);
}

static xmlDoc *
ews_create_autodiscover_xml (const gchar *email)
{
	xmlDoc *doc;
	xmlNode *node;
	xmlNs *ns;

	doc = xmlNewDoc ((xmlChar *) "1.0");

	node = xmlNewDocNode (doc, NULL, (xmlChar *) "Autodiscover", NULL);
	xmlDocSetRootElement (doc, node);
	ns = xmlNewNs (
		node,
		(xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/outlook/requestschema/2006",
		NULL);

	node = xmlNewChild (node, ns, (xmlChar *) "Request", NULL);
	xmlNewChild (node, ns, (xmlChar *) "EMailAddress", (xmlChar *) email);
	xmlNewChild (
		node, ns,
		(xmlChar *) "AcceptableResponseSchema",
		(xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/outlook/responseschema/2006a");

	return doc;
}

static void
ews_post_restarted_cb (SoupMessage *msg,
                       gpointer data)
{
	GBytes *buf = data;
	gconstpointer buf_content;
	gsize buf_size;

	/* In violation of RFC2616, libsoup will change a
	 * POST request to a GET on receiving a 302 redirect. */
	g_debug ("Working around libsoup bug with redirect");
	g_object_set (msg, "method", "POST", NULL);

	buf_content = g_bytes_get_data (buf, &buf_size);
	e_soup_session_util_set_message_request_body_from_data (msg, TRUE, "text/xml; charset=utf-8", buf_content, buf_size, NULL);
}

static gboolean
goa_ews_client_accept_certificate_cb (SoupMessage *msg,
				      GTlsCertificate *tls_peer_certificate,
				      GTlsCertificateFlags tls_peer_errors,
				      gpointer user_data)
{
	/* As much as EDS is interested, any certificate error during
	   autodiscover is ignored, because it had been allowed during
	   the GOA account creation. */

	return TRUE;
}

static SoupMessage *
ews_create_msg_for_url (const gchar *url,
                        GBytes *buf)
{
	SoupMessage *msg;

	msg = soup_message_new (buf != NULL ? "POST" : "GET", url);
	soup_message_headers_append (
		soup_message_get_request_headers (msg), "User-Agent", "libews/0.1");

	g_signal_connect (msg, "accept-certificate",
		G_CALLBACK (goa_ews_client_accept_certificate_cb), NULL);

	if (buf != NULL) {
		gsize buf_size;
		gconstpointer buf_content = g_bytes_get_data (buf, &buf_size);
		e_soup_session_util_set_message_request_body_from_data (msg, TRUE, "text/xml; charset=utf-8", buf_content, buf_size, NULL);
		g_signal_connect_data (
			msg, "restarted",
			G_CALLBACK (ews_post_restarted_cb), g_bytes_ref (buf),
			(GClosureNotify) g_bytes_unref, 0);
	}

	return msg;
}

void
goa_ews_autodiscover (GoaObject *goa_object,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GoaAccount *goa_account;
	GoaExchange *goa_exchange;
	GoaPasswordBased *goa_password;
	GTask *task;
	AutodiscoverData *data;
	gchar *urls[AUTODISCOVER_MESSAGES];
	xmlDoc *doc;
	xmlChar *xml_body = NULL;
	gint xml_body_size = 0;
	gchar *email;
	gchar *username;
	gchar *host;
	gchar *password = NULL;
	GError *error = NULL;
	GBytes *bytes;
	guint ii;

	g_return_if_fail (GOA_IS_OBJECT (goa_object));

	task = g_task_new (goa_object, cancellable, callback, user_data);
	g_task_set_source_tag (task, goa_ews_autodiscover);
	g_task_set_check_cancellable (task, TRUE);

	goa_password = goa_object_get_password_based (goa_object);
	goa_password_based_call_get_password_sync (
		goa_password, "", &password, cancellable, &error);
	g_clear_object (&goa_password);

	/* Sanity check */
	g_return_if_fail (
		((password != NULL) && (error == NULL)) ||
		((password == NULL) && (error != NULL)));

	if (error) {
		g_dbus_error_strip_remote_error (error);
		g_task_return_error (task, g_steal_pointer (&error));
		g_object_unref (task);
		return;
	}

	goa_exchange = goa_object_get_exchange (goa_object);
	host = goa_exchange_dup_host (goa_exchange);
	g_clear_object (&goa_exchange);

	goa_account = goa_object_get_account (goa_object);
	email = goa_account_dup_presentation_identity (goa_account);
	username = goa_account_dup_identity (goa_account);
	g_clear_object (&goa_account);

	doc = ews_create_autodiscover_xml (email);
	xmlDocDumpMemory (doc, &xml_body, &xml_body_size);
	bytes = g_bytes_new_with_free_func (xml_body, xml_body_size,
	                                    (GDestroyNotify) xmlFree, xml_body);

	g_clear_pointer (&doc, xmlFreeDoc);
	g_clear_pointer (&email, g_free);

	urls[0] = g_strdup_printf (
		"https://%s/autodiscover/autodiscover.xml", host);
	urls[1] = g_strdup_printf (
		"https://autodiscover.%s/autodiscover/autodiscover.xml", host);
	g_clear_pointer (&host, g_free);

	/* http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
	* supposed to try $domain and then autodiscover.$domain. But some
	* people have broken firewalls on the former which drop packets
	* instead of rejecting connections, and make the request take ages
	* to time out. So run both queries in parallel and let the fastest
	* (successful) one win. */

	data = g_new0 (AutodiscoverData, 1);
	data->session = soup_session_new_with_options (
		"timeout", 15,
		"accept-language-auto", TRUE,
		NULL);
	data->requests_in_flight = AUTODISCOVER_MESSAGES;
	if (G_IS_CANCELLABLE (cancellable)) {
		data->cancellable = g_object_ref (cancellable);
		data->cancellable_id = g_cancellable_connect (
			data->cancellable,
			G_CALLBACK (ews_autodiscover_cancelled_cb),
			data, NULL);
	}
	g_task_set_task_data (task, data, (GDestroyNotify) ews_autodiscover_data_free);

	for (ii = 0; ii < AUTODISCOVER_MESSAGES; ii++) {
		ResponseData *rd;
		AutodiscoverAuthData *auth;

		rd = g_new0 (ResponseData, 1);
		rd->msg = ews_create_msg_for_url (urls[ii], bytes);
		rd->task = g_object_ref (task);
		g_clear_pointer (&urls[ii], g_free);

		auth = g_new0 (AutodiscoverAuthData, 1);
		auth->username = g_strdup (username);
		auth->password = g_strdup (password);

		g_signal_connect_data (
			rd->msg, "authenticate",
			G_CALLBACK (ews_authenticate), g_steal_pointer (&auth),
			ews_autodiscover_auth_data_free, 0);

		soup_session_send_and_read_async (data->session, rd->msg, G_PRIORITY_DEFAULT,
			cancellable, ews_autodiscover_response_cb, rd);
	}

	g_clear_pointer (&username, g_free);
	g_clear_pointer (&password, e_util_safe_free_string);
	g_clear_pointer (&bytes, g_bytes_unref);
	g_object_unref (task);
}

gboolean
goa_ews_autodiscover_finish (GoaObject *goa_object,
                             GAsyncResult *result,
                             gchar **out_as_url,
                             gchar **out_oab_url,
                             GError **error)
{
	AutodiscoverResult *data;

	g_return_val_if_fail (g_task_is_valid (result, goa_object), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, goa_ews_autodiscover), FALSE);

	data = g_task_propagate_pointer (G_TASK (result), error);
	if (!data)
		return FALSE;

	if (out_as_url != NULL)
		*out_as_url = g_steal_pointer (&data->as_url);

	if (out_oab_url != NULL)
		*out_oab_url = g_steal_pointer (&data->oab_url);

	g_clear_pointer (&data, ews_autodiscover_result_free);
	return TRUE;
}

gboolean
goa_ews_autodiscover_sync (GoaObject *goa_object,
                           gchar **out_as_url,
                           gchar **out_oab_url,
                           GCancellable *cancellable,
                           GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (GOA_IS_OBJECT (goa_object), FALSE);

	closure = e_async_closure_new ();

	goa_ews_autodiscover (
		goa_object, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = goa_ews_autodiscover_finish (
		goa_object, result, out_as_url, out_oab_url, error);

	e_async_closure_free (closure);

	return success;
}


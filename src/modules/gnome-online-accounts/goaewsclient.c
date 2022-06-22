/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
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

typedef struct {
	GCancellable *cancellable;
	SoupMessage *msgs[2];
	SoupSession *session;
	gulong cancellable_id;
	xmlOutputBuffer *buf;

	/* results */
	gchar *as_url;
	gchar *oab_url;
} AutodiscoverData;

typedef struct {
	gchar *password;
	gchar *username;
} AutodiscoverAuthData;

static void
ews_autodiscover_data_free (AutodiscoverData *data)
{
	if (data->cancellable_id > 0) {
		g_cancellable_disconnect (
			data->cancellable, data->cancellable_id);
		g_object_unref (data->cancellable);
	}

	/* soup_session_queue_message stole the references to data->msgs */
	xmlOutputBufferClose (data->buf);
	g_object_unref (data->session);

	g_free (data->as_url);
	g_free (data->oab_url);

	g_slice_free (AutodiscoverData, data);
}

static void
ews_autodiscover_auth_data_free (gpointer data,
                                 GClosure *closure)
{
	AutodiscoverAuthData *auth = data;

	g_free (auth->password);
	g_free (auth->username);
	g_slice_free (AutodiscoverAuthData, auth);
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
                                 AutodiscoverData *data)
{
	gboolean got_as_url = FALSE;
	gboolean got_oab_url = FALSE;

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

				data->oab_url = tmp; /* takes ownership */
			} else {
				data->oab_url = g_strdup (oab_url);
			}
			xmlFree (content);
			got_oab_url = TRUE;
		}

		if (got_as_url && got_oab_url)
			break;
	}

	return (got_as_url && got_oab_url);
}

typedef struct _ResponseData {
	SoupMessage *msg;
	GSimpleAsyncResult *simple;
} ResponseData;

static void
ews_autodiscover_response_cb (GObject *source_object,
                              GAsyncResult *result,
                              gpointer user_data)
{
	ResponseData *rd = user_data;
	GSimpleAsyncResult *simple = rd->simple;
	SoupMessage *msg = rd->msg;
	AutodiscoverData *data;
	GBytes *bytes;
	gboolean success = FALSE;
	gint idx;
	gsize size;
	xmlDoc *doc;
	xmlNode *node;
	GError *error = NULL;

	data = g_simple_async_result_get_op_res_gpointer (simple);

	g_slice_free (ResponseData, rd);
	rd = NULL;

	bytes = soup_session_send_and_read_finish (SOUP_SESSION (source_object), result, &error);

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error (&error);
		g_clear_object (&msg);
		g_clear_object (&simple);
		return;
	}

	size = sizeof (data->msgs) / sizeof (data->msgs[0]);

	for (idx = 0; idx < size; idx++) {
		if (data->msgs[idx] == msg)
			break;
	}
	if (idx == size) {
		g_clear_error (&error);
		if (bytes)
			g_bytes_unref (bytes);
		g_clear_object (&msg);
		g_clear_object (&simple);
		return;
	}

	data->msgs[idx] = NULL;

	if (soup_message_get_status (msg) != SOUP_STATUS_OK) {
		if (!error) {
			g_set_error (
				&error, GOA_ERROR,
				GOA_ERROR_FAILED, /* TODO: more specific */
				_("Code: %u — Unexpected response from server"),
				soup_message_get_status (msg));
		}
		if (bytes)
			g_bytes_unref (bytes);
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
			success = ews_autodiscover_parse_protocol (node, data);
			break;
		}
	}
	if (!success) {
		g_set_error (
			&error, GOA_ERROR,
			GOA_ERROR_FAILED, /* TODO: more specific */
			_("Failed to find ASUrl and OABUrl in autodiscover response"));
			goto out;
	}

	for (idx = 0; idx < size; idx++) {
		if (data->msgs[idx] != NULL) {
			data->msgs[idx] = NULL;
		}
	}

	/* Since we are cancelling from the same thread
	 * that we queued the message, the callback (ie.
	 * this function) will be invoked before
	 * soup_session_abort returns. */
	soup_session_abort (data->session);

 out:
	if (error != NULL) {
		for (idx = 0; idx < size; idx++) {
			if (data->msgs[idx] != NULL) {
				/* There's another request outstanding.
				 * Hope that it has better luck. */
				g_clear_error (&error);
				g_clear_object (&msg);
				g_clear_object (&simple);
				return;
			}
		}
		g_simple_async_result_take_error (simple, error);
	}

	g_simple_async_result_complete_in_idle (simple);
	g_object_unref (simple);
	g_clear_object (&msg);
	g_clear_object (&simple);
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

static void
ews_post_restarted_cb (SoupMessage *msg,
                       gpointer data)
{
	xmlOutputBuffer *buf = data;
	gconstpointer buf_content;
	gsize buf_size;

	/* In violation of RFC2616, libsoup will change a
	 * POST request to a GET on receiving a 302 redirect. */
	g_debug ("Working around libsoup bug with redirect");
	g_object_set (msg, "method", "POST", NULL);

	buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
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
                        xmlOutputBuffer *buf)
{
	SoupMessage *msg;
	gconstpointer buf_content;
	gsize buf_size;

	msg = soup_message_new (buf != NULL ? "POST" : "GET", url);
	soup_message_headers_append (
		soup_message_get_request_headers (msg), "User-Agent", "libews/0.1");

	g_signal_connect (msg, "accept-certificate",
		G_CALLBACK (goa_ews_client_accept_certificate_cb), NULL);

	if (buf != NULL) {
		buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
		e_soup_session_util_set_message_request_body_from_data (msg, TRUE, "text/xml; charset=utf-8", buf_content, buf_size, NULL);
		g_signal_connect (
			msg, "restarted",
			G_CALLBACK (ews_post_restarted_cb), buf);
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
	GSimpleAsyncResult *simple;
	AutodiscoverData *data;
	AutodiscoverAuthData *auth;
	gchar *url1;
	gchar *url2;
	xmlDoc *doc;
	xmlOutputBuffer *buf;
	gchar *email;
	gchar *host;
	gchar *password = NULL;
	GError *error = NULL;

	g_return_if_fail (GOA_IS_OBJECT (goa_object));

	goa_account = goa_object_get_account (goa_object);
	goa_exchange = goa_object_get_exchange (goa_object);
	goa_password = goa_object_get_password_based (goa_object);

	email = goa_account_dup_presentation_identity (goa_account);
	host = goa_exchange_dup_host (goa_exchange);

	doc = ews_create_autodiscover_xml (email);
	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, xmlDocGetRootElement (doc), 0, 1, NULL);
	xmlOutputBufferFlush (buf);

	url1 = g_strdup_printf (
		"https://%s/autodiscover/autodiscover.xml", host);
	url2 = g_strdup_printf (
		"https://autodiscover.%s/autodiscover/autodiscover.xml", host);

	g_free (host);
	g_free (email);

	/* http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
	* supposed to try $domain and then autodiscover.$domain. But some
	* people have broken firewalls on the former which drop packets
	* instead of rejecting connections, and make the request take ages
	* to time out. So run both queries in parallel and let the fastest
	* (successful) one win. */
	data = g_slice_new0 (AutodiscoverData);
	data->buf = buf;
	data->msgs[0] = ews_create_msg_for_url (url1, buf);
	data->msgs[1] = ews_create_msg_for_url (url2, buf);
	data->session = soup_session_new_with_options (
		"timeout", 90,
		"accept-language-auto", TRUE,
		NULL);
	if (G_IS_CANCELLABLE (cancellable)) {
		data->cancellable = g_object_ref (cancellable);
		data->cancellable_id = g_cancellable_connect (
			data->cancellable,
			G_CALLBACK (ews_autodiscover_cancelled_cb),
			data, NULL);
	}

	simple = g_simple_async_result_new (
		G_OBJECT (goa_object), callback,
		user_data, goa_ews_autodiscover);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, data, (GDestroyNotify) ews_autodiscover_data_free);

	goa_password_based_call_get_password_sync (
		goa_password, "", &password, cancellable, &error);

	/* Sanity check */
	g_return_if_fail (
		((password != NULL) && (error == NULL)) ||
		((password == NULL) && (error != NULL)));

	if (error == NULL) {
		ResponseData *rd;
		gchar *username;

		username = goa_account_dup_identity (goa_account);

		auth = g_slice_new0 (AutodiscoverAuthData);
		auth->username = username;  /* takes ownership */
		auth->password = password;  /* takes ownership */

		g_signal_connect_data (
			data->msgs[0], "authenticate",
			G_CALLBACK (ews_authenticate), auth,
			ews_autodiscover_auth_data_free, 0);

		auth = g_slice_new0 (AutodiscoverAuthData);
		auth->username = g_strdup (username);
		auth->password = g_strdup (password);

		g_signal_connect_data (
			data->msgs[1], "authenticate",
			G_CALLBACK (ews_authenticate), auth,
			ews_autodiscover_auth_data_free, 0);

		rd = g_slice_new (ResponseData);
		rd->msg = g_object_ref (data->msgs[0]);
		rd->simple = g_object_ref (simple);

		soup_session_send_and_read_async (data->session, data->msgs[0], G_PRIORITY_DEFAULT, cancellable,
			ews_autodiscover_response_cb, rd);

		rd = g_slice_new (ResponseData);
		rd->msg = g_object_ref (data->msgs[1]);
		rd->simple = g_object_ref (simple);

		soup_session_send_and_read_async (data->session, data->msgs[1], G_PRIORITY_DEFAULT, cancellable,
			ews_autodiscover_response_cb, rd);
	} else {
		g_dbus_error_strip_remote_error (error);
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_free (url2);
	g_free (url1);
	xmlFreeDoc (doc);

	g_object_unref (goa_account);
	g_object_unref (goa_exchange);
	g_object_unref (goa_password);
}

gboolean
goa_ews_autodiscover_finish (GoaObject *goa_object,
                             GAsyncResult *result,
                             gchar **out_as_url,
                             gchar **out_oab_url,
                             GError **error)
{
	GSimpleAsyncResult *simple;
	AutodiscoverData *data;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (goa_object),
		goa_ews_autodiscover), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	data = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	if (out_as_url != NULL) {
		*out_as_url = data->as_url;
		data->as_url = NULL;
	}

	if (out_oab_url != NULL) {
		*out_oab_url = data->oab_url;
		data->oab_url = NULL;
	}

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


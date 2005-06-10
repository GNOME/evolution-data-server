/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "e2k-context.h"
#include "e2k-encoding-utils.h"
#include "e2k-marshal.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"

#include <libsoup/soup-address.h>
#include <libsoup/soup-message-filter.h>
#include <libsoup/soup-session-async.h>
#include <libsoup/soup-session-sync.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-uri.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

enum {
	REDIRECT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _E2kContextPrivate {
	SoupSession *session, *async_session;
	char *owa_uri, *username, *password;
	time_t last_timestamp;

	/* Notification listener */
	SoupSocket *get_local_address_sock;
	GIOChannel *listener_channel;
	int listener_watch_id;

	char *notification_uri;
	GHashTable *subscriptions_by_id, *subscriptions_by_uri;

	/* Forms-based authentication */
	char *cookie;
	gboolean cookie_verified;
};

/* For operations with progress */
#define E2K_CONTEXT_MIN_BATCH_SIZE 25
#define E2K_CONTEXT_MAX_BATCH_SIZE 100

#ifdef E2K_DEBUG
char *e2k_debug;
int e2k_debug_level;
#endif

static gboolean renew_subscription (gpointer user_data);
static void unsubscribe_internal (E2kContext *ctx, const char *uri, GList *sub_list);
static gboolean do_notification (GIOChannel *source, GIOCondition condition, gpointer data);

static void setup_message (SoupMessageFilter *filter, SoupMessage *msg);

static void
init (GObject *object)
{
	E2kContext *ctx = E2K_CONTEXT (object);

	ctx->priv = g_new0 (E2kContextPrivate, 1);
	ctx->priv->subscriptions_by_id =
		g_hash_table_new (g_str_hash, g_str_equal);
	ctx->priv->subscriptions_by_uri =
		g_hash_table_new (g_str_hash, g_str_equal);
}

static void
destroy_sub_list (gpointer uri, gpointer sub_list, gpointer ctx)
{
	unsubscribe_internal (ctx, uri, sub_list);
	g_list_free (sub_list);
}

static void
dispose (GObject *object)
{
	E2kContext *ctx = E2K_CONTEXT (object);

	if (ctx->priv) {
		if (ctx->priv->owa_uri)
			g_free (ctx->priv->owa_uri);
		if (ctx->priv->username)
			g_free (ctx->priv->username);
		if (ctx->priv->password)
			g_free (ctx->priv->password);

		if (ctx->priv->get_local_address_sock)
			g_object_unref (ctx->priv->get_local_address_sock);

		g_hash_table_foreach (ctx->priv->subscriptions_by_uri,
				      destroy_sub_list, ctx);
		g_hash_table_destroy (ctx->priv->subscriptions_by_uri);

		g_hash_table_destroy (ctx->priv->subscriptions_by_id);

		if (ctx->priv->listener_watch_id)
			g_source_remove (ctx->priv->listener_watch_id);
		if (ctx->priv->listener_channel) {
			g_io_channel_shutdown (ctx->priv->listener_channel,
					       FALSE, NULL);
			g_io_channel_unref (ctx->priv->listener_channel);
		}

		if (ctx->priv->session)
			g_object_unref (ctx->priv->session);
		if (ctx->priv->async_session)
			g_object_unref (ctx->priv->async_session);

		g_free (ctx->priv->cookie);

		g_free (ctx->priv);
		ctx->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;

	signals[REDIRECT] =
		g_signal_new ("redirect",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (E2kContextClass, redirect),
			      NULL, NULL,
			      e2k_marshal_NONE__INT_STRING_STRING,
			      G_TYPE_NONE, 3,
			      G_TYPE_INT,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
}

static void
filter_iface_init (SoupMessageFilterClass *filter_class)
{
	/* interface implementation */
	filter_class->setup_message = setup_message;

#ifdef E2K_DEBUG
	e2k_debug = getenv ("E2K_DEBUG");
	if (e2k_debug)
		e2k_debug_level = atoi (e2k_debug);
#endif
}

E2K_MAKE_TYPE_WITH_IFACE (e2k_context, E2kContext, class_init, init, PARENT_TYPE, filter_iface_init, SOUP_TYPE_MESSAGE_FILTER)


static void
renew_sub_list (gpointer key, gpointer value, gpointer data)
{
	GList *sub_list;

	for (sub_list = value; sub_list; sub_list = sub_list->next)
		renew_subscription (sub_list->data);
}

static void
got_connection (SoupSocket *sock, guint status, gpointer user_data)
{
	E2kContext *ctx = user_data;
	SoupAddress *addr;
	struct sockaddr_in sin;
	const char *local_ipaddr;
	unsigned short port;
	int s, ret;

	ctx->priv->get_local_address_sock = NULL;

	if (status != SOUP_STATUS_OK)
		goto done;

	addr = soup_socket_get_local_address (sock);
	local_ipaddr = soup_address_get_physical (addr);

	s = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1)
		goto done;

	memset (&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;

	port = (short)getpid ();
	do {
		port++;
		if (port < 1024)
			port += 1024;
		sin.sin_port = htons (port);
		ret = bind (s, (struct sockaddr *)&sin, sizeof (sin));
	} while (ret == -1 && errno == EADDRINUSE);

	if (ret == -1) {
		close (s);
		goto done;
	}

	ctx->priv->listener_channel = g_io_channel_unix_new (s);
	g_io_channel_set_encoding (ctx->priv->listener_channel, NULL, NULL);
	g_io_channel_set_buffered (ctx->priv->listener_channel, FALSE);

	ctx->priv->listener_watch_id =
		g_io_add_watch (ctx->priv->listener_channel,
				G_IO_IN, do_notification, ctx);

	ctx->priv->notification_uri = g_strdup_printf ("httpu://%s:%u/",
							local_ipaddr,
							port);

	g_hash_table_foreach (ctx->priv->subscriptions_by_uri,
			      renew_sub_list, ctx);

 done:
	if (sock)
		g_object_unref (sock);
	g_object_unref (ctx);
}

/**
 * e2k_context_new:
 * @uri: OWA uri to connect to
 *
 * Creates a new #E2kContext based at @uri
 *
 * Return value: the new context
 **/
E2kContext *
e2k_context_new (const char *uri)
{
	E2kContext *ctx;
	SoupUri *suri;

	suri = soup_uri_new (uri);
	if (!suri)
		return NULL;

	ctx = g_object_new (E2K_TYPE_CONTEXT, NULL);
	ctx->priv->owa_uri = g_strdup (uri);

	g_object_ref (ctx);
	ctx->priv->get_local_address_sock =
		soup_socket_client_new_async (
			suri->host, suri->port, FALSE,
			got_connection, ctx);
	soup_uri_free (suri);

	return ctx;
}

static void
session_authenticate (SoupSession *session, SoupMessage *msg,
		      const char *auth_type, const char *auth_realm,
		      char **username, char **password, gpointer user_data)
{
	E2kContext *ctx = user_data;

	*username = g_strdup (ctx->priv->username);
	*password = g_strdup (ctx->priv->password);
}

/**
 * e2k_context_set_auth:
 * @ctx: the context
 * @username: the Windows username (not including domain) of the user
 * @domain: the NT domain, or %NULL to use the default (if using NTLM)
 * @authmech: the HTTP Authorization type to use; either "Basic" or "NTLM"
 * @password: the user's password
 *
 * Sets the authentication information on @ctx. This will have the
 * side effect of cancelling any pending requests on @ctx.
 **/
void
e2k_context_set_auth (E2kContext *ctx, const char *username,
		      const char *domain, const char *authmech,
		      const char *password)
{

	g_return_if_fail (E2K_IS_CONTEXT (ctx));

	if (username) {
		g_free (ctx->priv->username);
		if (domain) {
			ctx->priv->username =
				g_strdup_printf ("%s\\%s", domain,
						 username);
		} else
			ctx->priv->username = g_strdup (username);
	}

	if (password) {
		g_free (ctx->priv->password);
		ctx->priv->password = g_strdup (password);
	}

	/* Destroy the old sessions so we don't reuse old auths */
	if (ctx->priv->session)
		g_object_unref (ctx->priv->session);
	if (ctx->priv->async_session)
		g_object_unref (ctx->priv->async_session);

	ctx->priv->session = soup_session_sync_new_with_options (
		SOUP_SESSION_USE_NTLM, !authmech || !strcmp (authmech, "NTLM"),
		NULL);
	g_signal_connect (ctx->priv->session, "authenticate",
			  G_CALLBACK (session_authenticate), ctx);
	soup_session_add_filter (ctx->priv->session,
				 SOUP_MESSAGE_FILTER (ctx));

	ctx->priv->async_session = soup_session_async_new_with_options (
		SOUP_SESSION_USE_NTLM, !authmech || !strcmp (authmech, "NTLM"),
		NULL);
	g_signal_connect (ctx->priv->async_session, "authenticate",
			  G_CALLBACK (session_authenticate), ctx);
	soup_session_add_filter (ctx->priv->async_session,
				 SOUP_MESSAGE_FILTER (ctx));
}

/**
 * e2k_context_get_last_timestamp:
 * @ctx: the context
 *
 * Returns a %time_t corresponding to the last "Date" header
 * received from the server.
 *
 * Return value: the timestamp
 **/
time_t
e2k_context_get_last_timestamp (E2kContext *ctx)
{
	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), -1);

	return ctx->priv->last_timestamp;
}

#ifdef E2K_DEBUG
/* Debug levels:
 * 0 - None
 * 1 - Basic request and response
 * 2 - 1 plus all headers
 * 3 - 2 plus all bodies
 * 4 - 3 plus Global Catalog debug too
 */

static void
print_header (gpointer name, gpointer value, gpointer data)
{
	printf ("%s: %s\n", (char *)name, (char *)value);
}

static void
e2k_debug_print_request (SoupMessage *msg, const char *note)
{
	const SoupUri *uri;

	uri = soup_message_get_uri (msg);
	printf ("%s %s%s%s HTTP/1.1\nE2k-Debug: %p @ %lu",
		msg->method, uri->path,
		uri->query ? "?" : "",
		uri->query ? uri->query : "",
		msg, (unsigned long)time (0));
	if (note)
		printf (" [%s]\n", note);
	else
		printf ("\n");
	if (e2k_debug_level > 1) {
		print_header ("Host", uri->host, NULL);
		soup_message_foreach_header (msg->request_headers,
					     print_header, NULL);
	}
	if (e2k_debug_level > 2 && msg->request.length &&
	    strcmp (msg->method, "POST")) {
		printf ("\n");
		fwrite (msg->request.body, 1, msg->request.length, stdout);
		if (msg->request.body[msg->request.length - 1] != '\n')
			printf ("\n");
	}
	printf ("\n");
}

static void
e2k_debug_print_response (SoupMessage *msg)
{
	printf ("%d %s\nE2k-Debug: %p @ %lu\n",
		msg->status_code, msg->reason_phrase,
		msg, time (0));
	if (e2k_debug_level > 1) {
		soup_message_foreach_header (msg->response_headers,
					     print_header, NULL);
	}
	if (e2k_debug_level > 2 && msg->response.length &&
	    E2K_HTTP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		const char *content_type =
			soup_message_get_header (msg->response_headers,
						 "Content-Type");
		if (!content_type || e2k_debug_level > 4 ||
		    g_ascii_strcasecmp (content_type, "text/html")) {
			printf ("\n");
			fwrite (msg->response.body, 1, msg->response.length, stdout);
			if (msg->response.body[msg->response.length - 1] != '\n')
				printf ("\n");
		}
	}
	printf ("\n");
}

static void
e2k_debug_handler (SoupMessage *msg, gpointer user_data)
{
	gboolean restarted = GPOINTER_TO_INT (user_data);

	e2k_debug_print_response (msg);
	if (restarted)
		e2k_debug_print_request (msg, "restarted");
}

static void
e2k_debug_setup (SoupMessage *msg)
{
	if (!e2k_debug_level)
		return;

	e2k_debug_print_request (msg, NULL);

	g_signal_connect (msg, "finished",
			  G_CALLBACK (e2k_debug_handler),
			  GINT_TO_POINTER (FALSE));
	g_signal_connect (msg, "restarted",
			  G_CALLBACK (e2k_debug_handler),
			  GINT_TO_POINTER (TRUE));
}
#endif

#define E2K_FBA_FLAG_FORCE_DOWNLEVEL 1
#define E2K_FBA_FLAG_TRUSTED         4

/**
 * e2k_context_fba:
 * @ctx: the context
 * @failed_msg: a message that received a 440 status code
 *
 * Attempts to synchronously perform Exchange 2003 forms-based
 * authentication.
 *
 * Return value: %FALSE if authentication failed, %TRUE if it
 * succeeded, in which case @failed_msg can be requeued.
 **/
gboolean
e2k_context_fba (E2kContext *ctx, SoupMessage *failed_msg)
{
	static gboolean in_fba_auth = FALSE;
	int status, len;
	char *body;
	char *action, *method, *name, *value;
	xmlDoc *doc = NULL;
	xmlNode *node;
	SoupMessage *post_msg;
	GString *form_body, *cookie_str;
	const GSList *cookies, *c;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), FALSE);

	if (in_fba_auth)
		return FALSE;

	if (ctx->priv->cookie) {
		g_free (ctx->priv->cookie);
		ctx->priv->cookie = NULL;
		if (!ctx->priv->cookie_verified) {
			/* New cookie failed on the first try. Must
			 * be a bad password.
			 */
			return FALSE;
		}
		/* Otherwise, it's just expired. */
	}

	if (!ctx->priv->username || !ctx->priv->password)
		return FALSE;

	in_fba_auth = TRUE;

	status = e2k_context_get_owa (ctx, NULL, ctx->priv->owa_uri,
				      FALSE, &body, &len);
	if (!SOUP_STATUS_IS_SUCCESSFUL (status) || len == 0)
		goto failed;

	doc = e2k_parse_html (body, len);
	g_free (body);

	node = e2k_xml_find (doc->children, "form");
	if (!node)
		goto failed;

	method = xmlGetProp (node, "method");
	if (!method || g_ascii_strcasecmp (method, "post") != 0) {
		if (method)
			xmlFree (method);
		goto failed;
	}
	xmlFree (method);

	value = xmlGetProp (node, "action");
	if (!value)
		goto failed;
	if (*value == '/') {
		SoupUri *suri;

		suri = soup_uri_new (ctx->priv->owa_uri);
		g_free (suri->path);
		suri->path = g_strdup (value);
		action = soup_uri_to_string (suri, FALSE);
		soup_uri_free (suri);
	} else
		action = g_strdup (value);
	xmlFree (value);

	form_body = g_string_new (NULL);
	while ((node = e2k_xml_find (node, "input"))) {
		name = xmlGetProp (node, "name");
		if (!name)
			continue;
		value = xmlGetProp (node, "value");

		if (!g_ascii_strcasecmp (name, "destination") && value) {
			g_string_append (form_body, name);
			g_string_append_c (form_body, '=');
			e2k_uri_append_encoded (form_body, value, FALSE, NULL);
			g_string_append_c (form_body, '&');
		} else if (!g_ascii_strcasecmp (name, "flags")) {
			g_string_append_printf (form_body, "flags=%d",
						E2K_FBA_FLAG_TRUSTED);
			g_string_append_c (form_body, '&');
		} else if (!g_ascii_strcasecmp (name, "username")) {
			g_string_append (form_body, "username=");
			e2k_uri_append_encoded (form_body, ctx->priv->username, FALSE, NULL);
			g_string_append_c (form_body, '&');
		} else if (!g_ascii_strcasecmp (name, "password")) {
			g_string_append (form_body, "password=");
			e2k_uri_append_encoded (form_body, ctx->priv->password, FALSE, NULL);
			g_string_append_c (form_body, '&');
		}

		if (value)
			xmlFree (value);
		xmlFree (name);
	}
	g_string_append_printf (form_body, "trusted=%d", E2K_FBA_FLAG_TRUSTED);
	xmlFreeDoc (doc);
	doc = NULL;

	post_msg = e2k_soup_message_new_full (ctx, action, "POST",
					      "application/x-www-form-urlencoded",
					      SOUP_BUFFER_SYSTEM_OWNED,
					      form_body->str, form_body->len);
	soup_message_set_flags (post_msg, SOUP_MESSAGE_NO_REDIRECT);
	e2k_context_send_message (ctx, NULL /* FIXME? */, post_msg);
	g_string_free (form_body, FALSE);
	g_free (action);

	if (!SOUP_STATUS_IS_SUCCESSFUL (post_msg->status_code) &&
	    !SOUP_STATUS_IS_REDIRECTION (post_msg->status_code)) {
		g_object_unref (post_msg);
		goto failed;
	}

	/* Extract the cookies */
	cookies = soup_message_get_header_list (post_msg->response_headers,
						"Set-Cookie");
	cookie_str = g_string_new (NULL);

	for (c = cookies; c; c = c->next) {
		value = c->data;
		len = strcspn (value, ";");

		if (cookie_str->len)
			g_string_append (cookie_str, "; ");
		g_string_append_len (cookie_str, value, len);
	}
	ctx->priv->cookie = cookie_str->str;
	ctx->priv->cookie_verified = FALSE;
	g_string_free (cookie_str, FALSE);
	g_object_unref (post_msg);

	in_fba_auth = FALSE;

	/* Set up the failed message to be requeued */
	soup_message_remove_header (failed_msg->request_headers, "Cookie");
	soup_message_add_header (failed_msg->request_headers,
				 "Cookie", ctx->priv->cookie);
	return TRUE;

 failed:
	in_fba_auth = FALSE;
	if (doc)
		xmlFreeDoc (doc);
	return FALSE;
}

static void
fba_timeout_handler (SoupMessage *msg, gpointer user_data)
{
	E2kContext *ctx = user_data;

#ifdef E2K_DEBUG
	if (e2k_debug_level)
		e2k_debug_print_response (msg);
#endif

	if (e2k_context_fba (ctx, msg))
		soup_session_requeue_message (ctx->priv->session, msg);
	else
		soup_message_set_status (msg, SOUP_STATUS_UNAUTHORIZED);
}

static void
timestamp_handler (SoupMessage *msg, gpointer user_data)
{
	E2kContext *ctx = user_data;
	const char *date;

	date = soup_message_get_header (msg->response_headers, "Date");
	if (date)
		ctx->priv->last_timestamp = e2k_http_parse_date (date);
}

static void
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	E2kContext *ctx = user_data;
	const char *new_uri;
	SoupUri *soup_uri;
	char *old_uri;

	if (soup_message_get_flags (msg) & SOUP_MESSAGE_NO_REDIRECT)
		return;

	new_uri = soup_message_get_header (msg->response_headers, "Location");
	if (new_uri) {
		soup_uri = soup_uri_copy (soup_message_get_uri (msg));
		old_uri = soup_uri_to_string (soup_uri, FALSE);

		g_signal_emit (ctx, signals[REDIRECT], 0,
			       msg->status_code, old_uri, new_uri);
		soup_uri_free (soup_uri);
		g_free (old_uri);
	}
}

static void
setup_message (SoupMessageFilter *filter, SoupMessage *msg)
{
	E2kContext *ctx = E2K_CONTEXT (filter);


	if (ctx->priv->cookie) {
		soup_message_remove_header (msg->request_headers, "Cookie");
		soup_message_add_header (msg->request_headers,
					 "Cookie", ctx->priv->cookie);
	}

	/* Only do this the first time through */
	if (!soup_message_get_header (msg->request_headers, "User-Agent")) {
		soup_message_add_handler (msg, SOUP_HANDLER_PRE_BODY,
					  timestamp_handler, ctx);
		soup_message_add_status_class_handler (msg, SOUP_STATUS_CLASS_REDIRECT,
						       SOUP_HANDLER_PRE_BODY,
						       redirect_handler, ctx);
		soup_message_add_status_code_handler (msg, E2K_HTTP_TIMEOUT,
						      SOUP_HANDLER_PRE_BODY,
						      fba_timeout_handler, ctx);
		soup_message_add_header (msg->request_headers, "User-Agent",
					 "Evolution/" EVOLUTION_VERSION);

#ifdef E2K_DEBUG
		e2k_debug_setup (msg);
#endif
	}
}

/**
 * e2k_soup_message_new:
 * @ctx: the context
 * @uri: the URI
 * @method: the HTTP method
 *
 * Creates a new %SoupMessage for @ctx.
 *
 * Return value: a new %SoupMessage, set up for connector use
 **/
SoupMessage *
e2k_soup_message_new (E2kContext *ctx, const char *uri, const char *method)
{
	SoupMessage *msg;

	if (method[0] == 'B') {
		char *slash_uri = e2k_strdup_with_trailing_slash (uri);
		msg = soup_message_new (method, slash_uri);
		g_free (slash_uri);
	} else
		msg = soup_message_new (method, uri);

	return msg;
}

/**
 * e2k_soup_message_new_full:
 * @ctx: the context
 * @uri: the URI
 * @method: the HTTP method
 * @content_type: MIME Content-Type of @body
 * @owner: ownership of @body
 * @body: request body
 * @length: length of @body
 *
 * Creates a new %SoupMessage with the given body.
 *
 * Return value: a new %SoupMessage with a request body, set up for
 * connector use
 **/
SoupMessage *
e2k_soup_message_new_full (E2kContext *ctx, const char *uri,
			   const char *method, const char *content_type,
			   SoupOwnership owner, const char *body,
			   gulong length)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new (ctx, uri, method);
	soup_message_set_request (msg, content_type,
				  owner, (char *)body, length);

	return msg;
}

/**
 * e2k_context_queue_message:
 * @ctx: the context
 * @msg: the message to queue
 * @callback: callback to invoke when @msg is done
 * @user_data: data for @callback
 *
 * Asynchronously queues @msg in @ctx's session.
 **/
void
e2k_context_queue_message (E2kContext *ctx, SoupMessage *msg,
			   SoupMessageCallbackFn callback,
			   gpointer user_data)
{
	g_return_if_fail (E2K_IS_CONTEXT (ctx));

	soup_session_queue_message (ctx->priv->async_session, msg,
				    callback, user_data);
}

static void
context_canceller (E2kOperation *op, gpointer owner, gpointer data)
{
	E2kContext *ctx = owner;
	SoupMessage *msg = data;

	soup_message_set_status (msg, SOUP_STATUS_CANCELLED);
	soup_session_cancel_message (ctx->priv->session, msg);
}

/**
 * e2k_context_send_message:
 * @ctx: the context
 * @op: an #E2kOperation to use for cancellation
 * @msg: the message to send
 *
 * Synchronously sends @msg in @ctx's session.
 *
 * Return value: the HTTP status of the message
 **/
E2kHTTPStatus
e2k_context_send_message (E2kContext *ctx, E2kOperation *op, SoupMessage *msg)
{
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);

	if (e2k_operation_is_cancelled (op)) {
		soup_message_set_status (msg, E2K_HTTP_CANCELLED);
		return E2K_HTTP_CANCELLED;
	}

	e2k_operation_start (op, context_canceller, ctx, msg);
	status = soup_session_send_message (ctx->priv->session, msg);
	e2k_operation_finish (op);

	return status;
}


static void
update_unique_uri (E2kContext *ctx, SoupMessage *msg,
		   const char *folder_uri, const char *encoded_name, int *count,
		   E2kContextTestCallback test_callback, gpointer user_data)
{
	SoupUri *suri;
	char *uri = NULL;

	do {
		g_free (uri);
		if (*count == 1) {
			uri = g_strdup_printf ("%s%s.EML", folder_uri,
					       encoded_name);
		} else {
			uri = g_strdup_printf ("%s%s-%d.EML", folder_uri,
					       encoded_name, *count);
		}
		(*count)++;
	} while (test_callback && !test_callback (ctx, uri, user_data));

	suri = soup_uri_new (uri);
	soup_message_set_uri (msg, suri);
	soup_uri_free (suri);
	g_free (uri);
}


/* GET */

static SoupMessage *
get_msg (E2kContext *ctx, const char *uri, gboolean owa, gboolean claim_ie)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new (ctx, uri, "GET");
	if (!owa)
		soup_message_add_header (msg->request_headers, "Translate", "F");
	if (claim_ie) {
		soup_message_remove_header (msg->request_headers, "User-Agent");
		soup_message_add_header (msg->request_headers, "User-Agent",
					 "MSIE 6.0b (Windows NT 5.0; compatible; "
					 "Evolution/" EVOLUTION_VERSION ")");
	}

	return msg;
}

/**
 * e2k_context_get:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: URI of the object to GET
 * @content_type: if not %NULL, will contain the Content-Type of the
 * response on return.
 * @body: if not %NULL, will contain the response body on return
 * @len: if not %NULL, will contain the response body length on return
 *
 * Performs a GET on @ctx for @uri. If successful (2xx status code),
 * the Content-Type, body and length will be returned. The body is not
 * terminated by a '\0'. If the GET is not successful, @content_type,
 * @body and @len will be untouched (even if the error response
 * included a body).
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_get (E2kContext *ctx, E2kOperation *op, const char *uri,
		 char **content_type, char **body, int *len)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);

	msg = get_msg (ctx, uri, FALSE, FALSE);
	status = e2k_context_send_message (ctx, op, msg);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (content_type) {
			const char *header;
			header = soup_message_get_header (msg->response_headers,
							  "Content-Type");
			*content_type = g_strdup (header);
		}
		if (body) {
			*body = msg->response.body;
			msg->response.owner = SOUP_BUFFER_USER_OWNED;
		}
		if (len)
			*len = msg->response.length;
	}

	g_object_unref (msg);
	return status;
}

/**
 * e2k_context_get_owa:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: URI of the object to GET
 * @claim_ie: whether or not to claim to be IE
 * @body: if not %NULL, will contain the response body on return
 * @len: if not %NULL, will contain the response body length on return
 *
 * As with e2k_context_get(), but used when you need the HTML or XML
 * data that would be returned to OWA rather than the raw object data.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_get_owa (E2kContext *ctx, E2kOperation *op,
		     const char *uri, gboolean claim_ie,
		     char **body, int *len)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);

	msg = get_msg (ctx, uri, TRUE, claim_ie);
	status = e2k_context_send_message (ctx, op, msg);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (body) {
			*body = msg->response.body;
			msg->response.owner = SOUP_BUFFER_USER_OWNED;
		}
		if (len)
			*len = msg->response.length;
	}

	g_object_unref (msg);
	return status;
}

/* PUT / POST */

static SoupMessage *
put_msg (E2kContext *ctx, const char *uri, const char *content_type,
	 SoupOwnership buffer_type, const char *body, int length)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (ctx, uri, "PUT", content_type,
					 buffer_type, body, length);
	soup_message_add_header (msg->request_headers, "Translate", "f");

	return msg;
}

static SoupMessage *
post_msg (E2kContext *ctx, const char *uri, const char *content_type,
	  SoupOwnership buffer_type, const char *body, int length)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (ctx, uri, "POST", content_type,
					 buffer_type, body, length);
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	return msg;
}

static void
extract_put_results (SoupMessage *msg, char **location, char **repl_uid)
{
	const char *header;

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (msg->status_code))
		return;

	if (repl_uid) {
		header = soup_message_get_header (msg->response_headers,
						  "Repl-UID");
		*repl_uid = g_strdup (header);
	}
	if (location) {
		header = soup_message_get_header (msg->response_headers,
						  "Location");
		*location = g_strdup (header);
	}
}

/**
 * e2k_context_put:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the URI to PUT to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @repl_uid: if not %NULL, will contain the Repl-UID of the PUT
 * object on return
 *
 * Performs a PUT operation on @ctx for @uri.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_put (E2kContext *ctx, E2kOperation *op, const char *uri,
		 const char *content_type, const char *body, int length,
		 char **repl_uid)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (content_type != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (body != NULL, E2K_HTTP_MALFORMED);

	msg = put_msg (ctx, uri, content_type,
		       SOUP_BUFFER_USER_OWNED,
		       body, length);
	status = e2k_context_send_message (ctx, op, msg);
	extract_put_results (msg, NULL, repl_uid);

	g_object_unref (msg);
	return status;
}

/**
 * e2k_context_put_new:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @folder_uri: the URI of the folder to PUT into
 * @object_name: base name of the new object (not URI-encoded)
 * @test_callback: callback to use to test possible object URIs
 * @user_data: data for @test_callback
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @location: if not %NULL, will contain the Location of the PUT
 * object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the PUT
 * object on return
 *
 * PUTs data into @folder_uri on @ctx with a new name based on
 * @object_name. If @test_callback is non-%NULL, it will be called
 * with each URI that is considered for the object so that the caller
 * can check its summary data to see if that URI is in use
 * (potentially saving one or more round-trips to the server).
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_put_new (E2kContext *ctx, E2kOperation *op,
		     const char *folder_uri, const char *object_name,
		     E2kContextTestCallback test_callback, gpointer user_data,
		     const char *content_type, const char *body, int length,
		     char **location, char **repl_uid)
{
	SoupMessage *msg;
	E2kHTTPStatus status;
	char *slash_uri, *encoded_name;
	int count;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (folder_uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (object_name != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (content_type != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (body != NULL, E2K_HTTP_MALFORMED);

	slash_uri = e2k_strdup_with_trailing_slash (folder_uri);
	encoded_name = e2k_uri_encode (object_name, TRUE, NULL);

	/* folder_uri is a dummy here */
	msg = put_msg (ctx, folder_uri, content_type,
		       SOUP_BUFFER_USER_OWNED, body, length);
	soup_message_add_header (msg->request_headers, "If-None-Match", "*");

	count = 1;
	do {
		update_unique_uri (ctx, msg, slash_uri, encoded_name, &count,
				   test_callback, user_data);
		status = e2k_context_send_message (ctx, op, msg);
	} while (status == E2K_HTTP_PRECONDITION_FAILED);

	extract_put_results (msg, location, repl_uid);

	g_object_unref (msg);
	g_free (slash_uri);
	g_free (encoded_name);
	return status;
}

/**
 * e2k_context_post:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the URI to POST to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @location: if not %NULL, will contain the Location of the POSTed
 * object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the POSTed
 * object on return
 *
 * Performs a POST operation on @ctx for @uri.
 *
 * Note that POSTed objects will be irrevocably(?) marked as "unsent",
 * If you open a POSTed message in Outlook, it will open in the
 * composer rather than in the message viewer.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_post (E2kContext *ctx, E2kOperation *op, const char *uri,
		  const char *content_type, const char *body, int length,
		  char **location, char **repl_uid)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (content_type != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (body != NULL, E2K_HTTP_MALFORMED);

	msg = post_msg (ctx, uri, content_type,
			SOUP_BUFFER_USER_OWNED,
			body, length);

	status = e2k_context_send_message (ctx, op, msg);
	extract_put_results (msg, location, repl_uid);

	g_object_unref (msg);
	return status;
}

/* PROPPATCH */

static void
add_namespaces (const char *namespace, char abbrev, gpointer user_data)
{
	GString *propxml = user_data;

	g_string_append_printf (propxml, " xmlns:%c=\"%s\"", abbrev, namespace);
}

static void
write_prop (GString *xml, const char *propertyname,
	    E2kPropType type, gpointer value, gboolean set)
{
	const char *namespace, *name, *typestr;
	char *encoded, abbrev;
	gboolean b64enc, need_type;
	GByteArray *data;
	GPtrArray *array;
	int i;

	if (value == NULL)
		return;
	namespace = e2k_prop_namespace_name (propertyname);
	abbrev = e2k_prop_namespace_abbrev (propertyname);
	name = e2k_prop_property_name (propertyname);

	need_type = (strstr (namespace, "/mapi/id/") != NULL);

	g_string_append_printf (xml, "<%c:%s", abbrev, name);
	if (!set) {
		g_string_append (xml, "/>");
		return;
	} else if (!need_type)
		g_string_append_c (xml, '>');

	switch (type) {
	case E2K_PROP_TYPE_BINARY:
		if (need_type)
			g_string_append (xml, " T:dt=\"bin.base64\">");
		data = value;
		encoded = e2k_base64_encode (data->data, data->len);
		g_string_append (xml, encoded);
		g_free (encoded);
		break;

	case E2K_PROP_TYPE_STRING_ARRAY:
		typestr = " T:dt=\"mv.string\">";
		b64enc = FALSE;
		goto array_common;

	case E2K_PROP_TYPE_INT_ARRAY:
		typestr = " T:dt=\"mv.int\">";
		b64enc = FALSE;
		goto array_common;

	case E2K_PROP_TYPE_BINARY_ARRAY:
		typestr = " T:dt=\"mv.bin.base64\">";
		b64enc = TRUE;

	array_common:
		if (need_type)
			g_string_append (xml, typestr);
		array = value;
		for (i = 0; i < array->len; i++) {
			g_string_append (xml, "<X:v>");

			if (b64enc) {
				data = array->pdata[i];
				encoded = e2k_base64_encode (data->data,
							     data->len);
				g_string_append (xml, encoded);
				g_free (encoded);
			} else
				e2k_g_string_append_xml_escaped (xml, array->pdata[i]);

			g_string_append (xml, "</X:v>");
		}
		break;

	case E2K_PROP_TYPE_XML:
		g_assert_not_reached ();
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		if (need_type) {
			switch (type) {
			case E2K_PROP_TYPE_INT:
				typestr = " T:dt=\"int\">";
				break;
			case E2K_PROP_TYPE_BOOL:
				typestr = " T:dt=\"boolean\">";
				break;
			case E2K_PROP_TYPE_FLOAT:
				typestr = " T:dt=\"float\">";
				break;
			case E2K_PROP_TYPE_DATE:
				typestr = " T:dt=\"dateTime.tz\">";
				break;
			default:
				typestr = ">";
				break;
			}
			g_string_append (xml, typestr);
		}
		e2k_g_string_append_xml_escaped (xml, value);
		break;

	}

	g_string_append_printf (xml, "</%c:%s>", abbrev, name);
}

static void
add_set_props (const char *propertyname, E2kPropType type,
	       gpointer value, gpointer user_data)
{
	GString **props = user_data;

	if (!*props)
		*props = g_string_new (NULL);

	write_prop (*props, propertyname, type, value, TRUE);
}

static void
add_remove_props (const char *propertyname, E2kPropType type,
		  gpointer value, gpointer user_data)
{
	GString **props = user_data;

	if (!*props)
		*props = g_string_new (NULL);

	write_prop (*props, propertyname, type, value, FALSE);
}

static SoupMessage *
patch_msg (E2kContext *ctx, const char *uri, const char *method,
	   const char **hrefs, int nhrefs, E2kProperties *props,
	   gboolean create)
{
	SoupMessage *msg;
	GString *propxml, *subxml;
	int i;

	propxml = g_string_new (E2K_XML_HEADER);
	g_string_append (propxml, "<D:propertyupdate xmlns:D=\"DAV:\"");

	/* Iterate over the properties, noting each namespace once,
	 * then add them all to the header.
	 */
	e2k_properties_foreach_namespace (props, add_namespaces, propxml);
	g_string_append (propxml, ">\r\n");

	/* If this is a BPROPPATCH, add the <target> section. */
	if (hrefs) {
		g_string_append (propxml, "<D:target>\r\n");
		for (i = 0; i < nhrefs; i++) {
			g_string_append_printf (propxml, "<D:href>%s</D:href>",
						hrefs[i]);
		}
		g_string_append (propxml, "\r\n</D:target>\r\n");
	}

	/* Add <set> properties. */
	subxml = NULL;
	e2k_properties_foreach (props, add_set_props, &subxml);
	if (subxml) {
		g_string_append (propxml, "<D:set><D:prop>\r\n");
		g_string_append (propxml, subxml->str);
		g_string_append (propxml, "\r\n</D:prop></D:set>");
		g_string_free (subxml, TRUE);
	}

	/* Add <remove> properties. */
	subxml = NULL;
	e2k_properties_foreach_removed (props, add_remove_props, &subxml);
	if (subxml) {
		g_string_append (propxml, "<D:remove><D:prop>\r\n");
		g_string_append (propxml, subxml->str);
		g_string_append (propxml, "\r\n</D:prop></D:remove>");
		g_string_free (subxml, TRUE);
	}

	/* Finish it up */
	g_string_append (propxml, "\r\n</D:propertyupdate>");

	/* And build the message. */
	msg = e2k_soup_message_new_full (ctx, uri, method,
					 "text/xml", SOUP_BUFFER_SYSTEM_OWNED,
					 propxml->str, propxml->len);
	g_string_free (propxml, FALSE);
	soup_message_add_header (msg->request_headers, "Brief", "t");
	if (!create)
		soup_message_add_header (msg->request_headers, "If-Match", "*");

	return msg;
}

/**
 * e2k_context_proppatch:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the URI to PROPPATCH
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 * @repl_uid: if not %NULL, will contain the Repl-UID of the
 * PROPPATCHed object on return
 *
 * Performs a PROPPATCH operation on @ctx for @uri.
 *
 * If @create is %FALSE and @uri does not already exist, the response
 * code will be %E2K_HTTP_PRECONDITION_FAILED.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_proppatch (E2kContext *ctx, E2kOperation *op,
		       const char *uri, E2kProperties *props,
		       gboolean create, char **repl_uid)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (props != NULL, E2K_HTTP_MALFORMED);

	msg = patch_msg (ctx, uri, "PROPPATCH", NULL, 0, props, create);
	status = e2k_context_send_message (ctx, op, msg);
	extract_put_results (msg, NULL, repl_uid);

	g_object_unref (msg);
	return status;
}

/**
 * e2k_context_proppatch_new:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @folder_uri: the URI of the folder to PROPPATCH a new object in
 * @object_name: base name of the new object (not URI-encoded)
 * @test_callback: callback to use to test possible object URIs
 * @user_data: data for @test_callback
 * @props: the properties to set/remove
 * @location: if not %NULL, will contain the Location of the
 * PROPPATCHed object on return
 * @repl_uid: if not %NULL, will contain the Repl-UID of the
 * PROPPATCHed object on return
 *
 * PROPPATCHes data into @folder_uri on @ctx with a new name based on
 * @object_name. If @test_callback is non-%NULL, it will be called
 * with each URI that is considered for the object so that the caller
 * can check its summary data to see if that URI is in use
 * (potentially saving one or more round-trips to the server).

 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_proppatch_new (E2kContext *ctx, E2kOperation *op,
			   const char *folder_uri, const char *object_name,
			   E2kContextTestCallback test_callback,
			   gpointer user_data,
			   E2kProperties *props,
			   char **location, char **repl_uid)
{
	SoupMessage *msg;
	E2kHTTPStatus status;
	char *slash_uri, *encoded_name;
	int count;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (folder_uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (object_name != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (props != NULL, E2K_HTTP_MALFORMED);

	slash_uri = e2k_strdup_with_trailing_slash (folder_uri);
	encoded_name = e2k_uri_encode (object_name, TRUE, NULL);

	/* folder_uri is a dummy here */
	msg = patch_msg (ctx, folder_uri, "PROPPATCH", NULL, 0, props, TRUE);
	soup_message_add_header (msg->request_headers, "If-None-Match", "*");

	count = 1;
	do {
		update_unique_uri (ctx, msg, slash_uri, encoded_name, &count,
				   test_callback, user_data);
		status = e2k_context_send_message (ctx, op, msg);
	} while (status == E2K_HTTP_PRECONDITION_FAILED);

	if (location)
		*location = soup_uri_to_string (soup_message_get_uri (msg), FALSE);
	extract_put_results (msg, NULL, repl_uid);

	g_object_unref (msg);
	g_free (slash_uri);
	g_free (encoded_name);
	return status;
}

static E2kHTTPStatus
bproppatch_fetch (E2kResultIter *iter,
		  E2kContext *ctx, E2kOperation *op,
		  E2kResult **results, int *nresults,
		  int *first, int *total,
		  gpointer user_data)
{
	SoupMessage *msg = user_data;
	E2kHTTPStatus status;

	if (msg->status != SOUP_MESSAGE_STATUS_IDLE)
		return E2K_HTTP_OK;

	status = e2k_context_send_message (ctx, op, msg);
	if (status == E2K_HTTP_MULTI_STATUS) {
		e2k_results_from_multistatus (msg, results, nresults);
		*total = *nresults;
	}
	return status;
}

static void
bproppatch_free (E2kResultIter *iter, gpointer msg)
{
	g_object_unref (msg);
}

/**
 * e2k_context_bproppatch_start:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 *
 * Begins a BPROPPATCH (bulk PROPPATCH) of @hrefs based at @uri.
 *
 * Return value: an iterator for getting the results of the BPROPPATCH
 **/
E2kResultIter *
e2k_context_bproppatch_start (E2kContext *ctx, E2kOperation *op,
			      const char *uri, const char **hrefs, int nhrefs,
			      E2kProperties *props, gboolean create)
{
	SoupMessage *msg;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (props != NULL, NULL);

	msg = patch_msg (ctx, uri, "BPROPPATCH", hrefs, nhrefs, props, create);
	return e2k_result_iter_new (ctx, op, TRUE, -1,
				    bproppatch_fetch, bproppatch_free,
				    msg);
}

/* PROPFIND */

static SoupMessage *
propfind_msg (E2kContext *ctx, const char *base_uri,
	      const char **props, int nprops, const char **hrefs, int nhrefs)
{
	SoupMessage *msg;
	GString *propxml;
	GData *set_namespaces;
	const char *name;
	char abbrev;
	int i;

	propxml = g_string_new (E2K_XML_HEADER);
	g_string_append (propxml, "<D:propfind xmlns:D=\"DAV:\"");

	set_namespaces = NULL;
	for (i = 0; i < nprops; i++) {
		name = e2k_prop_namespace_name (props[i]);
		abbrev = e2k_prop_namespace_abbrev (props[i]);

		if (!g_datalist_get_data (&set_namespaces, name)) {
			g_datalist_set_data (&set_namespaces, name,
					     GINT_TO_POINTER (1));
			g_string_append_printf (propxml, " xmlns:%c=\"%s\"",
						abbrev, name);
		}
	}
	g_datalist_clear (&set_namespaces);
	g_string_append (propxml, ">\r\n");

	if (hrefs) {
		g_string_append (propxml, "<D:target>\r\n");
		for (i = 0; i < nhrefs; i++) {
			g_string_append_printf (propxml, "<D:href>%s</D:href>",
						hrefs[i]);
		}
		g_string_append (propxml, "\r\n</D:target>\r\n");
	}

	g_string_append (propxml, "<D:prop>\r\n");
	for (i = 0; i < nprops; i++) {
		abbrev = e2k_prop_namespace_abbrev (props[i]);
		name = e2k_prop_property_name (props[i]);
		g_string_append_printf (propxml, "<%c:%s/>", abbrev, name);
	}
	g_string_append (propxml, "\r\n</D:prop>\r\n</D:propfind>");

	msg = e2k_soup_message_new_full (ctx, base_uri, 
					 hrefs ? "BPROPFIND" : "PROPFIND",
					 "text/xml", SOUP_BUFFER_SYSTEM_OWNED,
					 propxml->str, propxml->len);
	g_string_free (propxml, FALSE);
	soup_message_add_header (msg->request_headers, "Brief", "t");
	soup_message_add_header (msg->request_headers, "Depth", "0");

	return msg;
}

/**
 * e2k_context_propfind:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the URI to PROPFIND on
 * @props: array of properties to find
 * @nprops: length of @props
 * @results: on return, the results
 * @nresults: length of @results
 *
 * Performs a PROPFIND operation on @ctx for @uri. If successful, the
 * results are returned as an array of #E2kResult (which you must free
 * with e2k_results_free()), but the array will always have either 0
 * or 1 members.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_propfind (E2kContext *ctx, E2kOperation *op,
		      const char *uri, const char **props, int nprops,
		      E2kResult **results, int *nresults)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (props != NULL, E2K_HTTP_MALFORMED);

	msg = propfind_msg (ctx, uri, props, nprops, NULL, 0);
	status = e2k_context_send_message (ctx, op, msg);

	if (msg->status_code == E2K_HTTP_MULTI_STATUS)
		e2k_results_from_multistatus (msg, results, nresults);
	g_object_unref (msg);
	return status;
}

static E2kHTTPStatus
bpropfind_fetch (E2kResultIter *iter,
		 E2kContext *ctx, E2kOperation *op,
		 E2kResult **results, int *nresults,
		 int *first, int *total,
		 gpointer user_data)
{
	GSList **msgs = user_data;
	E2kHTTPStatus status;
	SoupMessage *msg;

	if (!*msgs)
		return E2K_HTTP_OK;

	msg = (*msgs)->data;
	*msgs = g_slist_remove (*msgs, msg);

	status = e2k_context_send_message (ctx, op, msg);
	if (status == E2K_HTTP_MULTI_STATUS)
		e2k_results_from_multistatus (msg, results, nresults);
	g_object_unref (msg);

	return status;
}

static void
bpropfind_free (E2kResultIter *iter, gpointer user_data)
{
	GSList **msgs = user_data, *m;

	for (m = *msgs; m; m = m->next)
		g_object_unref (m->data);
	g_slist_free (*msgs);
	g_free (msgs);
}

/**
 * e2k_context_bpropfind_start:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @props: array of properties to find
 * @nprops: length of @props
 *
 * Begins a BPROPFIND (bulk PROPFIND) operation on @ctx for @hrefs.
 *
 * Return value: an iterator for getting the results
 **/
E2kResultIter *
e2k_context_bpropfind_start (E2kContext *ctx, E2kOperation *op,
			     const char *uri, const char **hrefs, int nhrefs,
			     const char **props, int nprops)
{
	SoupMessage *msg;
	GSList **msgs;
	int i;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (props != NULL, NULL);
	g_return_val_if_fail (hrefs != NULL, NULL);

	msgs = g_new0 (GSList *, 1);
	for (i = 0; i < nhrefs; i += E2K_CONTEXT_MAX_BATCH_SIZE) {
		msg = propfind_msg (ctx, uri, props, nprops,
				    hrefs + i, MIN (E2K_CONTEXT_MAX_BATCH_SIZE, nhrefs - i));
		*msgs = g_slist_append (*msgs, msg);
	}

	return e2k_result_iter_new (ctx, op, TRUE, nhrefs,
				    bpropfind_fetch, bpropfind_free,
				    msgs);
}

/* SEARCH */

static SoupMessage *
search_msg (E2kContext *ctx, const char *uri,
	    SoupOwnership buffer_type, const char *searchxml,
	    int size, gboolean ascending, int offset)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (ctx, uri, "SEARCH", "text/xml",
					 buffer_type, searchxml,
					 strlen (searchxml));
	soup_message_add_header (msg->request_headers, "Brief", "t");

	if (size) {
		char *range;

		if (offset == INT_MAX) {
			range = g_strdup_printf ("rows=-%u", size);
		} else {
			range = g_strdup_printf ("rows=%u-%u",
						 offset, offset + size - 1);
		}
		soup_message_add_header (msg->request_headers, "Range", range);
		g_free (range);
	}

	return msg;
}

static char *
search_xml (const char **props, int nprops,
	    E2kRestriction *rn, const char *orderby)
{
	GString *xml;
	char *ret, *where;
	int i;

	xml = g_string_new (E2K_XML_HEADER);
	g_string_append (xml, "<searchrequest xmlns=\"DAV:\"><sql>\r\n");
	g_string_append (xml, "SELECT ");

	for (i = 0; i < nprops; i++) {
		if (i > 0)
			g_string_append (xml, ", ");
		g_string_append_c (xml, '"');
		g_string_append   (xml, props[i]);
		g_string_append_c (xml, '"');
	}

	if (e2k_restriction_folders_only (rn))
		g_string_append_printf (xml, "\r\nFROM SCOPE('hierarchical traversal of \"\"')\r\n");
	else
		g_string_append (xml, "\r\nFROM \"\"\r\n");

	if (rn) {
		where = e2k_restriction_to_sql (rn);
		if (where) {
			e2k_g_string_append_xml_escaped (xml, where);
			g_string_append (xml, "\r\n");
			g_free (where);
		}
	}

	if (orderby)
		g_string_append_printf (xml, "ORDER BY \"%s\"\r\n", orderby);

	g_string_append (xml, "</sql></searchrequest>");

	ret = xml->str;
	g_string_free (xml, FALSE);

	return ret;
}

static gboolean
search_result_get_range (SoupMessage *msg, int *first, int *total)
{
	const char *range, *p;

	range = soup_message_get_header (msg->response_headers,
					 "Content-Range");
	if (!range)
		return FALSE;
	p = strstr (range, "rows ");
	if (!p)
		return FALSE;

	if (first)
		*first = atoi (p + 5);

	if (total) {
		p = strstr (range, "total=");
		if (p)
			*total = atoi (p + 6);
		else
			*total = -1;
	}

	return TRUE;
}

typedef struct {
	char *uri, *xml;
	gboolean ascending;
	int batch_size, next;
} E2kSearchData;

static E2kHTTPStatus
search_fetch (E2kResultIter *iter,
	      E2kContext *ctx, E2kOperation *op,
	      E2kResult **results, int *nresults,
	      int *first, int *total,
	      gpointer user_data)
{
	E2kSearchData *search_data = user_data;
	E2kHTTPStatus status;
	SoupMessage *msg;

	if (search_data->batch_size == 0)
		return E2K_HTTP_OK;

	msg = search_msg (ctx, search_data->uri,
			  SOUP_BUFFER_USER_OWNED, search_data->xml,
			  search_data->batch_size,
			  search_data->ascending, search_data->next);
	status = e2k_context_send_message (ctx, op, msg);
	if (msg->status_code == E2K_HTTP_REQUESTED_RANGE_NOT_SATISFIABLE)
		status = E2K_HTTP_OK;
	else if (status == E2K_HTTP_MULTI_STATUS) {
		search_result_get_range (msg, first, total);
		if (*total == 0)
			goto cleanup;

		e2k_results_from_multistatus (msg, results, nresults);
		if (*total == -1)
			*total = *first + *nresults;

		if (search_data->ascending && *first + *nresults < *total)
			search_data->next = *first + *nresults;
		else if (!search_data->ascending && *first > 0) {
			if (*first >= search_data->batch_size)
				search_data->next = *first - search_data->batch_size;
			else {
				search_data->batch_size = *first;
				search_data->next = 0;
			}
		} else
			search_data->batch_size = 0;
	}

 cleanup:
	g_object_unref (msg);
	return status;
}

static void
search_free (E2kResultIter *iter, gpointer user_data)
{
	E2kSearchData *search_data = user_data;

	g_free (search_data->uri);
	g_free (search_data->xml);
	g_free (search_data);
}

/**
 * e2k_context_search_start:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the folder to search
 * @props: the properties to search for
 * @nprops: size of @props array
 * @rn: the search restriction
 * @orderby: if non-%NULL, the field to sort the search results by
 * @ascending: %TRUE for an ascending search, %FALSE for descending.
 *
 * Begins a SEARCH on @ctx at @uri.
 *
 * Return value: an iterator for returning the search results
 **/
E2kResultIter *
e2k_context_search_start (E2kContext *ctx, E2kOperation *op, const char *uri,
			  const char **props, int nprops, E2kRestriction *rn,
			  const char *orderby, gboolean ascending)
{
	E2kSearchData *search_data;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (props != NULL, NULL);

	search_data = g_new0 (E2kSearchData, 1);
	search_data->uri = g_strdup (uri);
	search_data->xml = search_xml (props, nprops, rn, orderby);
	search_data->ascending = ascending;
	search_data->batch_size = E2K_CONTEXT_MAX_BATCH_SIZE;
	search_data->next = ascending ? 0 : INT_MAX;

	return e2k_result_iter_new (ctx, op, ascending, -1,
				    search_fetch, search_free,
				    search_data);
}



/* DELETE */

static SoupMessage *
delete_msg (E2kContext *ctx, const char *uri)
{
	return e2k_soup_message_new (ctx, uri, "DELETE");
}

/**
 * e2k_context_delete:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: URI to DELETE
 *
 * Attempts to DELETE @uri on @ctx.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_delete (E2kContext *ctx, E2kOperation *op, const char *uri)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);

	msg = delete_msg (ctx, uri);
	status = e2k_context_send_message (ctx, op, msg);

	g_object_unref (msg);
	return status;
}

/* BDELETE */

static SoupMessage *
bdelete_msg (E2kContext *ctx, const char *uri, const char **hrefs, int nhrefs)
{
	SoupMessage *msg;
	GString *xml;
	int i;

	xml = g_string_new (E2K_XML_HEADER "<delete xmlns=\"DAV:\"><target>");

	for (i = 0; i < nhrefs; i++) {
		g_string_append (xml, "<href>");
		e2k_g_string_append_xml_escaped (xml, hrefs[i]);
		g_string_append (xml, "</href>");
	}

	g_string_append (xml, "</target></delete>");

	msg = e2k_soup_message_new_full (ctx, uri, "BDELETE", "text/xml",
					 SOUP_BUFFER_SYSTEM_OWNED,
					 xml->str, xml->len);
	g_string_free (xml, FALSE);

	return msg;
}

static E2kHTTPStatus
bdelete_fetch (E2kResultIter *iter,
	       E2kContext *ctx, E2kOperation *op,
	       E2kResult **results, int *nresults,
	       int *first, int *total,
	       gpointer user_data)
{
	GSList **msgs = user_data;
	E2kHTTPStatus status;
	SoupMessage *msg;

	if (!*msgs)
		return E2K_HTTP_OK;

	msg = (*msgs)->data;
	*msgs = g_slist_remove (*msgs, msg);

	status = e2k_context_send_message (ctx, op, msg);
	if (status == E2K_HTTP_MULTI_STATUS)
		e2k_results_from_multistatus (msg, results, nresults);
	g_object_unref (msg);

	return status;
}

static void
bdelete_free (E2kResultIter *iter, gpointer user_data)
{
	GSList **msgs = user_data, *m;

	for (m = (*msgs); m; m = m->next)
		g_object_unref (m->data);
	g_slist_free (*msgs);
	g_free (msgs);
}

/**
 * e2k_context_bdelete_start:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri, to delete
 * @nhrefs: length of @hrefs
 *
 * Begins a BDELETE (bulk DELETE) operation on @ctx for @hrefs.
 *
 * Return value: an iterator for returning the results
 **/
E2kResultIter *
e2k_context_bdelete_start (E2kContext *ctx, E2kOperation *op,
			   const char *uri, const char **hrefs, int nhrefs)
{
	GSList **msgs;
	int i, batchsize;
	SoupMessage *msg;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (hrefs != NULL, NULL);

	batchsize = (nhrefs + 9) / 10;
	if (batchsize < E2K_CONTEXT_MIN_BATCH_SIZE)
		batchsize = E2K_CONTEXT_MIN_BATCH_SIZE;
	else if (batchsize > E2K_CONTEXT_MAX_BATCH_SIZE)
		batchsize = E2K_CONTEXT_MAX_BATCH_SIZE;

	msgs = g_new0 (GSList *, 1);
	for (i = 0; i < nhrefs; i += batchsize) {
		batchsize = MIN (batchsize, nhrefs - i);
		msg = bdelete_msg (ctx, uri, hrefs + i, batchsize);
		*msgs = g_slist_prepend (*msgs, msg);
	}

	return e2k_result_iter_new (ctx, op, TRUE, nhrefs,
				    bdelete_fetch, bdelete_free,
				    msgs);
}

/* MKCOL */

/**
 * e2k_context_mkcol:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @uri: URI of the new folder
 * @props: properties to set on the new folder, or %NULL
 * @permanent_url: if not %NULL, will contain the permanent URL of the
 * new folder on return
 *
 * Performs a MKCOL operation on @ctx to create @uri, with optional
 * additional properties.
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_mkcol (E2kContext *ctx, E2kOperation *op,
		   const char *uri, E2kProperties *props,
		   char **permanent_url)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (uri != NULL, E2K_HTTP_MALFORMED);

	if (!props)
		msg = e2k_soup_message_new (ctx, uri, "MKCOL");
	else
		msg = patch_msg (ctx, uri, "MKCOL", NULL, 0, props, TRUE);

	status = e2k_context_send_message (ctx, op, msg);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && permanent_url) {
		const char *header;

		header = soup_message_get_header (msg->response_headers,
						  "MS-Exchange-Permanent-URL");
		*permanent_url = g_strdup (header);
	}

	g_object_unref (msg);
	return status;
}

/* BMOVE / BCOPY */

static SoupMessage *
transfer_msg (E2kContext *ctx,
	      const char *source_uri, const char *dest_uri,
	      const char **source_hrefs, int nhrefs,
	      gboolean delete_originals)
{
	SoupMessage *msg;
	GString *xml;
	int i;

	xml = g_string_new (E2K_XML_HEADER);
	g_string_append (xml, delete_originals ? "<move" : "<copy");
	g_string_append (xml, " xmlns=\"DAV:\"><target>");
	for (i = 0; i < nhrefs; i++) {
		g_string_append (xml, "<href>");
		e2k_g_string_append_xml_escaped (xml, source_hrefs[i]);
		g_string_append (xml, "</href>");
	}
	g_string_append (xml, "</target></");
	g_string_append (xml, delete_originals ? "move>" : "copy>");

	msg = e2k_soup_message_new_full (ctx, source_uri,
					 delete_originals ? "BMOVE" : "BCOPY",
					 "text/xml",
					 SOUP_BUFFER_SYSTEM_OWNED,
					 xml->str, xml->len);
	soup_message_add_header (msg->request_headers, "Overwrite", "f");
	soup_message_add_header (msg->request_headers, "Allow-Rename", "t");
	soup_message_add_header (msg->request_headers, "Destination", dest_uri);
	g_string_free (xml, FALSE);

	return msg;
}

static E2kHTTPStatus
transfer_next (E2kResultIter *iter,
	       E2kContext *ctx, E2kOperation *op,
	       E2kResult **results, int *nresults,
	       int *first, int *total,
	       gpointer user_data)
{
	GSList **msgs = user_data;
	SoupMessage *msg;
	E2kHTTPStatus status;

	if (!*msgs)
		return E2K_HTTP_OK;

	msg = (*msgs)->data;
	*msgs = g_slist_remove (*msgs, msg);

	status = e2k_context_send_message (ctx, op, msg);
	if (status == E2K_HTTP_MULTI_STATUS)
		e2k_results_from_multistatus (msg, results, nresults);

	g_object_unref (msg);
	return status;
}

static void
transfer_free (E2kResultIter *iter, gpointer user_data)
{
	GSList **msgs = user_data, *m;

	for (m = *msgs; m; m = m->next)
		g_object_unref (m->data);
	g_slist_free (*msgs);
	g_free (msgs);
}

/**
 * e2k_context_transfer_start:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @source_folder: URI of the source folder
 * @dest_folder: URI of the destination folder
 * @source_hrefs: an array of hrefs to move, relative to @source_folder
 * @delete_originals: whether or not to delete the original objects
 *
 * Starts a BMOVE or BCOPY (depending on @delete_originals) operation
 * on @ctx for @source_folder. The objects in @source_folder described
 * by @source_hrefs will be moved or copied to @dest_folder.
 * e2k_result_iter_next() can be used to check the success or failure
 * of each move/copy. (The #E2K_PR_DAV_LOCATION property for each
 * result will show the new location of the object.)
 *
 * NB: may not work correctly if @source_hrefs contains folders
 *
 * Return value: the iterator for the results
 **/
E2kResultIter *
e2k_context_transfer_start (E2kContext *ctx, E2kOperation *op,
			    const char *source_folder, const char *dest_folder,
			    GPtrArray *source_hrefs, gboolean delete_originals)
{
	GSList **msgs;
	SoupMessage *msg;
	char *dest_uri;
	const char **hrefs;
	int i;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (source_folder != NULL, NULL);
	g_return_val_if_fail (dest_folder != NULL, NULL);
	g_return_val_if_fail (source_hrefs != NULL, NULL);

	dest_uri = e2k_strdup_with_trailing_slash (dest_folder);
	hrefs = (const char **)source_hrefs->pdata;

	msgs = g_new0 (GSList *, 1);
	for (i = 0; i < source_hrefs->len; i += E2K_CONTEXT_MAX_BATCH_SIZE) {
		msg = transfer_msg (ctx, source_folder, dest_uri,
				    hrefs + i, MIN (E2K_CONTEXT_MAX_BATCH_SIZE, source_hrefs->len - i),
				    delete_originals);
		*msgs = g_slist_append (*msgs, msg);
	}
	g_free (dest_uri);

	return e2k_result_iter_new (ctx, op, TRUE, source_hrefs->len,
				    transfer_next, transfer_free,
				    msgs);
}

/**
 * e2k_context_transfer_dir:
 * @ctx: the context
 * @op: pointer to an #E2kOperation to use for cancellation
 * @source_href: URI of the source folder
 * @dest_href: URI of the destination folder
 * @delete_original: whether or not to delete the original folder
 * @permanent_url: if not %NULL, will contain the permanent URL of the
 * new folder on return
 *
 * Performs a MOVE or COPY (depending on @delete_original) operation
 * on @ctx for @source_href. The folder itself will be moved, renamed,
 * or copied to @dest_href (which is the name of the new folder
 * itself, not its parent).
 *
 * Return value: the HTTP status
 **/
E2kHTTPStatus
e2k_context_transfer_dir (E2kContext *ctx, E2kOperation *op,
			  const char *source_href, const char *dest_href,
			  gboolean delete_original,
			  char **permanent_url)
{
	SoupMessage *msg;
	E2kHTTPStatus status;

	g_return_val_if_fail (E2K_IS_CONTEXT (ctx), E2K_HTTP_MALFORMED);
	g_return_val_if_fail (source_href != NULL, E2K_HTTP_MALFORMED);
	g_return_val_if_fail (dest_href != NULL, E2K_HTTP_MALFORMED);

	msg = e2k_soup_message_new (ctx, source_href, delete_original ? "MOVE" : "COPY");
	soup_message_add_header (msg->request_headers, "Overwrite", "f");
	soup_message_add_header (msg->request_headers, "Destination", dest_href);

	status = e2k_context_send_message (ctx, op, msg);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && permanent_url) {
		const char *header;

		header = soup_message_get_header (msg->response_headers,
						  "MS-Exchange-Permanent-URL");
		*permanent_url = g_strdup (header);
	}

	g_object_unref (msg);
	return status;
}


/* Subscriptions */

typedef struct {
	E2kContext *ctx;
	char *uri, *id;
	E2kContextChangeType type;
	int lifetime, min_interval;
	time_t last_notification;

	E2kContextChangeCallback callback;
	gpointer user_data;

	guint renew_timeout;
	SoupMessage *renew_msg;
	guint poll_timeout;
	SoupMessage *poll_msg;
	guint notification_timeout;
} E2kSubscription;

static gboolean
belated_notification (gpointer user_data)
{
	E2kSubscription *sub = user_data;

	sub->notification_timeout = 0;
	sub->callback (sub->ctx, sub->uri, sub->type, sub->user_data);
	return FALSE;
}

static void
maybe_notification (E2kSubscription *sub)
{
	time_t now = time (NULL);
	int delay = sub->last_notification + sub->min_interval - now;

	if (delay > 0) {
		if (sub->notification_timeout)
			g_source_remove (sub->notification_timeout);
		sub->notification_timeout = g_timeout_add (delay * 1000,
							   belated_notification,
							   sub);
		return;
	}
	sub->last_notification = now;

	sub->callback (sub->ctx, sub->uri, sub->type, sub->user_data);
}

static void
polled (SoupMessage *msg, gpointer user_data)
{
	E2kSubscription *sub = user_data;
	E2kContext *ctx = sub->ctx;
	E2kResult *results;
	int nresults, i;
	xmlNode *ids;
	char *id;

	sub->poll_msg = NULL;
	if (msg->status_code != E2K_HTTP_MULTI_STATUS) {
		g_warning ("Unexpected error %d %s from POLL",
			   msg->status_code, msg->reason_phrase);
		return;
	}

	e2k_results_from_multistatus (msg, &results, &nresults);
	for (i = 0; i < nresults; i++) {
		if (results[i].status != E2K_HTTP_OK)
			continue;

		ids = e2k_properties_get_prop (results[i].props, E2K_PR_SUBSCRIPTION_ID);
		if (!ids)
			continue;
		for (ids = ids->xmlChildrenNode; ids; ids = ids->next) {
			if (strcmp (ids->name, "li") != 0 ||
			    !ids->xmlChildrenNode ||
			    !ids->xmlChildrenNode->content)
				continue;
			id = ids->xmlChildrenNode->content;
			sub = g_hash_table_lookup (ctx->priv->subscriptions_by_id, id);
			if (sub)
				maybe_notification (sub);
		}
	}
	e2k_results_free (results, nresults);
}

static gboolean
timeout_notification (gpointer user_data)
{
	E2kSubscription *sub = user_data, *sub2;
	E2kContext *ctx = sub->ctx;
	GList *sub_list;
	GString *subscription_ids;

	sub->poll_timeout = 0;
	subscription_ids = g_string_new (sub->id);

	/* Find all subscriptions at this URI that are awaiting a
	 * POLL so we can POLL them all at once.
	 */
	sub_list = g_hash_table_lookup (ctx->priv->subscriptions_by_uri,
					sub->uri);
	for (; sub_list; sub_list = sub_list->next) {
		sub2 = sub_list->data;
		if (sub2 == sub)
			continue;
		if (!sub2->poll_timeout)
			continue;
		g_source_remove (sub2->poll_timeout);
		sub2->poll_timeout = 0;
		g_string_append_printf (subscription_ids, ",%s", sub2->id);
	}

	sub->poll_msg = e2k_soup_message_new (ctx, sub->uri, "POLL");
	soup_message_add_header (sub->poll_msg->request_headers,
				 "Subscription-id", subscription_ids->str);
	e2k_context_queue_message (ctx, sub->poll_msg, polled, sub);

	g_string_free (subscription_ids, TRUE);
	return FALSE;
}

static gboolean
do_notification (GIOChannel *source, GIOCondition condition, gpointer data)
{
	E2kContext *ctx = data;
	E2kSubscription *sub;
	char buffer[1024], *id, *lasts;
	gsize len;
	GIOError err;

	err = g_io_channel_read_chars (source, buffer, sizeof (buffer) - 1, &len, NULL);
	if (err != G_IO_ERROR_NONE && err != G_IO_ERROR_AGAIN) {
		g_warning ("do_notification I/O error: %d (%s)", err,
			   g_strerror (errno));
		return FALSE;
	}
	buffer[len] = '\0';

#ifdef E2K_DEBUG
	if (e2k_debug_level) {
		if (e2k_debug_level == 1) {
			fwrite (buffer, 1, strcspn (buffer, "\r\n"), stdout);
			fputs ("\n\n", stdout);
		} else
			fputs (buffer, stdout);
	}
#endif

	if (g_ascii_strncasecmp (buffer, "NOTIFY ", 7) != 0)
		return TRUE;

	id = buffer;
	while (1) {
		id = strchr (id, '\n');
		if (!id++)
			return TRUE;
		if (g_ascii_strncasecmp (id, "Subscription-id: ", 17) == 0)
			break;
	}
	id += 17;

	for (id = strtok_r (id, ",\r", &lasts); id; id = strtok_r (NULL, ",\r", &lasts)) {
		sub = g_hash_table_lookup (ctx->priv->subscriptions_by_id, id);
		if (!sub)
			continue;

		/* We don't want to POLL right away in case there are
		 * several changes in a row. So we just bump up the
		 * timeout to be one second from now. (Using an idle
		 * handler here doesn't actually work to prevent
		 * multiple POLLs.)
		 */
		if (sub->poll_timeout)
			g_source_remove (sub->poll_timeout);
		sub->poll_timeout =
			g_timeout_add (1000, timeout_notification, sub);
	}

	return TRUE;
}

static void
renew_cb (SoupMessage *msg, gpointer user_data)
{
	E2kSubscription *sub = user_data;

	sub->renew_msg = NULL;
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		g_warning ("renew_subscription: %d %s", msg->status_code,
			   msg->reason_phrase);
		return;
	}

	if (sub->id) {
		g_hash_table_remove (sub->ctx->priv->subscriptions_by_id, sub->id);
		g_free (sub->id);
	}
	sub->id = g_strdup (soup_message_get_header (msg->response_headers,
						     "Subscription-id"));
	g_return_if_fail (sub->id != NULL);
	g_hash_table_insert (sub->ctx->priv->subscriptions_by_id,
			     sub->id, sub);
}

#define E2K_SUBSCRIPTION_INITIAL_LIFETIME  3600 /*  1 hour  */
#define E2K_SUBSCRIPTION_MAX_LIFETIME     57600 /* 16 hours */

/* This must be kept in sync with E2kSubscriptionType */
static char *subscription_type[] = {
	"update",		/* E2K_SUBSCRIPTION_OBJECT_CHANGED */
	"update/newmember",	/* E2K_SUBSCRIPTION_OBJECT_ADDED */
	"delete",		/* E2K_SUBSCRIPTION_OBJECT_REMOVED */
	"move"			/* E2K_SUBSCRIPTION_OBJECT_MOVED */
};

static gboolean
renew_subscription (gpointer user_data)
{
	E2kSubscription *sub = user_data;
	E2kContext *ctx = sub->ctx;
	char ltbuf[80];

	if (!ctx->priv->notification_uri)
		return FALSE;

	if (sub->lifetime < E2K_SUBSCRIPTION_MAX_LIFETIME)
		sub->lifetime *= 2;

	sub->renew_msg = e2k_soup_message_new (ctx, sub->uri, "SUBSCRIBE");
	sprintf (ltbuf, "%d", sub->lifetime);
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Subscription-lifetime", ltbuf);
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Notification-type",
				 subscription_type[sub->type]);
	if (sub->min_interval > 1) {
		sprintf (ltbuf, "%d", sub->min_interval);
		soup_message_add_header (sub->renew_msg->request_headers,
					 "Notification-delay", ltbuf);
	}
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Call-back", ctx->priv->notification_uri);

	e2k_context_queue_message (ctx, sub->renew_msg, renew_cb, sub);
	sub->renew_timeout = g_timeout_add ((sub->lifetime - 60) * 1000,
					    renew_subscription, sub);
	return FALSE;
}

/**
 * e2k_context_subscribe:
 * @ctx: the context
 * @uri: the folder URI to subscribe to notifications on
 * @type: the type of notification to subscribe to
 * @min_interval: the minimum interval (in seconds) between
 * notifications.
 * @callback: the callback to call when a notification has been
 * received
 * @user_data: data to pass to @callback.
 *
 * This subscribes to change notifications of the given @type on @uri.
 * @callback will (eventually) be invoked any time the folder changes
 * in the given way: whenever an object is added to it for
 * %E2K_CONTEXT_OBJECT_ADDED, whenever an object is deleted (but
 * not moved) from it (or the folder itself is deleted) for
 * %E2K_CONTEXT_OBJECT_REMOVED, whenever an object is moved in or
 * out of the folder for %E2K_CONTEXT_OBJECT_MOVED, and whenever
 * any of the above happens, or the folder or one of its items is
 * modified, for %E2K_CONTEXT_OBJECT_CHANGED. (This means that if
 * you subscribe to both CHANGED and some other notification on the
 * same folder that multiple callbacks may be invoked every time an
 * object is added/removed/moved/etc.)
 *
 * Notifications can be used *only* to discover changes made by other
 * clients! The code cannot assume that it will receive a notification
 * for every change that it makes to the server, for two reasons:
 * 
 * First, if multiple notifications occur within @min_interval seconds
 * of each other, the later ones will be suppressed, to avoid
 * excessive traffic between the client and the server as the client
 * tries to sync. Second, if there is a firewall between the client
 * and the server, it is possible that all notifications will be lost.
 **/
void
e2k_context_subscribe (E2kContext *ctx, const char *uri,
		       E2kContextChangeType type, int min_interval,
		       E2kContextChangeCallback callback,
		       gpointer user_data)
{
	E2kSubscription *sub;
	GList *sub_list;
	gpointer key, value;

	g_return_if_fail (E2K_IS_CONTEXT (ctx));

	sub = g_new0 (E2kSubscription, 1);
	sub->ctx = ctx;
	sub->uri = g_strdup (uri);
	sub->type = type;
	sub->lifetime = E2K_SUBSCRIPTION_INITIAL_LIFETIME / 2;
	sub->min_interval = min_interval;
	sub->callback = callback;
	sub->user_data = user_data;

	if (g_hash_table_lookup_extended (ctx->priv->subscriptions_by_uri,
					  uri, &key, &value)) {
		sub_list = value;
		sub_list = g_list_prepend (sub_list, sub);
		g_hash_table_insert (ctx->priv->subscriptions_by_uri,
				     key, sub_list);
	} else {
		g_hash_table_insert (ctx->priv->subscriptions_by_uri,
				     sub->uri, g_list_prepend (NULL, sub));
	}

	renew_subscription (sub);
}

static void
free_subscription (E2kSubscription *sub)
{
	SoupSession *session = sub->ctx->priv->session;

	if (sub->renew_timeout)
		g_source_remove (sub->renew_timeout);
	if (sub->renew_msg)
		soup_session_cancel_message (session, sub->renew_msg);
	if (sub->poll_timeout)
		g_source_remove (sub->poll_timeout);
	if (sub->notification_timeout)
		g_source_remove (sub->notification_timeout);
	if (sub->poll_msg)
		soup_session_cancel_message (session, sub->poll_msg);
	g_free (sub->uri);
	g_free (sub->id);
	g_free (sub);
}

static void
unsubscribed (SoupMessage *msg, gpointer user_data)
{
	;
}

static void
unsubscribe_internal (E2kContext *ctx, const char *uri, GList *sub_list)
{
	GList *l;
	E2kSubscription *sub;
	SoupMessage *msg;
	GString *subscription_ids = NULL;

	for (l = sub_list; l; l = l->next) {
		sub = l->data;
		if (sub->id) {
			if (!subscription_ids)
				subscription_ids = g_string_new (sub->id);
			else {
				g_string_append_printf (subscription_ids,
							",%s", sub->id);
			}
			g_hash_table_remove (ctx->priv->subscriptions_by_id, sub->id);
		}
		free_subscription (sub);
	}

	if (subscription_ids) {
		msg = e2k_soup_message_new (ctx, uri, "UNSUBSCRIBE");
		soup_message_add_header (msg->request_headers,
					 "Subscription-id",
					 subscription_ids->str);
		e2k_context_queue_message (ctx, msg, unsubscribed, NULL);
		g_string_free (subscription_ids, TRUE);
	}
}

/**
 * e2k_context_unsubscribe:
 * @ctx: the context
 * @uri: the URI to unsubscribe from
 *
 * Unsubscribes to all notifications on @ctx for @uri.
 **/
void
e2k_context_unsubscribe (E2kContext *ctx, const char *uri)
{
	GList *sub_list;

	g_return_if_fail (E2K_IS_CONTEXT (ctx));

	sub_list = g_hash_table_lookup (ctx->priv->subscriptions_by_uri, uri);
	g_hash_table_remove (ctx->priv->subscriptions_by_uri, uri);
	unsubscribe_internal (ctx, uri, sub_list);
	g_list_free (sub_list);
}

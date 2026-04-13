/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "camel-jmap-folder.h"
#include "camel-jmap-settings.h"
#include "camel-jmap-store.h"

#define JMAP_WELL_KNOWN_PATH "/.well-known/jmap"
#define JMAP_CONTENT_TYPE "application/json"

struct _CamelJmapStore {
	CamelStore parent;

	GMutex session_lock;
	SoupSession *soup_session;

	/* JMAP session resource fields, populated on connect */
	gchar *api_url;
	gchar *account_id;

	/* Folder info cache */
	GMutex mailbox_lock;
	GHashTable *mailbox_id_by_name; /* gchar *full_name -> gchar *id */
	GHashTable *mailbox_name_by_id; /* gchar *id -> gchar *full_name */

	/* WebSocket transport (RFC 8887) */
	gchar *ws_url;
	SoupWebsocketConnection *ws_connection;
	GMainContext *ws_context;
	GMainLoop *ws_loop;
	GThread *ws_thread;
	GMutex ws_lock;             /* protects ws_connection, ws_pending_calls, ws_request_counter */
	GHashTable *ws_pending_calls; /* gchar *id -> JmapWsCall* */
	guint ws_request_counter;
};

enum {
	PROP_0,
	N_PROPS,
	PROP_CONNECTABLE,
	PROP_HOST_REACHABLE
};

static void jmap_store_network_service_init (CamelNetworkServiceInterface *iface);

/* ---- WebSocket support (RFC 8887) ---- */

/* Per-call synchronisation state for jmap_store_ws_call_sync. */
typedef struct {
	GMutex mutex;
	GCond cond;
	JsonNode *result; /* (transfer full), set on success */
	GError *error;    /* set on failure */
	gboolean done;
} JmapWsCall;

/* Shared state used while jmap_store_ws_connect_sync waits for the
 * WebSocket handshake to complete on the background thread. */
typedef struct {
	CamelJmapStore *store;
	GCancellable *cancellable;
	/* Written by the WebSocket thread, read by the caller: */
	GMutex mutex;
	GCond cond;
	gboolean done;
	GError *error;
} JmapWsConnectData;

/* Payload passed to jmap_store_ws_do_send via g_main_context_invoke_full. */
typedef struct {
	SoupWebsocketConnection *ws;
	gchar *json_text;
} JmapWsSendData;

static void jmap_store_ws_disconnect (CamelJmapStore *store);
static void jmap_store_ws_connect_cb (GObject *source, GAsyncResult *result, gpointer user_data);

G_DEFINE_TYPE_WITH_CODE (
	CamelJmapStore,
	camel_jmap_store,
	CAMEL_TYPE_STORE,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SERVICE,
		jmap_store_network_service_init))

static void
jmap_store_set_auth_header (CamelJmapStore *store,
                             SoupMessage *message)
{
	CamelService *service;
	CamelSettings *settings;
	CamelJmapSettings *jmap_settings;

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);
	jmap_settings = CAMEL_JMAP_SETTINGS (settings);

	if (camel_jmap_settings_get_use_bearer_token (jmap_settings)) {
		gchar *bearer_token;
		gchar *auth_value;

		bearer_token = camel_jmap_settings_dup_bearer_token (jmap_settings);
		if (bearer_token && *bearer_token) {
			auth_value = g_strdup_printf ("Bearer %s", bearer_token);
			soup_message_headers_replace (
				soup_message_get_request_headers (message),
				"Authorization", auth_value);
			g_free (auth_value);
		}
		g_free (bearer_token);
	} else {
		CamelNetworkSettings *network_settings;
		gchar *user, *password, *credentials, *encoded, *auth_value;

		network_settings = CAMEL_NETWORK_SETTINGS (settings);
		user = camel_network_settings_dup_user (network_settings);
		password = camel_service_dup_password (service);

		if (user && password) {
			credentials = g_strdup_printf ("%s:%s", user, password);
			encoded = g_base64_encode ((guchar *) credentials, strlen (credentials));
			auth_value = g_strdup_printf ("Basic %s", encoded);
			soup_message_headers_replace (
				soup_message_get_request_headers (message),
				"Authorization", auth_value);
			g_free (auth_value);
			g_free (encoded);
			g_free (credentials);
		}
		g_free (user);
		g_free (password);
	}

	g_object_unref (settings);
}

/* Builds the base URL for the JMAP server from settings. */
static gchar *
jmap_store_build_base_url (CamelJmapStore *store)
{
	CamelService *service;
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	CamelNetworkSecurityMethod security_method;
	gchar *host, *url;
	guint16 port;
	const gchar *scheme;

	service = CAMEL_SERVICE (store);
	settings = camel_service_ref_settings (service);
	network_settings = CAMEL_NETWORK_SETTINGS (settings);

	host = camel_network_settings_dup_host (network_settings);
	port = camel_network_settings_get_port (network_settings);
	security_method = camel_network_settings_get_security_method (network_settings);

	if (security_method == CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT ||
	    security_method == CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT)
		scheme = "https";
	else
		scheme = "http";

	if ((g_strcmp0 (scheme, "https") == 0 && port == 443) ||
	    (g_strcmp0 (scheme, "http") == 0 && port == 80))
		url = g_strdup_printf ("%s://%s", scheme, host);
	else
		url = g_strdup_printf ("%s://%s:%u", scheme, host, (guint) port);

	g_free (host);
	g_object_unref (settings);

	return url;
}

/* Performs a synchronous HTTP GET. Returns a newly-allocated response body or NULL on error. */
static gchar *
jmap_store_http_get_sync (CamelJmapStore *store,
                           const gchar *url,
                           GCancellable *cancellable,
                           GError **error)
{
	SoupMessage *message;
	GBytes *body;
	gchar *response_text;

	message = soup_message_new (SOUP_METHOD_GET, url);
	if (!message) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Invalid JMAP URL: %s"), url);
		return NULL;
	}

	jmap_store_set_auth_header (store, message);

	body = soup_session_send_and_read (
		store->soup_session, message, cancellable, error);

	if (!body) {
		g_object_unref (message);
		return NULL;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("JMAP server returned HTTP %u for %s"),
			soup_message_get_status (message), url);
		g_bytes_unref (body);
		g_object_unref (message);
		return NULL;
	}

	response_text = g_strndup (g_bytes_get_data (body, NULL), g_bytes_get_size (body));
	g_bytes_unref (body);
	g_object_unref (message);

	return response_text;
}

/* Performs a synchronous HTTP POST with a JSON body. Returns parsed response or NULL on error. */
static JsonNode *
jmap_store_http_post_json_sync (CamelJmapStore *store,
                                 const gchar *url,
                                 JsonNode *request_node,
                                 GCancellable *cancellable,
                                 GError **error)
{
	SoupMessage *message;
	JsonGenerator *generator;
	gchar *request_body;
	gsize request_len;
	GBytes *body;
	JsonParser *parser;
	JsonNode *response_node;

	message = soup_message_new (SOUP_METHOD_POST, url);
	if (!message) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Invalid JMAP API URL: %s"), url);
		return NULL;
	}

	generator = json_generator_new ();
	json_generator_set_root (generator, request_node);
	request_body = json_generator_to_data (generator, &request_len);
	g_object_unref (generator);

	soup_message_set_request_body_from_bytes (
		message,
		JMAP_CONTENT_TYPE,
		g_bytes_new_take (request_body, request_len));

	soup_message_headers_replace (
		soup_message_get_request_headers (message),
		"Accept", JMAP_CONTENT_TYPE);

	jmap_store_set_auth_header (store, message);

	body = soup_session_send_and_read (
		store->soup_session, message, cancellable, error);

	if (!body) {
		g_object_unref (message);
		return NULL;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("JMAP server returned HTTP %u"),
			soup_message_get_status (message));
		g_bytes_unref (body);
		g_object_unref (message);
		return NULL;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser,
	    g_bytes_get_data (body, NULL),
	    g_bytes_get_size (body),
	    error)) {
		g_object_unref (parser);
		g_bytes_unref (body);
		g_object_unref (message);
		return NULL;
	}

	response_node = json_node_ref (json_parser_get_root (parser));
	g_object_unref (parser);
	g_bytes_unref (body);
	g_object_unref (message);

	return response_node;
}

/* ---- WebSocket helpers ---- */

static JmapWsCall *
jmap_ws_call_new (void)
{
	JmapWsCall *call = g_new0 (JmapWsCall, 1);
	g_mutex_init (&call->mutex);
	g_cond_init (&call->cond);
	return call;
}

static void
jmap_ws_call_free (JmapWsCall *call)
{
	g_clear_pointer (&call->result, json_node_unref);
	g_clear_error (&call->error);
	g_mutex_clear (&call->mutex);
	g_cond_clear (&call->cond);
	g_free (call);
}

/* Signal all pending WebSocket calls with an error. Caller must NOT hold ws_lock. */
static void
jmap_store_ws_fail_all_pending (CamelJmapStore *store,
                                const gchar *reason)
{
	GList *calls, *l;

	g_mutex_lock (&store->ws_lock);
	calls = g_hash_table_get_values (store->ws_pending_calls);
	g_hash_table_remove_all (store->ws_pending_calls);
	g_mutex_unlock (&store->ws_lock);

	for (l = calls; l; l = l->next) {
		JmapWsCall *call = l->data;

		g_mutex_lock (&call->mutex);
		if (!call->done) {
			g_set_error_literal (&call->error,
				CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_NOT_CONNECTED,
				reason);
			call->done = TRUE;
			g_cond_signal (&call->cond);
		}
		g_mutex_unlock (&call->mutex);
	}

	g_list_free (calls);
}

/* Called on the WebSocket thread when a text or binary frame arrives. */
static void
jmap_store_ws_message_cb (SoupWebsocketConnection *ws,
                          SoupWebsocketDataType type,
                          GBytes *message,
                          gpointer user_data)
{
	CamelJmapStore *store = user_data;
	const gchar *data;
	gsize len;
	JsonParser *parser;
	JsonObject *root_obj;
	const gchar *at_type, *request_id;
	JmapWsCall *call = NULL;

	if (type != SOUP_WEBSOCKET_DATA_TEXT)
		return;

	data = g_bytes_get_data (message, &len);

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, data, (gssize) len, NULL)) {
		g_object_unref (parser);
		return;
	}

	root_obj = json_node_get_object (json_parser_get_root (parser));
	if (!root_obj) {
		g_object_unref (parser);
		return;
	}

	at_type    = json_object_get_string_member_with_default (root_obj, "@type",     NULL);
	request_id = json_object_get_string_member_with_default (root_obj, "requestId", NULL);

	if (!request_id) {
		/* Could be a push StateChange notification — ignore for now. */
		g_object_unref (parser);
		return;
	}

	/* Look up the pending call while holding ws_lock, then transfer to
	 * call->mutex before releasing ws_lock so the calling thread cannot
	 * free the call between the lookup and the signal. */
	g_mutex_lock (&store->ws_lock);
	call = g_hash_table_lookup (store->ws_pending_calls, request_id);
	if (call)
		g_mutex_lock (&call->mutex);
	g_mutex_unlock (&store->ws_lock);

	if (call) {
		if (g_strcmp0 (at_type, "Response") == 0) {
			call->result = json_node_ref (json_parser_get_root (parser));
		} else if (g_strcmp0 (at_type, "RequestError") == 0) {
			const gchar *detail;
			detail = json_object_get_string_member_with_default (root_obj,
				"detail", _("JMAP WebSocket request error"));
			g_set_error_literal (&call->error,
				CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_INVALID,
				detail);
		}
		call->done = TRUE;
		g_cond_signal (&call->cond);
		g_mutex_unlock (&call->mutex);
	}

	g_object_unref (parser);
}

/* Called on the WebSocket thread when the connection is closed. */
static void
jmap_store_ws_closed_cb (SoupWebsocketConnection *ws,
                         gpointer user_data)
{
	CamelJmapStore *store = user_data;

	jmap_store_ws_fail_all_pending (store, _("JMAP WebSocket connection closed"));

	g_mutex_lock (&store->ws_lock);
	if (store->ws_connection == ws)
		g_clear_object (&store->ws_connection);
	g_mutex_unlock (&store->ws_lock);
}

/* Called on the WebSocket thread when a protocol error occurs. */
static void
jmap_store_ws_error_cb (SoupWebsocketConnection *ws,
                        GError *error,
                        gpointer user_data)
{
	CamelJmapStore *store = user_data;
	gchar *reason;

	reason = g_strdup_printf (_("JMAP WebSocket error: %s"), error->message);
	jmap_store_ws_fail_all_pending (store, reason);
	g_free (reason);
}

/* Background thread: runs the GMainLoop that drives WebSocket I/O. */
static gpointer
jmap_store_ws_thread_func (gpointer data)
{
	CamelJmapStore *store = data;

	g_main_context_push_thread_default (store->ws_context);
	g_main_loop_run (store->ws_loop);
	g_main_context_pop_thread_default (store->ws_context);

	return NULL;
}

/* Invoked on the WebSocket thread via g_main_context_invoke_full.
 * Initiates the async WebSocket handshake and signals the caller when done. */
static gboolean
jmap_store_ws_do_connect (gpointer user_data)
{
	JmapWsConnectData *cd = user_data;
	CamelJmapStore *store = cd->store;
	SoupMessage *msg;
	const gchar *protocols[] = { "jmap", NULL };

	if (!store->ws_url) {
		g_mutex_lock (&cd->mutex);
		g_set_error_literal (&cd->error,
			CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("No JMAP WebSocket URL available"));
		cd->done = TRUE;
		g_cond_signal (&cd->cond);
		g_mutex_unlock (&cd->mutex);
		return G_SOURCE_REMOVE;
	}

	msg = soup_message_new ("GET", store->ws_url);
	if (!msg) {
		g_mutex_lock (&cd->mutex);
		g_set_error (&cd->error,
			CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Invalid JMAP WebSocket URL: %s"), store->ws_url);
		cd->done = TRUE;
		g_cond_signal (&cd->cond);
		g_mutex_unlock (&cd->mutex);
		return G_SOURCE_REMOVE;
	}

	jmap_store_set_auth_header (store, msg);

	soup_session_websocket_connect_async (
		store->soup_session,
		msg,
		NULL,
		(gchar **) protocols,
		G_PRIORITY_DEFAULT,
		cd->cancellable,
		jmap_store_ws_connect_cb,
		cd);

	g_object_unref (msg);
	return G_SOURCE_REMOVE;
}

/* Callback invoked on the WebSocket thread once the handshake completes. */
static void
jmap_store_ws_connect_cb (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	JmapWsConnectData *cd = user_data;
	CamelJmapStore *store = cd->store;
	SoupWebsocketConnection *ws;

	ws = soup_session_websocket_connect_finish (
		SOUP_SESSION (source), result, &cd->error);

	if (ws) {
		g_signal_connect (ws, "message",
			G_CALLBACK (jmap_store_ws_message_cb), store);
		g_signal_connect (ws, "closed",
			G_CALLBACK (jmap_store_ws_closed_cb), store);
		g_signal_connect (ws, "error",
			G_CALLBACK (jmap_store_ws_error_cb), store);

		g_mutex_lock (&store->ws_lock);
		g_clear_object (&store->ws_connection);
		store->ws_connection = ws; /* transfer ownership */
		g_mutex_unlock (&store->ws_lock);
	}

	g_mutex_lock (&cd->mutex);
	cd->done = TRUE;
	g_cond_signal (&cd->cond);
	g_mutex_unlock (&cd->mutex);
}

/* Connects to the JMAP WebSocket endpoint.  Blocks until the handshake
 * completes (or fails).  Must not be called while already connected. */
static gboolean
jmap_store_ws_connect_sync (CamelJmapStore *store,
                             GCancellable *cancellable,
                             GError **error)
{
	JmapWsConnectData cd = { .store = store, .cancellable = cancellable, .done = FALSE };

	g_mutex_init (&cd.mutex);
	g_cond_init (&cd.cond);

	store->ws_context = g_main_context_new ();
	store->ws_loop    = g_main_loop_new (store->ws_context, FALSE);
	store->ws_thread  = g_thread_new ("jmap-ws",
		jmap_store_ws_thread_func, store);

	/* Ask the WebSocket thread to start the handshake. */
	g_main_context_invoke_full (store->ws_context,
		G_PRIORITY_DEFAULT,
		jmap_store_ws_do_connect,
		&cd, NULL);

	/* Wait for the handshake to complete or fail. */
	g_mutex_lock (&cd.mutex);
	while (!cd.done)
		g_cond_wait (&cd.cond, &cd.mutex);
	g_mutex_unlock (&cd.mutex);

	g_mutex_clear (&cd.mutex);
	g_cond_clear (&cd.cond);

	if (cd.error) {
		g_propagate_error (error, cd.error);

		/* Tear down the event loop — nothing useful is connected. */
		g_main_loop_quit (store->ws_loop);
		g_thread_join (store->ws_thread);
		store->ws_thread = NULL;
		g_main_loop_unref (store->ws_loop);
		store->ws_loop = NULL;
		g_main_context_unref (store->ws_context);
		store->ws_context = NULL;

		return FALSE;
	}

	return TRUE;
}

/* Closes the WebSocket connection and stops the background event thread.
 * Safe to call even if WebSocket is not connected. */
static void
jmap_store_ws_disconnect (CamelJmapStore *store)
{
	if (!store->ws_loop)
		return;

	/* Close the connection gracefully; this will trigger the "closed"
	 * signal which fails all pending calls. */
	g_mutex_lock (&store->ws_lock);
	if (store->ws_connection &&
	    soup_websocket_connection_get_state (store->ws_connection)
	        == SOUP_WEBSOCKET_STATE_OPEN) {
		soup_websocket_connection_close (store->ws_connection,
			SOUP_WEBSOCKET_CLOSE_NORMAL, NULL);
	}
	g_clear_object (&store->ws_connection);
	g_mutex_unlock (&store->ws_lock);

	/* Ensure any stragglers are also failed. */
	jmap_store_ws_fail_all_pending (store, _("JMAP WebSocket disconnected"));

	g_main_loop_quit (store->ws_loop);
	g_thread_join (store->ws_thread);
	store->ws_thread = NULL;

	g_main_loop_unref (store->ws_loop);
	store->ws_loop = NULL;

	g_main_context_unref (store->ws_context);
	store->ws_context = NULL;
}

/* Invoked on the WebSocket thread via g_main_context_invoke_full to send a
 * single text frame.  Frees the JmapWsSendData itself. */
static gboolean
jmap_store_ws_do_send (gpointer user_data)
{
	JmapWsSendData *sd = user_data;

	if (soup_websocket_connection_get_state (sd->ws) == SOUP_WEBSOCKET_STATE_OPEN)
		soup_websocket_connection_send_text (sd->ws, sd->json_text);

	g_object_unref (sd->ws);
	g_free (sd->json_text);
	g_free (sd);

	return G_SOURCE_REMOVE;
}

/* Wraps a plain JMAP request node in the RFC 8887 WebSocket envelope:
 *   { "@type": "Request", "id": "<id>", <original members...> }
 * Returns a newly-allocated JSON string. */
static gchar *
jmap_store_ws_build_envelope (JsonNode *request,
                               const gchar *id)
{
	JsonObject *req_obj = json_node_get_object (request);
	JsonBuilder *builder;
	JsonNode *wrapped;
	JsonGenerator *gen;
	GList *members, *l;
	gchar *json_text;

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "@type");
	json_builder_add_string_value (builder, "Request");

	json_builder_set_member_name (builder, "id");
	json_builder_add_string_value (builder, id);

	members = json_object_get_members (req_obj);
	for (l = members; l; l = l->next) {
		const gchar *name = l->data;

		json_builder_set_member_name (builder, name);
		json_builder_add_value (builder,
			json_node_ref (json_object_get_member (req_obj, name)));
	}
	g_list_free (members);

	json_builder_end_object (builder);

	wrapped = json_builder_get_root (builder);
	g_object_unref (builder);

	gen = json_generator_new ();
	json_generator_set_root (gen, wrapped);
	json_text = json_generator_to_data (gen, NULL);
	g_object_unref (gen);
	json_node_unref (wrapped);

	return json_text;
}

/* Performs a synchronous JMAP API call over the WebSocket connection.
 * Wraps the request per RFC 8887, sends it, and waits (up to 60 s) for the
 * matching response before returning. */
static JsonNode *
jmap_store_ws_call_sync (CamelJmapStore *store,
                          JsonNode *request,
                          GCancellable *cancellable,
                          GError **error)
{
	JmapWsCall *call;
	JmapWsSendData *sd;
	SoupWebsocketConnection *ws = NULL;
	JsonNode *result = NULL;
	gchar *call_id;
	gchar *json_text;
	gint64 deadline;

	g_mutex_lock (&store->ws_lock);
	if (store->ws_connection)
		ws = g_object_ref (store->ws_connection);
	g_mutex_unlock (&store->ws_lock);

	if (!ws) {
		g_set_error_literal (error,
			CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("JMAP WebSocket is not connected"));
		return NULL;
	}

	call_id = g_strdup_printf ("r%u", g_atomic_int_add ((gint *) &store->ws_request_counter, 1));
	call = jmap_ws_call_new ();

	/* Register the pending call before sending so that fast responses are
	 * never missed. */
	g_mutex_lock (&store->ws_lock);
	g_hash_table_insert (store->ws_pending_calls, g_strdup (call_id), call);
	g_mutex_unlock (&store->ws_lock);

	json_text = jmap_store_ws_build_envelope (request, call_id);

	sd = g_new (JmapWsSendData, 1);
	sd->ws        = g_object_ref (ws);
	sd->json_text = json_text; /* transfer */

	/* Dispatch the send on the WebSocket thread for thread-safety. */
	g_main_context_invoke_full (store->ws_context,
		G_PRIORITY_DEFAULT,
		jmap_store_ws_do_send,
		sd, NULL);

	/* Wait for the response, with a generous timeout. */
	deadline = g_get_monotonic_time () + 60 * G_TIME_SPAN_SECOND;

	g_mutex_lock (&call->mutex);
	while (!call->done) {
		if (!g_cond_wait_until (&call->cond, &call->mutex, deadline)) {
			g_set_error_literal (&call->error,
				G_IO_ERROR,
				G_IO_ERROR_TIMED_OUT,
				_("JMAP WebSocket request timed out"));
			call->done = TRUE;
			break;
		}
	}

	if (call->error)
		g_propagate_error (error, g_steal_pointer (&call->error));
	else
		result = g_steal_pointer (&call->result);

	g_mutex_unlock (&call->mutex);

	/* Remove from the pending table (it may have already been removed by
	 * jmap_store_ws_fail_all_pending on disconnect). */
	g_mutex_lock (&store->ws_lock);
	g_hash_table_remove (store->ws_pending_calls, call_id);
	g_mutex_unlock (&store->ws_lock);

	jmap_ws_call_free (call);
	g_free (call_id);
	g_object_unref (ws);

	return result;
}

/* ---- end WebSocket support ---- */

/* Fetches and parses the JMAP session resource at /.well-known/jmap. */
static gboolean
jmap_store_fetch_session (CamelJmapStore *store,
                           GCancellable *cancellable,
                           GError **error)
{
	gchar *base_url, *well_known_url, *response_text;
	JsonParser *parser;
	JsonObject *root_obj;
	JsonObject *accounts_obj;
	const gchar *api_url;
	GList *account_ids;

	base_url = jmap_store_build_base_url (store);
	well_known_url = g_strdup_printf ("%s%s", base_url, JMAP_WELL_KNOWN_PATH);
	g_free (base_url);

	response_text = jmap_store_http_get_sync (store, well_known_url, cancellable, error);
	g_free (well_known_url);

	if (!response_text)
		return FALSE;

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, response_text, -1, error)) {
		g_free (response_text);
		g_object_unref (parser);
		return FALSE;
	}
	g_free (response_text);

	root_obj = json_node_get_object (json_parser_get_root (parser));
	if (!root_obj) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("JMAP session resource is not a JSON object"));
		g_object_unref (parser);
		return FALSE;
	}

	api_url = json_object_get_string_member_with_default (root_obj, "apiUrl", NULL);
	if (!api_url) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("JMAP session resource missing 'apiUrl'"));
		g_object_unref (parser);
		return FALSE;
	}

	g_mutex_lock (&store->session_lock);
	g_free (store->api_url);
	store->api_url = g_strdup (api_url);

	/* Use the first listed account ID as the primary account. */
	accounts_obj = json_object_get_object_member (root_obj, "accounts");
	if (accounts_obj) {
		account_ids = json_object_get_members (accounts_obj);
		if (account_ids) {
			g_free (store->account_id);
			store->account_id = g_strdup (account_ids->data);
			g_list_free (account_ids);
		}
	}

	/* Extract WebSocket URL from the urn:ietf:params:jmap:websocket capability
	 * (RFC 8887 §3.3).  The URL is stored for use in connect_sync. */
	g_free (store->ws_url);
	store->ws_url = NULL;
	{
		JsonObject *capabilities_obj;
		capabilities_obj = json_object_get_object_member (root_obj, "capabilities");
		if (capabilities_obj) {
			JsonObject *ws_cap;
			ws_cap = json_object_get_object_member (capabilities_obj,
				"urn:ietf:params:jmap:websocket");
			if (ws_cap) {
				const gchar *ws_url;
				ws_url = json_object_get_string_member_with_default (
					ws_cap, "url", NULL);
				if (ws_url)
					store->ws_url = g_strdup (ws_url);
			}
		}
	}

	g_mutex_unlock (&store->session_lock);

	g_object_unref (parser);
	return TRUE;
}

static guint16
jmap_store_get_default_port (CamelNetworkService *service,
                              CamelNetworkSecurityMethod method)
{
	if (method == CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT)
		return 443;
	return 80;
}

static void
jmap_store_network_service_init (CamelNetworkServiceInterface *iface)
{
	iface->get_default_port = jmap_store_get_default_port;
}

static gboolean
jmap_store_connect_sync (CamelService *service,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelJmapStore *store = CAMEL_JMAP_STORE (service);

	g_mutex_lock (&store->session_lock);
	if (!store->soup_session) {
		store->soup_session = soup_session_new ();
		soup_session_set_user_agent (store->soup_session,
			"CamelJMAP/" VERSION);
	}
	g_mutex_unlock (&store->session_lock);

	if (!jmap_store_fetch_session (store, cancellable, error))
		return FALSE;

	/* If the server advertises a WebSocket endpoint, upgrade to a
	 * persistent WebSocket connection (RFC 8887). */
	if (store->ws_url) {
		if (!jmap_store_ws_connect_sync (store, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
jmap_store_disconnect_sync (CamelService *service,
                             gboolean clean,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelJmapStore *store = CAMEL_JMAP_STORE (service);

	/* Close WebSocket before releasing the soup session. */
	jmap_store_ws_disconnect (store);

	g_mutex_lock (&store->session_lock);

	g_clear_pointer (&store->api_url, g_free);
	g_clear_pointer (&store->account_id, g_free);

	g_clear_object (&store->soup_session);

	g_mutex_unlock (&store->session_lock);

	return TRUE;
}

static CamelAuthenticationResult
jmap_store_authenticate_sync (CamelService *service,
                               const gchar *mechanism,
                               GCancellable *cancellable,
                               GError **error)
{
	/* JMAP authentication is done via HTTP Authorization headers.
	 * If we got here, connection succeeded which means auth worked. */
	return CAMEL_AUTHENTICATION_ACCEPTED;
}

static gchar *
jmap_store_get_name (CamelService *service,
                      gboolean brief)
{
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	gchar *host, *user, *name;

	settings = camel_service_ref_settings (service);
	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);
	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (_("JMAP server %s"), host);
	else
		name = g_strdup_printf (_("JMAP mail for %s on %s"), user, host);

	g_free (host);
	g_free (user);

	return name;
}

static CamelFolder *
jmap_store_get_folder_sync (CamelStore *store,
                              const gchar *folder_name,
                              CamelStoreGetFolderFlags flags,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelJmapStore *jmap_store = CAMEL_JMAP_STORE (store);
	CamelFolder *folder = NULL;
	const gchar *mailbox_id;

	g_mutex_lock (&jmap_store->mailbox_lock);
	mailbox_id = g_hash_table_lookup (
		jmap_store->mailbox_id_by_name, folder_name);
	g_mutex_unlock (&jmap_store->mailbox_lock);

	if (!mailbox_id && !(flags & CAMEL_STORE_FOLDER_CREATE)) {
		g_set_error (error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return NULL;
	}

	folder = camel_jmap_folder_new (store, folder_name, mailbox_id, cancellable, error);

	return folder;
}

/* Recursively builds CamelFolderInfo from the mailbox list. */
static CamelFolderInfo *
jmap_build_folder_info (GHashTable *id_to_name,
                         GHashTable *id_to_parent_id,
                         GHashTable *id_to_unread,
                         const gchar *parent_id,
                         const gchar *parent_full_name)
{
	CamelFolderInfo *first = NULL, *prev = NULL;
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, id_to_name);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *id = key;
		const gchar *name = value;
		const gchar *pid;
		CamelFolderInfo *fi;
		gchar *full_name;
		gint unread;

		pid = g_hash_table_lookup (id_to_parent_id, id);
		if (g_strcmp0 (pid, parent_id) != 0)
			continue;

		fi = camel_folder_info_new ();
		fi->display_name = g_strdup (name);

		if (parent_full_name)
			full_name = g_strdup_printf ("%s/%s", parent_full_name, name);
		else
			full_name = g_strdup (name);

		fi->full_name = full_name;

		unread = GPOINTER_TO_INT (g_hash_table_lookup (id_to_unread, id));
		fi->unread = unread;
		fi->total = -1;

		fi->child = jmap_build_folder_info (
			id_to_name, id_to_parent_id, id_to_unread,
			id, full_name);

		if (prev)
			prev->next = fi;
		else
			first = fi;
		prev = fi;
	}

	return first;
}

static CamelFolderInfo *
jmap_store_get_folder_info_sync (CamelStore *store,
                                  const gchar *top,
                                  CamelStoreGetFolderInfoFlags flags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelJmapStore *jmap_store = CAMEL_JMAP_STORE (store);
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonArray *mailbox_list;
	JsonObject *get_response;
	GHashTable *id_to_name, *id_to_parent_id, *id_to_unread;
	CamelFolderInfo *folder_info;
	guint ii, len;

	if (!jmap_store->account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return NULL;
	}

	/* Build Mailbox/get request */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Mailbox/get");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, jmap_store->account_id);
					json_builder_set_member_name (builder, "ids");
					json_builder_add_null_value (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return NULL;

	/* Parse response */
	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");

	if (!method_responses || json_array_get_length (method_responses) == 0) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Empty JMAP response for Mailbox/get"));
		json_node_unref (response);
		return NULL;
	}

	get_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);

	mailbox_list = json_object_get_array_member (get_response, "list");
	if (!mailbox_list) {
		json_node_unref (response);
		return NULL;
	}

	id_to_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	id_to_parent_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	id_to_unread = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	g_mutex_lock (&jmap_store->mailbox_lock);
	g_hash_table_remove_all (jmap_store->mailbox_id_by_name);
	g_hash_table_remove_all (jmap_store->mailbox_name_by_id);

	len = json_array_get_length (mailbox_list);
	for (ii = 0; ii < len; ii++) {
		JsonObject *mailbox;
		const gchar *id, *name, *parent_id;
		gint unread;

		mailbox = json_array_get_object_element (mailbox_list, ii);
		id = json_object_get_string_member_with_default (mailbox, "id", NULL);
		name = json_object_get_string_member_with_default (mailbox, "name", NULL);
		parent_id = json_object_get_string_member_with_default (mailbox, "parentId", NULL);
		unread = (gint) json_object_get_int_member_with_default (mailbox, "unreadEmails", 0);

		if (!id || !name)
			continue;

		g_hash_table_insert (id_to_name, g_strdup (id), g_strdup (name));
		g_hash_table_insert (id_to_parent_id, g_strdup (id), g_strdup (parent_id ? parent_id : ""));
		g_hash_table_insert (id_to_unread, g_strdup (id), GINT_TO_POINTER (unread));

		/* Cache the flat name -> id mapping for get_folder_sync */
		g_hash_table_insert (jmap_store->mailbox_id_by_name,
			g_strdup (name), g_strdup (id));
		g_hash_table_insert (jmap_store->mailbox_name_by_id,
			g_strdup (id), g_strdup (name));
	}
	g_mutex_unlock (&jmap_store->mailbox_lock);

	folder_info = jmap_build_folder_info (
		id_to_name, id_to_parent_id, id_to_unread, "", NULL);

	g_hash_table_destroy (id_to_name);
	g_hash_table_destroy (id_to_parent_id);
	g_hash_table_destroy (id_to_unread);
	json_node_unref (response);

	return folder_info;
}

static CamelFolderInfo *
jmap_store_create_folder_sync (CamelStore *store,
                                const gchar *parent_name,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelJmapStore *jmap_store = CAMEL_JMAP_STORE (store);
	JsonBuilder *builder;
	JsonNode *request, *response;
	JsonArray *method_responses;
	JsonObject *set_response, *created, *mailbox;
	CamelFolderInfo *fi;
	const gchar *new_id;
	gchar *full_name;

	if (!jmap_store->account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return NULL;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Mailbox/set");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, jmap_store->account_id);
					json_builder_set_member_name (builder, "create");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "new-folder");
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "name");
							json_builder_add_string_value (builder, folder_name);
							if (parent_name && *parent_name) {
								const gchar *parent_id;

								g_mutex_lock (&jmap_store->mailbox_lock);
								parent_id = g_hash_table_lookup (
									jmap_store->mailbox_id_by_name,
									parent_name);
								if (parent_id) {
									json_builder_set_member_name (builder, "parentId");
									json_builder_add_string_value (builder, parent_id);
								}
								g_mutex_unlock (&jmap_store->mailbox_lock);
							}
						json_builder_end_object (builder);
					json_builder_end_object (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return NULL;

	method_responses = json_object_get_array_member (
		json_node_get_object (response), "methodResponses");
	set_response = json_array_get_object_element (
		json_array_get_array_element (method_responses, 0), 1);
	created = json_object_get_object_member (set_response, "created");

	if (!created) {
		g_set_error (error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("JMAP server did not create the folder"));
		json_node_unref (response);
		return NULL;
	}

	mailbox = json_object_get_object_member (created, "new-folder");
	new_id = mailbox ? json_object_get_string_member_with_default (mailbox, "id", NULL) : NULL;

	if (new_id) {
		g_mutex_lock (&jmap_store->mailbox_lock);
		g_hash_table_insert (jmap_store->mailbox_id_by_name,
			g_strdup (folder_name), g_strdup (new_id));
		g_hash_table_insert (jmap_store->mailbox_name_by_id,
			g_strdup (new_id), g_strdup (folder_name));
		g_mutex_unlock (&jmap_store->mailbox_lock);
	}

	json_node_unref (response);

	if (parent_name && *parent_name)
		full_name = g_strdup_printf ("%s/%s", parent_name, folder_name);
	else
		full_name = g_strdup (folder_name);

	fi = camel_folder_info_new ();
	fi->full_name = full_name;
	fi->display_name = g_strdup (folder_name);
	fi->unread = 0;
	fi->total = 0;

	return fi;
}

static gboolean
jmap_store_delete_folder_sync (CamelStore *store,
                                const gchar *folder_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelJmapStore *jmap_store = CAMEL_JMAP_STORE (store);
	JsonBuilder *builder;
	JsonNode *request, *response;
	gchar *mailbox_id;

	g_mutex_lock (&jmap_store->mailbox_lock);
	mailbox_id = g_strdup (
		g_hash_table_lookup (jmap_store->mailbox_id_by_name, folder_name));
	g_mutex_unlock (&jmap_store->mailbox_lock);

	if (!mailbox_id) {
		g_set_error (error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), folder_name);
		return FALSE;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Mailbox/set");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, jmap_store->account_id);
					json_builder_set_member_name (builder, "destroy");
					json_builder_begin_array (builder);
						json_builder_add_string_value (builder, mailbox_id);
					json_builder_end_array (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);
	g_free (mailbox_id);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return FALSE;

	g_mutex_lock (&jmap_store->mailbox_lock);
	g_hash_table_remove (jmap_store->mailbox_id_by_name, folder_name);
	g_mutex_unlock (&jmap_store->mailbox_lock);

	json_node_unref (response);
	return TRUE;
}

static gboolean
jmap_store_rename_folder_sync (CamelStore *store,
                                const gchar *old_name,
                                const gchar *new_name,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelJmapStore *jmap_store = CAMEL_JMAP_STORE (store);
	JsonBuilder *builder;
	JsonNode *request, *response;
	gchar *mailbox_id;
	const gchar *leaf_name;

	g_mutex_lock (&jmap_store->mailbox_lock);
	mailbox_id = g_strdup (
		g_hash_table_lookup (jmap_store->mailbox_id_by_name, old_name));
	g_mutex_unlock (&jmap_store->mailbox_lock);

	if (!mailbox_id) {
		g_set_error (error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_NO_FOLDER,
			_("No such folder: %s"), old_name);
		return FALSE;
	}

	/* Use only the leaf name part for the JMAP rename */
	leaf_name = strrchr (new_name, '/');
	leaf_name = leaf_name ? leaf_name + 1 : new_name;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Mailbox/set");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, jmap_store->account_id);
					json_builder_set_member_name (builder, "update");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, mailbox_id);
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "name");
							json_builder_add_string_value (builder, leaf_name);
						json_builder_end_object (builder);
					json_builder_end_object (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request = json_builder_get_root (builder);
	g_object_unref (builder);
	g_free (mailbox_id);

	response = camel_jmap_store_call_sync (jmap_store, request, cancellable, error);
	json_node_unref (request);

	if (!response)
		return FALSE;

	json_node_unref (response);
	return TRUE;
}

static void
jmap_store_set_property (GObject *object,
                          guint property_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			camel_network_service_set_connectable (
				CAMEL_NETWORK_SERVICE (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
jmap_store_get_property (GObject *object,
                          guint property_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTABLE:
			g_value_take_object (
				value,
				camel_network_service_ref_connectable (
					CAMEL_NETWORK_SERVICE (object)));
			return;

		case PROP_HOST_REACHABLE:
			g_value_set_boolean (
				value,
				camel_network_service_get_host_reachable (
					CAMEL_NETWORK_SERVICE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
jmap_store_dispose (GObject *object)
{
	CamelJmapStore *store = CAMEL_JMAP_STORE (object);

	/* Ensure WebSocket is cleanly closed before dropping the soup session. */
	jmap_store_ws_disconnect (store);

	g_clear_object (&store->soup_session);

	G_OBJECT_CLASS (camel_jmap_store_parent_class)->dispose (object);
}

static void
jmap_store_finalize (GObject *object)
{
	CamelJmapStore *store = CAMEL_JMAP_STORE (object);

	g_mutex_clear (&store->session_lock);
	g_mutex_clear (&store->mailbox_lock);
	g_mutex_clear (&store->ws_lock);
	g_free (store->api_url);
	g_free (store->account_id);
	g_free (store->ws_url);
	g_hash_table_destroy (store->mailbox_id_by_name);
	g_hash_table_destroy (store->mailbox_name_by_id);
	g_hash_table_destroy (store->ws_pending_calls);

	G_OBJECT_CLASS (camel_jmap_store_parent_class)->finalize (object);
}

static void
camel_jmap_store_class_init (CamelJmapStoreClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelStoreClass *store_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = jmap_store_set_property;
	object_class->get_property = jmap_store_get_property;
	object_class->dispose = jmap_store_dispose;
	object_class->finalize = jmap_store_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_JMAP_SETTINGS;
	service_class->get_name = jmap_store_get_name;
	service_class->connect_sync = jmap_store_connect_sync;
	service_class->disconnect_sync = jmap_store_disconnect_sync;
	service_class->authenticate_sync = jmap_store_authenticate_sync;

	store_class = CAMEL_STORE_CLASS (class);
	store_class->get_folder_sync = jmap_store_get_folder_sync;
	store_class->get_folder_info_sync = jmap_store_get_folder_info_sync;
	store_class->create_folder_sync = jmap_store_create_folder_sync;
	store_class->delete_folder_sync = jmap_store_delete_folder_sync;
	store_class->rename_folder_sync = jmap_store_rename_folder_sync;

	/* Inherited from CamelNetworkService. */
	g_object_class_override_property (
		object_class,
		PROP_CONNECTABLE,
		"connectable");

	/* Inherited from CamelNetworkService. */
	g_object_class_override_property (
		object_class,
		PROP_HOST_REACHABLE,
		"host-reachable");
}

static void
camel_jmap_store_init (CamelJmapStore *store)
{
	g_mutex_init (&store->session_lock);
	g_mutex_init (&store->mailbox_lock);

	store->mailbox_id_by_name = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, g_free);
	store->mailbox_name_by_id = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, g_free);

	g_mutex_init (&store->ws_lock);
	store->ws_pending_calls = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * camel_jmap_store_call_sync:
 * @store: a #CamelJmapStore
 * @request: a #JsonNode containing the JMAP request object
 * @cancellable: optional #GCancellable
 * @error: return location for a #GError, or %NULL
 *
 * Performs a synchronous JMAP API call.  When a WebSocket connection is
 * active (RFC 8887), the request is sent over that connection; otherwise
 * it falls back to an HTTPS POST to the server's @apiUrl.
 *
 * Returns: (transfer full) (nullable): the parsed JSON response node,
 *   or %NULL on error. Free with json_node_unref().
 */
JsonNode *
camel_jmap_store_call_sync (CamelJmapStore *store,
                             JsonNode *request,
                             GCancellable *cancellable,
                             GError **error)
{
	gboolean have_ws;

	g_return_val_if_fail (CAMEL_IS_JMAP_STORE (store), NULL);
	g_return_val_if_fail (request != NULL, NULL);

	/* Prefer the persistent WebSocket connection when available. */
	g_mutex_lock (&store->ws_lock);
	have_ws = (store->ws_connection != NULL &&
		soup_websocket_connection_get_state (store->ws_connection)
			== SOUP_WEBSOCKET_STATE_OPEN);
	g_mutex_unlock (&store->ws_lock);

	if (have_ws)
		return jmap_store_ws_call_sync (store, request, cancellable, error);

	/* Fall back to plain HTTPS. */
	g_mutex_lock (&store->session_lock);
	if (!store->api_url) {
		g_mutex_unlock (&store->session_lock);
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return NULL;
	}
	g_mutex_unlock (&store->session_lock);

	return jmap_store_http_post_json_sync (
		store, store->api_url, request, cancellable, error);
}

/**
 * camel_jmap_store_get_account_id:
 * @store: a #CamelJmapStore
 *
 * Returns the JMAP account ID for the primary mail account.
 * The returned string is owned by @store and must not be freed.
 *
 * Returns: (nullable): the account ID, or %NULL if not connected
 */
const gchar *
camel_jmap_store_get_account_id (CamelJmapStore *store)
{
	g_return_val_if_fail (CAMEL_IS_JMAP_STORE (store), NULL);

	return store->account_id;
}

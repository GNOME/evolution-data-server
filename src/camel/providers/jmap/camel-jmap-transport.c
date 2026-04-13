/*
 * SPDX-FileCopyrightText: 2026 Collabora, Ltd (https://collabora.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "camel-jmap-settings.h"
#include "camel-jmap-transport.h"

struct _CamelJmapTransport {
	CamelTransport parent;

	GMutex session_lock;
	SoupSession *soup_session;

	/* JMAP session resource fields populated on connect */
	gchar *api_url;
	gchar *account_id;
};

G_DEFINE_TYPE (
	CamelJmapTransport,
	camel_jmap_transport,
	CAMEL_TYPE_TRANSPORT)

static void
jmap_transport_set_auth_header (CamelJmapTransport *transport,
                                 SoupMessage *message)
{
	CamelService *service;
	CamelSettings *settings;
	CamelJmapSettings *jmap_settings;

	service = CAMEL_SERVICE (transport);
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

static gchar *
jmap_transport_build_base_url (CamelJmapTransport *transport)
{
	CamelService *service;
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	CamelNetworkSecurityMethod security_method;
	gchar *host, *url;
	guint16 port;
	const gchar *scheme;

	service = CAMEL_SERVICE (transport);
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

	url = g_uri_join(G_URI_FLAGS_NONE, scheme, NULL, host, port, "", NULL, NULL);
	g_free (host);
	g_object_unref (settings);

	return url;
}

static gboolean
jmap_transport_fetch_session (CamelJmapTransport *transport,
                               GCancellable *cancellable,
                               GError **error)
{
	gchar *base_url, *well_known_url;
	SoupMessage *message;
	GBytes *body;
	JsonParser *parser;
	JsonObject *root_obj;
	JsonObject *accounts_obj;
	const gchar *api_url;
	GList *account_ids;

	base_url = jmap_transport_build_base_url (transport);
	well_known_url = g_strdup_printf ("%s/.well-known/jmap", base_url);
	g_free (base_url);

	message = soup_message_new (SOUP_METHOD_GET, well_known_url);
	g_free (well_known_url);

	if (!message) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Invalid JMAP well-known URL"));
		return FALSE;
	}

	jmap_transport_set_auth_header (transport, message);

	body = soup_session_send_and_read (
		transport->soup_session, message, cancellable, error);

	if (!body) {
		g_object_unref (message);
		return FALSE;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (message))) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("JMAP server returned HTTP %u for session discovery"),
			soup_message_get_status (message));
		g_bytes_unref (body);
		g_object_unref (message);
		return FALSE;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser,
	    g_bytes_get_data (body, NULL),
	    g_bytes_get_size (body),
	    error)) {
		g_object_unref (parser);
		g_bytes_unref (body);
		g_object_unref (message);
		return FALSE;
	}

	g_bytes_unref (body);
	g_object_unref (message);

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

	g_mutex_lock (&transport->session_lock);
	g_free (transport->api_url);
	transport->api_url = g_strdup (api_url);

	accounts_obj = json_object_get_object_member (root_obj, "accounts");
	if (accounts_obj) {
		account_ids = json_object_get_members (accounts_obj);
		if (account_ids) {
			g_free (transport->account_id);
			transport->account_id = g_strdup (account_ids->data);
			g_list_free (account_ids);
		}
	}
	g_mutex_unlock (&transport->session_lock);

	g_object_unref (parser);
	return TRUE;
}

/* Formats a CamelAddress list as a JMAP "address" JSON array. */
static void
jmap_append_address_array (JsonBuilder *builder,
                             CamelAddress *addresses)
{
	CamelInternetAddress *internet_addr;
	gint ii, len;

	json_builder_begin_array (builder);

	if (!CAMEL_IS_INTERNET_ADDRESS (addresses)) {
		json_builder_end_array (builder);
		return;
	}

	internet_addr = CAMEL_INTERNET_ADDRESS (addresses);
	len = camel_address_length (addresses);

	for (ii = 0; ii < len; ii++) {
		const gchar *name = NULL, *addr = NULL;

		if (camel_internet_address_get (internet_addr, ii, &name, &addr)) {
			json_builder_begin_object (builder);
			json_builder_set_member_name (builder, "name");
			json_builder_add_string_value (builder, name ? name : "");
			json_builder_set_member_name (builder, "email");
			json_builder_add_string_value (builder, addr ? addr : "");
			json_builder_end_object (builder);
		}
	}

	json_builder_end_array (builder);
}

static gboolean
jmap_transport_connect_sync (CamelService *service,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelJmapTransport *transport = CAMEL_JMAP_TRANSPORT (service);

	g_mutex_lock (&transport->session_lock);
	if (!transport->soup_session) {
		transport->soup_session = soup_session_new ();
		soup_session_set_user_agent (transport->soup_session,
			"CamelJMAP/" VERSION);
	}
	g_mutex_unlock (&transport->session_lock);

	return jmap_transport_fetch_session (transport, cancellable, error);
}

static gboolean
jmap_transport_disconnect_sync (CamelService *service,
                                gboolean clean,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelJmapTransport *transport = CAMEL_JMAP_TRANSPORT (service);

	g_mutex_lock (&transport->session_lock);
	g_clear_pointer (&transport->api_url, g_free);
	g_clear_pointer (&transport->account_id, g_free);
	g_clear_object (&transport->soup_session);
	g_mutex_unlock (&transport->session_lock);

	return TRUE;
}

static CamelAuthenticationResult
jmap_transport_authenticate_sync (CamelService *service,
                                    const gchar *mechanism,
                                    GCancellable *cancellable,
                                    GError **error)
{
	return CAMEL_AUTHENTICATION_ACCEPTED;
}

static gchar *
jmap_transport_get_name (CamelService *service,
                          gboolean brief)
{
	CamelSettings *settings;
	CamelNetworkSettings *network_settings;
	gchar *host, *name;

	settings = camel_service_ref_settings (service);
	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	g_object_unref (settings);

	if (brief)
		name = g_strdup_printf (_("JMAP server %s"), host);
	else
		name = g_strdup_printf (_("JMAP mail transport on %s"), host);

	g_free (host);
	return name;
}

static gboolean
jmap_transport_send_to_sync (CamelTransport *transport,
                               CamelMimeMessage *message,
                               CamelAddress *from,
                               CamelAddress *recipients,
                               gboolean *out_sent_message_saved,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelJmapTransport *jmap_transport = CAMEL_JMAP_TRANSPORT (transport);
	CamelInternetAddress *from_inet, *recip_inet;
	JsonBuilder *builder;
	JsonNode *request_node;
	JsonGenerator *generator;
	gchar *request_body;
	gsize request_len;
	SoupMessage *soup_message;
	GBytes *body;
	JsonParser *parser;
	JsonNode *response_node;
	JsonObject *root_obj;
	JsonArray *method_responses;
	JsonObject *submission_response, *created;
	const gchar *account_id;
	gint ii, len;
	gboolean success = FALSE;

	g_mutex_lock (&jmap_transport->session_lock);
	account_id = jmap_transport->account_id;
	g_mutex_unlock (&jmap_transport->session_lock);

	if (!account_id) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		return FALSE;
	}

	/* Build Email/set + EmailSubmission/set JMAP request.
	 * First we create a draft Email, then submit it. */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "using");
		json_builder_begin_array (builder);
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:core");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:mail");
			json_builder_add_string_value (builder, "urn:ietf:params:jmap:submission");
		json_builder_end_array (builder);
		json_builder_set_member_name (builder, "methodCalls");
		json_builder_begin_array (builder);
			/* Call 0: Email/set to create the email */
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "Email/set");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "create");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "draft");
						json_builder_begin_object (builder);
							/* From */
							json_builder_set_member_name (builder, "from");
							jmap_append_address_array (builder, from);
							/* To */
							json_builder_set_member_name (builder, "to");
							jmap_append_address_array (builder, recipients);
							/* Subject */
							json_builder_set_member_name (builder, "subject");
							json_builder_add_string_value (builder,
								camel_mime_message_get_subject (message)
								? camel_mime_message_get_subject (message)
								: "");
							/* Mark as draft */
							json_builder_set_member_name (builder, "keywords");
							json_builder_begin_object (builder);
								json_builder_set_member_name (builder, "$draft");
								json_builder_add_boolean_value (builder, TRUE);
							json_builder_end_object (builder);
						json_builder_end_object (builder);
					json_builder_end_object (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c0");
			json_builder_end_array (builder);
			/* Call 1: EmailSubmission/set to submit the draft */
			json_builder_begin_array (builder);
				json_builder_add_string_value (builder, "EmailSubmission/set");
				json_builder_begin_object (builder);
					json_builder_set_member_name (builder, "accountId");
					json_builder_add_string_value (builder, account_id);
					json_builder_set_member_name (builder, "create");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "send");
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "emailId");
							/* Reference the ID created in call 0 */
							json_builder_add_string_value (builder, "#draft");
							json_builder_set_member_name (builder, "envelope");
							json_builder_begin_object (builder);
								json_builder_set_member_name (builder, "mailFrom");
								/* Build the SMTP MAIL FROM address */
								from_inet = CAMEL_INTERNET_ADDRESS (from);
								if (from_inet && camel_address_length (from) > 0) {
									const gchar *name = NULL, *addr = NULL;
									json_builder_begin_object (builder);
									camel_internet_address_get (from_inet, 0, &name, &addr);
									json_builder_set_member_name (builder, "email");
									json_builder_add_string_value (builder, addr ? addr : "");
									json_builder_end_object (builder);
								} else {
									json_builder_begin_object (builder);
									json_builder_set_member_name (builder, "email");
									json_builder_add_string_value (builder, "");
									json_builder_end_object (builder);
								}
								json_builder_set_member_name (builder, "rcptTo");
								/* Build SMTP RCPT TO addresses */
								recip_inet = CAMEL_INTERNET_ADDRESS (recipients);
								json_builder_begin_array (builder);
								len = camel_address_length (recipients);
								for (ii = 0; ii < len; ii++) {
									const gchar *name = NULL, *addr = NULL;
									if (camel_internet_address_get (recip_inet, ii, &name, &addr)) {
										json_builder_begin_object (builder);
										json_builder_set_member_name (builder, "email");
										json_builder_add_string_value (builder, addr ? addr : "");
										json_builder_end_object (builder);
									}
								}
								json_builder_end_array (builder);
							json_builder_end_object (builder);
						json_builder_end_object (builder);
					json_builder_end_object (builder);
					/* After submission, update the draft to remove $draft flag
					 * and mark as $seen */
					json_builder_set_member_name (builder, "onSuccessUpdateEmail");
					json_builder_begin_object (builder);
						json_builder_set_member_name (builder, "#send");
						json_builder_begin_object (builder);
							json_builder_set_member_name (builder, "keywords/$draft");
							json_builder_add_null_value (builder);
							json_builder_set_member_name (builder, "keywords/$seen");
							json_builder_add_boolean_value (builder, TRUE);
						json_builder_end_object (builder);
					json_builder_end_object (builder);
				json_builder_end_object (builder);
				json_builder_add_string_value (builder, "c1");
			json_builder_end_array (builder);
		json_builder_end_array (builder);
	json_builder_end_object (builder);

	request_node = json_builder_get_root (builder);
	g_object_unref (builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, request_node);
	request_body = json_generator_to_data (generator, &request_len);
	g_object_unref (generator);
	json_node_unref (request_node);

	g_mutex_lock (&jmap_transport->session_lock);

	if (!jmap_transport->api_url) {
		g_mutex_unlock (&jmap_transport->session_lock);
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_NOT_CONNECTED,
			_("Not connected to JMAP server"));
		g_free (request_body);
		return FALSE;
	}

	soup_message = soup_message_new (SOUP_METHOD_POST, jmap_transport->api_url);
	g_mutex_unlock (&jmap_transport->session_lock);

	if (!soup_message) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Invalid JMAP API URL"));
		g_free (request_body);
		return FALSE;
	}

	soup_message_set_request_body_from_bytes (
		soup_message,
		"application/json",
		g_bytes_new_take (request_body, request_len));

	soup_message_headers_replace (
		soup_message_get_request_headers (soup_message),
		"Accept", "application/json");

	jmap_transport_set_auth_header (jmap_transport, soup_message);

	body = soup_session_send_and_read (
		jmap_transport->soup_session, soup_message, cancellable, error);

	if (!body) {
		g_object_unref (soup_message);
		return FALSE;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (soup_message))) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("JMAP server returned HTTP %u for EmailSubmission/set"),
			soup_message_get_status (soup_message));
		g_bytes_unref (body);
		g_object_unref (soup_message);
		return FALSE;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser,
	    g_bytes_get_data (body, NULL),
	    g_bytes_get_size (body),
	    error)) {
		g_object_unref (parser);
		g_bytes_unref (body);
		g_object_unref (soup_message);
		return FALSE;
	}

	g_bytes_unref (body);
	g_object_unref (soup_message);

	response_node = json_parser_get_root (parser);
	root_obj = json_node_get_object (response_node);
	method_responses = json_object_get_array_member (root_obj, "methodResponses");

	if (method_responses && json_array_get_length (method_responses) >= 2) {
		JsonArray *response_call;

		response_call = json_array_get_array_element (method_responses, 1);
		submission_response = json_array_get_object_element (response_call, 1);
		created = json_object_get_object_member (submission_response, "created");

		if (created && json_object_has_member (created, "send")) {
			success = TRUE;
			if (out_sent_message_saved)
				*out_sent_message_saved = FALSE;
		} else {
			JsonArray *not_created;
			not_created = json_object_get_array_member (submission_response, "notCreated");
			if (not_created && json_array_get_length (not_created) > 0) {
				g_set_error (error, CAMEL_ERROR,
					CAMEL_ERROR_GENERIC,
					_("JMAP server rejected the email submission"));
			} else {
				success = TRUE;
			}
		}
	} else {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Unexpected JMAP response for EmailSubmission/set"));
	}

	g_object_unref (parser);
	return success;
}

static void
jmap_transport_dispose (GObject *object)
{
	CamelJmapTransport *transport = CAMEL_JMAP_TRANSPORT (object);

	g_clear_object (&transport->soup_session);

	G_OBJECT_CLASS (camel_jmap_transport_parent_class)->dispose (object);
}

static void
jmap_transport_finalize (GObject *object)
{
	CamelJmapTransport *transport = CAMEL_JMAP_TRANSPORT (object);

	g_mutex_clear (&transport->session_lock);
	g_clear_pointer (&transport->api_url, g_free);
	g_clear_pointer (&transport->account_id, g_free);

	G_OBJECT_CLASS (camel_jmap_transport_parent_class)->finalize (object);
}

static void
camel_jmap_transport_class_init (CamelJmapTransportClass *class)
{
	GObjectClass *object_class;
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = jmap_transport_dispose;
	object_class->finalize = jmap_transport_finalize;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->settings_type = CAMEL_TYPE_JMAP_SETTINGS;
	service_class->get_name = jmap_transport_get_name;
	service_class->connect_sync = jmap_transport_connect_sync;
	service_class->disconnect_sync = jmap_transport_disconnect_sync;
	service_class->authenticate_sync = jmap_transport_authenticate_sync;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = jmap_transport_send_to_sync;
}

static void
camel_jmap_transport_init (CamelJmapTransport *transport)
{
	g_mutex_init (&transport->session_lock);
}

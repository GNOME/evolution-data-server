/*
 * uoa-utils.c
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
#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>
#include <libsignon-glib/signon-glib.h>

#include "uoa-utils.h"

#define GOOGLE_USERINFO_URI \
	"https://www.googleapis.com/oauth2/v2/userinfo"

#define WINDOWS_LIVE_ME_URI \
	"https://apis.live.net/v5.0/me"

typedef struct _AsyncContext AsyncContext;

struct _AsyncContext {
	gchar *user_identity;
	gchar *address;
};

static AsyncContext *
async_context_new (const gchar* user_identity,
                   const gchar* address)
{
	AsyncContext *async_context = g_slice_new0 (AsyncContext);
	async_context->user_identity = g_strdup (user_identity);
	async_context->address = g_strdup (address);
	return async_context;
}

static void
async_context_free (AsyncContext *async_context)
{
	g_free (async_context->user_identity);
	g_free (async_context->address);

	g_slice_free (AsyncContext, async_context);
}

/****************************** Google Provider ******************************/

static void
e_ag_account_google_got_userinfo_cb (RestProxyCall *call,
                                     const GError *error,
                                     GObject *weak_object,
                                     gpointer user_data)
{
	GTask *task = NULL;
	JsonParser *json_parser;
	JsonObject *json_object;
	JsonNode *json_node;
	const gchar *email;
	GError *local_error = NULL;

	task = G_TASK (user_data);

	if (error != NULL) {
		g_task_return_error (task, g_error_copy (error));
		g_object_unref (task);
		return;
	}

	/* This is shamelessly stolen from goagoogleprovider.c */

	if (rest_proxy_call_get_status_code (call) != 200) {
		g_task_return_new_error (
			task, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Expected status 200 when requesting your "
			"identity, instead got status %d (%s)"),
			rest_proxy_call_get_status_code (call),
			rest_proxy_call_get_status_message (call));
		g_object_unref (task);
		return;
	}

	json_parser = json_parser_new ();
	json_parser_load_from_data (
		json_parser,
		rest_proxy_call_get_payload (call),
		rest_proxy_call_get_payload_length (call),
		&local_error);

	if (local_error != NULL) {
		g_prefix_error (
			&local_error,
			_("Error parsing response as JSON: "));
		g_task_return_error (task, local_error);
		g_object_unref (json_parser);
		g_object_unref (task);
		return;
	}

	json_node = json_parser_get_root (json_parser);
	json_object = json_node_get_object (json_node);
	email = json_object_get_string_member (json_object, "email");

	if (email != NULL) {
		AsyncContext *async_context = async_context_new (email, email);
		g_task_return_pointer (task, async_context, (GDestroyNotify) async_context_free);
	} else {
		g_task_return_new_error (
			task, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Didn’t find “email” in JSON data"));
	}

	g_object_unref (json_parser);
	g_object_unref (task);
}

static void
e_ag_account_google_session_process_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	GTask *task;
	GVariant *session_data;
	GError *local_error = NULL;

	task = G_TASK (user_data);

	session_data = signon_auth_session_process_finish (
		SIGNON_AUTH_SESSION (source_object), result, &local_error);

	/* Use the access token to obtain the user's email address. */

	if (session_data != NULL) {
		RestProxy *proxy;
		RestProxyCall *call;
		gchar *access_token = NULL;

		g_variant_lookup (
			session_data, "AccessToken", "s", &access_token);

		g_variant_unref (session_data);

		proxy = rest_proxy_new (GOOGLE_USERINFO_URI, FALSE);
		call = rest_proxy_new_call (proxy);
		rest_proxy_call_set_method (call, "GET");

		/* XXX This should never be NULL, but if it is just let
		 *     the call fail and pick up the resulting GError. */
		if (access_token != NULL) {
			rest_proxy_call_add_param (
				call, "access_token", access_token);
			g_free (access_token);
		}

		rest_proxy_call_async (
			call, e_ag_account_google_got_userinfo_cb,
			NULL, task, &local_error);

		if (local_error != NULL) {
			g_object_unref (task);
		}

		g_object_unref (proxy);
		g_object_unref (call);
	} else {
		g_task_return_error (task, local_error);
			g_object_unref (task);
	}
}

static void
e_ag_account_collect_google_userinfo (GTask *task)
{
	AgAccount *ag_account = NULL;
	AgService *ag_service = NULL;
	AgAccountService *ag_account_service = NULL;
	SignonAuthSession *session;
	AgAuthData *ag_auth_data;
	GList *list;
	GError *local_error = NULL;

	g_assert (G_IS_TASK (task));

	ag_account = g_task_get_source_object (task);

	/* First obtain an OAuth 2.0 access token. */

	list = ag_account_list_services_by_type (ag_account, "mail");
	if (list != NULL) {
		ag_service = ag_service_ref ((AgService *) list->data);
		ag_service_list_free (list);
	}

	ag_account_service = ag_account_service_new (ag_account, ag_service);
	ag_service_unref (ag_service);

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);

	session = signon_auth_session_new (
		ag_auth_data_get_credentials_id (ag_auth_data),
		ag_auth_data_get_method (ag_auth_data), &local_error);

	if (session != NULL) {
		signon_auth_session_process_async (
			session,
			ag_auth_data_get_login_parameters (ag_auth_data, NULL),
			ag_auth_data_get_mechanism (ag_auth_data),
			g_task_get_cancellable (task),
			e_ag_account_google_session_process_cb,
			task);
	} else {
		g_task_return_error (task, local_error);
		g_object_unref (task);
	}

	ag_auth_data_unref (ag_auth_data);
	g_object_unref (ag_account_service);
}

/*************************** Windows Live Provider ***************************/

static void
e_ag_account_windows_live_got_me_cb (RestProxyCall *call,
                                     const GError *error,
                                     GObject *weak_object,
                                     gpointer user_data)
{
	GTask *task;
	JsonParser *json_parser;
	JsonObject *json_object;
	JsonNode *json_node;
	const gchar *json_string;
	const gchar *id;
	const gchar *email;
	GError *local_error = NULL;

	task = G_TASK (user_data);

	if (error != NULL) {
		g_task_return_error (task, g_error_copy (error));
		g_object_unref (task);
		return;
	}

	/* This is shamelessly stolen from goawindowsliveprovider.c */

	if (rest_proxy_call_get_status_code (call) != 200) {
		g_task_return_new_error (
			task, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Expected status 200 when requesting your "
			"identity, instead got status %d (%s)"),
			rest_proxy_call_get_status_code (call),
			rest_proxy_call_get_status_message (call));
		g_object_unref (task);
		return;
	}

	json_parser = json_parser_new ();
	json_parser_load_from_data (
		json_parser,
		rest_proxy_call_get_payload (call),
		rest_proxy_call_get_payload_length (call),
		&local_error);

	if (local_error != NULL) {
		g_prefix_error (
			&local_error,
			_("Error parsing response as JSON: "));
		g_task_return_error (task, local_error);
		g_object_unref (json_parser);
		g_object_unref (task);
		return;
	}

	json_node = json_parser_get_root (json_parser);
	json_object = json_node_get_object (json_node);
	id = json_object_get_string_member (json_object, "id");

	json_object = json_object_get_object_member (json_object, "emails");
	email = json_object_get_string_member (json_object, "account");

	if (id == NULL) {
		g_task_return_new_error (
			task, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Didn’t find “id” in JSON data"));
	} else if (email == NULL) {
		g_task_return_new_error (
			task, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Didn’t find “emails.account” in JSON data"));
	} else {
		AsyncContext *async_context = async_context_new (id, email);
		g_task_return_pointer (task, async_context, (GDestroyNotify) async_context_free);
	}

	g_object_unref (json_parser);
	g_object_unref (task);
}

static void
e_ag_account_windows_live_session_process_cb (GObject *source_object,
                                              GAsyncResult *result,
                                              gpointer user_data)
{
	GTask *task;
	GVariant *session_data;
	GError *local_error = NULL;

	task = G_TASK (user_data);

	session_data = signon_auth_session_process_finish (
		SIGNON_AUTH_SESSION (source_object), result, &local_error);

	/* Use the access token to obtain the user's email address. */

	if (session_data != NULL) {
		RestProxy *proxy;
		RestProxyCall *call;
		gchar *access_token = NULL;

		g_variant_lookup (
			session_data, "AccessToken", "s", &access_token);

		g_variant_unref (session_data);

		proxy = rest_proxy_new (WINDOWS_LIVE_ME_URI, FALSE);
		call = rest_proxy_new_call (proxy);
		rest_proxy_call_set_method (call, "GET");

		/* XXX This should never be NULL, but if it is just let
		 *     the call fail and pick up the resulting GError. */
		if (access_token != NULL) {
			rest_proxy_call_add_param (
				call, "access_token", access_token);
			g_free (access_token);
		}

		rest_proxy_call_async (
			call, e_ag_account_windows_live_got_me_cb,
			NULL, task, &local_error);

		if (local_error != NULL) {
			g_object_unref (task);
		}

		g_object_unref (proxy);
		g_object_unref (call);
	} else {
		g_task_return_error (task, local_error);
		g_object_unref (task);
	}
}

static void
e_ag_account_collect_windows_live_userinfo (GTask *task)
{
	AgAccount *ag_account = NULL;
	AgService *ag_service = NULL;
	AgAccountService *ag_account_service = NULL;
	SignonAuthSession *session;
	AgAuthData *ag_auth_data;
	GList *list;
	GError *local_error = NULL;

	g_assert (G_IS_TASK (task));

	ag_account = g_task_get_source_object (task);

	/* First obtain an OAuth 2.0 access token. */

	list = ag_account_list_services_by_type (ag_account, "mail");
	if (list != NULL) {
		ag_service = ag_service_ref ((AgService *) list->data);
		ag_service_list_free (list);
	}

	ag_account_service = ag_account_service_new (ag_account, ag_service);
	ag_service_unref (ag_service);

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);

	session = signon_auth_session_new (
		ag_auth_data_get_credentials_id (ag_auth_data),
		ag_auth_data_get_method (ag_auth_data), &local_error);

	if (session != NULL) {
		signon_auth_session_process_async (
			session,
			ag_auth_data_get_login_parameters (ag_auth_data, NULL),
			ag_auth_data_get_mechanism (ag_auth_data),
			g_task_get_cancellable (task),
			e_ag_account_windows_live_session_process_cb,
			task);
	} else {
		g_task_return_error (task, local_error);
		g_object_unref (task);
	}

	ag_auth_data_unref (ag_auth_data);
	g_object_unref (ag_account_service);
}

/****************************** Yahoo! Provider ******************************/

static void
e_ag_account_collect_yahoo_userinfo (GTask *task)
{
	AgAccount *ag_account = NULL;
	AsyncContext *async_context;
	GString *email_address;
	const gchar *display_name;

	g_assert (G_IS_TASK (task));

	ag_account = g_task_get_source_object (task);

	/* XXX This is a bit of a hack just to get *something* working
	 *     for Yahoo! accounts.  The proper way to obtain userinfo
	 *     for Yahoo! is through OAuth 1.0 and OpenID APIs, which
	 *     does not look trivial.  This will do for now. */

	/* XXX AgAccount's display name also sort of doubles as a user
	 *     name, which may or may not be an email address.  If the
	 *     display name has no domain part, assume "@yahoo.com". */

	display_name = ag_account_get_display_name (ag_account);

	email_address = g_string_new (display_name);
	if (strchr (email_address->str, '@') == NULL)
		g_string_append (email_address, "@yahoo.com");

	async_context = async_context_new (email_address->str,
	                                   email_address->str);
	g_task_return_pointer (task, async_context, (GDestroyNotify) async_context_free);
	g_object_unref (task);
}

/************************ End Provider-Specific Code *************************/

void
e_ag_account_collect_userinfo (AgAccount *ag_account,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GTask *task;
	const gchar *provider_name;

	g_return_if_fail (AG_IS_ACCOUNT (ag_account));

	provider_name = ag_account_get_provider_name (ag_account);
	g_return_if_fail (provider_name != NULL);

	task = g_task_new (ag_account, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_ag_account_collect_userinfo);
	g_task_set_check_cancellable (task, TRUE);

	/* XXX This has to be done differently for each provider. */

	if (g_str_equal (provider_name, "google")) {
		e_ag_account_collect_google_userinfo (task);
	} else if (g_str_equal (provider_name, "windows-live")) {
		e_ag_account_collect_windows_live_userinfo (task);
	} else if (g_str_equal (provider_name, "yahoo")) {
		e_ag_account_collect_yahoo_userinfo (task);
	} else {
		g_warn_if_reached ();
		g_task_return_pointer (task, NULL, NULL);
		g_object_unref (task);
	}
}

gboolean
e_ag_account_collect_userinfo_finish (AgAccount *ag_account,
                                      GAsyncResult *result,
                                      gchar **out_user_identity,
                                      gchar **out_email_address,
                                      gchar **out_imap_user_name,
                                      gchar **out_smtp_user_name,
                                      GError **error)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_val_if_fail (g_task_is_valid (result, ag_account), FALSE);

	task = G_TASK (result);
	async_context = g_task_propagate_pointer (task, error);
	if (async_context == NULL)
		return FALSE;

	/* The result strings may be NULL without an error. */

	if (out_user_identity != NULL) {
		*out_user_identity = g_steal_pointer (&async_context->user_identity);
	}

	if (out_email_address != NULL) {
		*out_email_address = g_strdup (async_context->address);
	}

	if (out_imap_user_name != NULL) {
		*out_imap_user_name = g_strdup (async_context->address);
	}

	if (out_smtp_user_name != NULL) {
		*out_smtp_user_name = g_strdup (async_context->address);
	}

	async_context_free (async_context);
	return TRUE;
}

const gchar *
e_source_get_ag_service_type (ESource *source)
{
	const gchar *extension_name;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	/* Determine an appropriate service type based on
	 * which extensions are present in the ESource. */

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_CONTACTS;

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_CALENDAR;

	extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_CALENDAR;

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_CALENDAR;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_MAIL;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	if (e_source_has_extension (source, extension_name))
		return E_AG_SERVICE_TYPE_MAIL;

	g_return_val_if_reached (NULL);
}


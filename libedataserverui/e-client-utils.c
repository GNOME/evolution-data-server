/*
 * e-client-utils.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include <libedataserver/e-client.h>
#include "libedataserver/e-client-private.h"
#include <libebook/e-book-client.h>
#include <libecal/e-cal-client.h>

#include "e-passwords.h"
#include "e-client-utils.h"

/**
 * e_client_utils_new:
 *
 * Proxy function for e_book_client_utils_new() and e_cal_client_utils_new().
 **/
EClient	*
e_client_utils_new (ESource *source, EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new (source, error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_from_uri:
 *
 * Proxy function for e_book_client_utils_new_from_uri() and e_cal_client_utils_new_from_uri().
 **/
EClient *
e_client_utils_new_from_uri (const gchar *uri, EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	g_return_val_if_fail (uri != NULL, NULL);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_from_uri (uri, error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_from_uri (uri, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_system:
 *
 * Proxy function for e_book_client_utils_new_system() and e_cal_client_utils_new_system().
 **/
EClient *
e_client_utils_new_system (EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_system (error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_system (E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_new_default:
 *
 * Proxy function for e_book_client_utils_new_default() and e_cal_client_utils_new_default().
 **/
EClient *
e_client_utils_new_default (EClientSourceType source_type, GError **error)
{
	EClient *res = NULL;

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = E_CLIENT (e_book_client_new_default (error));
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error));
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = E_CLIENT (e_cal_client_new_default (E_CAL_CLIENT_SOURCE_TYPE_TASKS, error));
		break;
	default:
		g_return_val_if_reached (NULL);
		break;
	}

	return res;
}

/**
 * e_client_utils_set_default:
 *
 * Proxy function for e_book_client_utils_set_default() and e_book_client_utils_set_default().
 **/
gboolean
e_client_utils_set_default (EClient *client, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		g_return_val_if_fail (E_IS_BOOK_CLIENT (client), FALSE);
		res = e_book_client_set_default (E_BOOK_CLIENT (client), error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
	case E_CLIENT_SOURCE_TYPE_MEMOS:
	case E_CLIENT_SOURCE_TYPE_TASKS:
		g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
		res = e_cal_client_set_default (E_CAL_CLIENT (client), error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

/**
 * e_client_utils_set_default_source:
 *
 * Proxy function for e_book_client_utils_set_default_source() and e_cal_client_utils_set_default_source().
 **/
gboolean
e_client_utils_set_default_source (ESource *source, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = e_book_client_set_default_source (source, error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = e_cal_client_set_default_source (source, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

/**
 * e_client_utils_get_sources:
 *
 * Proxy function for e_book_client_utils_get_sources() and e_cal_client_utils_get_sources().
 **/
gboolean
e_client_utils_get_sources (ESourceList **sources, EClientSourceType source_type, GError **error)
{
	gboolean res = FALSE;

	g_return_val_if_fail (sources != NULL, FALSE);

	switch (source_type) {
	case E_CLIENT_SOURCE_TYPE_CONTACTS:
		res = e_book_client_get_sources (sources, error);
		break;
	case E_CLIENT_SOURCE_TYPE_EVENTS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_MEMOS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_MEMOS, error);
		break;
	case E_CLIENT_SOURCE_TYPE_TASKS:
		res = e_cal_client_get_sources (sources, E_CAL_CLIENT_SOURCE_TYPE_TASKS, error);
		break;
	default:
		g_return_val_if_reached (FALSE);
		break;
	}

	return res;
}

typedef struct _EClientUtilsAsyncOpData
{
	EClientUtilsAuthenticateHandler auth_handler;
	gpointer auth_handler_user_data;
	GAsyncReadyCallback async_cb;
	gpointer async_cb_user_data;
	GCancellable *cancellable;
	EClient *client;
	ECredentials *used_credentials;
	gboolean open_finished;
	GError *opened_cb_error;
} EClientUtilsAsyncOpData;

static void
free_client_utils_async_op_data (EClientUtilsAsyncOpData *async_data)
{
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->cancellable != NULL);
	g_return_if_fail (async_data->client != NULL);

	g_signal_handlers_disconnect_matched (async_data->cancellable, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, async_data);
	g_signal_handlers_disconnect_matched (async_data->client, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, async_data);

	if (async_data->used_credentials)
		e_credentials_free (async_data->used_credentials);
	if (async_data->opened_cb_error)
		g_error_free (async_data->opened_cb_error);
	g_object_unref (async_data->cancellable);
	g_object_unref (async_data->client);
	g_free (async_data);
}

static gboolean
complete_async_op_in_idle_cb (gpointer user_data)
{
	GSimpleAsyncResult *simple = user_data;
	gint run_main_depth;

	g_return_val_if_fail (simple != NULL, FALSE);

	run_main_depth = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (simple), "run-main-depth"));
	if (run_main_depth < 1)
		run_main_depth = 1;

	/* do not receive in higher level than was initially run */
	if (g_main_depth () > run_main_depth) {
		return TRUE;
	}

	g_simple_async_result_complete (simple);
	g_object_unref (simple);

	return FALSE;
}

#define return_async_error_if_fail(expr, async_cb, async_cb_user_data, source_tag) G_STMT_START {	\
	if (G_LIKELY ((expr))) { } else {								\
		GError *error;										\
													\
		error = g_error_new (E_CLIENT_ERROR, E_CLIENT_ERROR_INVALID_ARG,			\
				"%s: assertion '%s' failed", G_STRFUNC, #expr);				\
													\
		return_async_error (error, async_cb, async_cb_user_data, source_tag);			\
		g_error_free (error);									\
		return;											\
	}												\
	} G_STMT_END

static void
return_async_error (const GError *error, GAsyncReadyCallback async_cb, gpointer async_cb_user_data, gpointer source_tag)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (error != NULL);
	g_return_if_fail (source_tag != NULL);

	simple = g_simple_async_result_new (NULL, async_cb, async_cb_user_data, source_tag);
	g_simple_async_result_set_from_error (simple, error);

	g_object_set_data (G_OBJECT (simple), "run-main-depth", GINT_TO_POINTER (g_main_depth ()));
	g_idle_add (complete_async_op_in_idle_cb, simple);
}

static void
client_utils_open_new_done (EClientUtilsAsyncOpData *async_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->client != NULL);

	/* keep the initial auth_handler connected directly, thus it will be able
	   to answer any later authentication requests, for reconnection, for example
	*/
	if (async_data->auth_handler)
		g_signal_connect (async_data->client, "authenticate", G_CALLBACK (async_data->auth_handler), async_data->auth_handler_user_data);

	simple = g_simple_async_result_new (NULL, async_data->async_cb, async_data->async_cb_user_data, e_client_utils_open_new);
	g_simple_async_result_set_op_res_gpointer (simple, g_object_ref (async_data->client), g_object_unref);

	g_object_set_data (G_OBJECT (simple), "run-main-depth", GINT_TO_POINTER (g_main_depth ()));
	g_idle_add (complete_async_op_in_idle_cb, simple);

	free_client_utils_async_op_data (async_data);
}

static void
client_utils_open_new_cancelled_cb (GCancellable *cancellable, EClientUtilsAsyncOpData *async_data)
{
	GError *error = NULL;

	g_return_if_fail (cancellable != NULL);
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->cancellable == cancellable);
	g_return_if_fail (g_cancellable_set_error_if_cancelled (cancellable, &error));

	return_async_error (error, async_data->async_cb, async_data->async_cb_user_data, e_client_utils_open_new);
	free_client_utils_async_op_data (async_data);
	g_error_free (error);
}

static void
finish_or_retry_open (EClientUtilsAsyncOpData *async_data, const GError *error)
{
	g_return_if_fail (async_data != NULL);

	if (async_data->auth_handler && error && g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
		if (async_data->used_credentials) {
			const gchar *auth_domain, *prompt_key;

			auth_domain = e_credentials_peek (async_data->used_credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN);
			prompt_key = e_credentials_peek (async_data->used_credentials, E_CREDENTIALS_KEY_PROMPT_KEY);

			/* make sure the old password is forgotten when authentication failed */
			if (auth_domain && prompt_key)
				e_passwords_forget_password (auth_domain, prompt_key);

			e_credentials_set (async_data->used_credentials, E_CREDENTIALS_KEY_PROMPT_REASON, error->message);
		}

		e_client_process_authentication (async_data->client, async_data->used_credentials);
	} else if (error) {
		return_async_error (error, async_data->async_cb, async_data->async_cb_user_data, e_client_utils_open_new);
		free_client_utils_async_op_data (async_data);
	} else {
		client_utils_open_new_done (async_data);
	}
}

static void
client_utils_opened_cb (EClient *client, const GError *error, EClientUtilsAsyncOpData *async_data)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (client == async_data->client);

	if (!async_data->open_finished) {
		/* there can happen that the "opened" signal is received
		   before the e_client_open() is finished, thus keep detailed
		   error for later use, if any */
		if (error)
			async_data->opened_cb_error = g_error_copy (error);
	} else {
		finish_or_retry_open (async_data, error);
	}
}

static void
client_utils_open_new_async_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	EClientUtilsAsyncOpData *async_data = user_data;
	GError *error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (result != NULL);
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->async_cb != NULL);
	g_return_if_fail (async_data->client == E_CLIENT (source_object));

	async_data->open_finished = TRUE;

	if (!e_client_open_finish (E_CLIENT (source_object), result, &error)
	    || g_cancellable_set_error_if_cancelled (async_data->cancellable, &error)) {
		finish_or_retry_open (async_data, error);
		g_error_free (error);
		return;
	}

	if (async_data->opened_cb_error) {
		finish_or_retry_open (async_data, async_data->opened_cb_error);
		return;
	}

	if (e_client_is_opened (async_data->client)) {
		client_utils_open_new_done (async_data);
		return;
	}

	/* wait for 'opened' signal, which is received in client_utils_opened_cb */
	g_signal_connect (async_data->cancellable, "cancelled", G_CALLBACK (client_utils_open_new_cancelled_cb), async_data);
}

static gboolean
client_utils_open_new_auth_cb (EClient *client, ECredentials *credentials, gpointer user_data)
{
	EClientUtilsAsyncOpData *async_data = user_data;
	gboolean handled;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (async_data != NULL, FALSE);
	g_return_val_if_fail (async_data->auth_handler != NULL, FALSE);

	if (async_data->used_credentials) {
		const gchar *reason = e_credentials_peek (async_data->used_credentials, E_CREDENTIALS_KEY_PROMPT_REASON);

		if (reason) {
			e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT, NULL);
			e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_REASON, reason);
		}
	}

	handled = async_data->auth_handler (client, credentials, async_data->auth_handler_user_data);

	if (handled && credentials) {
		if (async_data->used_credentials) {
			gchar *prompt_flags_str;
			guint prompt_flags = 0;

			e_credentials_free (async_data->used_credentials);

			prompt_flags_str = e_credentials_get (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS);
			if (prompt_flags_str) {
				prompt_flags = e_credentials_util_string_to_prompt_flags (prompt_flags_str);
				g_free (prompt_flags_str);
			} else {
				prompt_flags = E_CREDENTIALS_PROMPT_FLAG_REMEMBER_FOREVER
					     | E_CREDENTIALS_PROMPT_FLAG_SECRET
					     | E_CREDENTIALS_PROMPT_FLAG_ONLINE;
			}

			prompt_flags |= E_CREDENTIALS_PROMPT_FLAG_REPROMPT;

			prompt_flags_str = e_credentials_util_prompt_flags_to_string (prompt_flags);
			e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS, prompt_flags_str);
			g_free (prompt_flags_str);
		}

		async_data->used_credentials = e_credentials_new_clone (credentials);
	}

	return handled;
}

/**
 * e_client_utils_open_new:
 * @source: an #ESource to be opened
 * @source_type: an #EClientSourceType of the @source
 * @only_if_exists: if %TRUE, fail if this client doesn't already exist, otherwise create it first
 * @auth_handler: authentication handler, to be used; the e_client_utils_authenticate_handler() is usually sufficient
 * @auth_handler_user_data: user data for @auth_handler function
 * @cancellable: a #GCancellable; can be %NULL
 * @async_cb: callback to call when a result is ready
 * @async_cb_user_data: user data for the @async_cb
 *
 * Begins asynchronous opening of a new #EClient corresponding
 * to the @source of type @source_type. The resulting #EClient
 * is fully opened and authenticated client, ready to be used.
 * This call is finished by e_client_utils_open_new_finish()
 * from the @async_cb.
 *
 * Note: the @auth_handler, and its @auth_handler_user_data,
 * should be valid through whole live of returned #EClient.
 *
 * Since: 3.2
 **/
void
e_client_utils_open_new (ESource *source, EClientSourceType source_type, gboolean only_if_exists,
			 EClientUtilsAuthenticateHandler auth_handler, gpointer auth_handler_user_data,
			 GCancellable *cancellable, GAsyncReadyCallback async_cb, gpointer async_cb_user_data)
{
	EClient *client;
	GError *error = NULL;
	EClientUtilsAsyncOpData *async_data;

	g_return_if_fail (async_cb != NULL);
	return_async_error_if_fail (source != NULL, async_cb, async_cb_user_data, e_client_utils_open_new);
	return_async_error_if_fail (E_IS_SOURCE (source), async_cb, async_cb_user_data, e_client_utils_open_new);

	client = e_client_utils_new (source, source_type, &error);
	if (!client) {
		return_async_error (error, async_cb, async_cb_user_data, e_client_utils_open_new);
		g_error_free (error);
		return;
	}

	async_data = g_new0 (EClientUtilsAsyncOpData, 1);
	async_data->auth_handler = auth_handler;
	async_data->auth_handler_user_data = auth_handler_user_data;
	async_data->async_cb = async_cb;
	async_data->async_cb_user_data = async_cb_user_data;
	async_data->client = client;
	async_data->open_finished = FALSE;
	if (cancellable)
		async_data->cancellable = g_object_ref (cancellable);
	else
		async_data->cancellable = g_cancellable_new ();

	if (auth_handler)
		g_signal_connect (client, "authenticate", G_CALLBACK (client_utils_open_new_auth_cb), async_data);


	/* wait till backend notifies about its opened state */
	g_signal_connect (client, "opened", G_CALLBACK (client_utils_opened_cb), async_data);

	e_client_open (client, only_if_exists, async_data->cancellable, client_utils_open_new_async_cb, async_data);
}

/**
 * e_client_utils_open_new_finish:
 * @result: a #GAsyncResult
 * @client: (out): Return value for an #EClient.
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_utils_open_new() and
 * sets @client to a fully opened and authenticated #EClient.
 * This @client, if not NULL, should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_utils_open_new_finish (GAsyncResult *result, EClient **client, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL, e_client_utils_open_new), FALSE);

	*client = NULL;
	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	*client = g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));

	return *client != NULL;
}

/* This function is suitable as a handler for EClient::authenticate signal.
   It takes care of all the password prompt and such and returns TRUE if
   credentials (password) were provided. Thus just connect it to that signal
   and it'll take care of everything else.
*/
gboolean
e_client_utils_authenticate_handler (EClient *client, ECredentials *credentials, gpointer unused_user_data)
{
	ESource *source;
	gboolean is_book, is_cal, res, remember_password = FALSE;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (credentials != NULL, FALSE);

	is_book = E_IS_BOOK_CLIENT (client);
	is_cal = !is_book && E_IS_CAL_CLIENT (client);
	g_return_val_if_fail (is_book || is_cal, FALSE);

	source = e_client_get_source (client);
	g_return_val_if_fail (source != NULL, FALSE);

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME)) {
		e_credentials_set (credentials, E_CREDENTIALS_KEY_USERNAME, e_source_get_property (source, "username"));

		/* no username set on the source - deny authentication request until
		   username will be also enterable with e-passwords */
		if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME))
			return FALSE;
	}

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN))
		e_credentials_set (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN, is_book ? E_CREDENTIALS_AUTH_DOMAIN_ADDRESSBOOK : E_CREDENTIALS_AUTH_DOMAIN_CALENDAR);

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT)) {
		gchar *prompt, *reason;
		gchar *username_markup, *source_name_markup;

		reason = e_credentials_get (credentials, E_CREDENTIALS_KEY_PROMPT_REASON);
		username_markup = g_markup_printf_escaped ("<b>%s</b>", e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME));
		source_name_markup = g_markup_printf_escaped ("<b>%s</b>", e_source_peek_name (source));


		if (reason && *reason)
			prompt = g_strdup_printf (_("Enter password for %s (user %s)\nReason: %s"), source_name_markup, username_markup, reason);
		else
			prompt = g_strdup_printf (_("Enter password for %s (user %s)"), source_name_markup, username_markup);

		e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT, prompt);

		g_free (username_markup);
		g_free (source_name_markup);
		g_free (reason);
		g_free (prompt);
	}

	if (!e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_KEY)) {
		SoupURI *suri;
		gchar *uri_str;

		suri = soup_uri_new (e_client_get_uri (client));
		g_return_val_if_fail (suri != NULL, FALSE);

		soup_uri_set_user (suri, e_credentials_peek (credentials, E_CREDENTIALS_KEY_USERNAME));
		soup_uri_set_password (suri, NULL);
		soup_uri_set_fragment (suri, NULL);

		uri_str = soup_uri_to_string (suri, FALSE);

		e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_KEY, uri_str);

		g_free (uri_str);
		soup_uri_free (suri);
	}

	remember_password = g_strcmp0 (e_source_get_property (source, "remember_password"), "true") == 0;

	res = e_credentials_authenticate_helper (credentials, NULL, &remember_password);

	if (res)
		e_source_set_property (source, "remember_password", remember_password ? "true" : NULL);

	e_credentials_clear_peek (credentials);

	return res;
}

/* Asks for a password based on the provided credentials information.
   Credentials should have set following keys:
      E_CREDENTIALS_KEY_USERNAME
      E_CREDENTIALS_KEY_AUTH_DOMAIN
      E_CREDENTIALS_KEY_PROMPT_TEXT
      E_CREDENTIALS_KEY_PROMPT_KEY
   all other keys are optional. If also E_CREDENTIALS_KEY_PASSWORD key is provided,
   then it implies a reprompt.

   When this returns TRUE, then the structure contains E_CREDENTIALS_KEY_PASSWORD set
   as entered by a user.
*/
gboolean
e_credentials_authenticate_helper (ECredentials *credentials, GtkWindow *parent, gboolean *remember_password)
{
	gboolean res, fake_remember_password = FALSE;
	guint prompt_flags;
	gchar *password = NULL;
	const gchar *title, *auth_domain, *prompt_key;

	g_return_val_if_fail (credentials != NULL, FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT), FALSE);
	g_return_val_if_fail (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_KEY), FALSE);

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS)) {
		prompt_flags = e_credentials_util_string_to_prompt_flags (e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS));
	} else {
		prompt_flags = E_CREDENTIALS_PROMPT_FLAG_REMEMBER_FOREVER
			     | E_CREDENTIALS_PROMPT_FLAG_SECRET
			     | E_CREDENTIALS_PROMPT_FLAG_ONLINE;
	}

	if (!remember_password) {
		prompt_flags |= E_CREDENTIALS_PROMPT_FLAG_DISABLE_REMEMBER;
		remember_password = &fake_remember_password;
	}

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PASSWORD))
		prompt_flags |= E_CREDENTIALS_PROMPT_FLAG_REPROMPT;

	if (e_credentials_has_key (credentials, E_CREDENTIALS_KEY_PROMPT_TITLE))
		title = e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_TITLE);
	else if (prompt_flags & E_CREDENTIALS_PROMPT_FLAG_PASSPHRASE)
		title = _("Enter Passphrase");
	else
		title = _("Enter Password");

	auth_domain = e_credentials_peek (credentials, E_CREDENTIALS_KEY_AUTH_DOMAIN);
	prompt_key = e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_KEY);

	if (!(prompt_flags & E_CREDENTIALS_PROMPT_FLAG_REPROMPT))
		password = e_passwords_get_password (auth_domain, prompt_key);

	if (!password)
		password = e_passwords_ask_password (title, auth_domain, prompt_key,
				e_credentials_peek (credentials, E_CREDENTIALS_KEY_PROMPT_TEXT),
				prompt_flags, remember_password, parent);

	res = password != NULL;

	if (res)
		e_credentials_set (credentials, E_CREDENTIALS_KEY_PASSWORD, password);

	e_credentials_util_safe_free_string (password);
	e_credentials_clear_peek (credentials);

	return res;
}

/*
 * e-source-authenticator.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * SECTION: e-source-authenticator
 * @include: libedataserver/libedataserver.h
 * @short_description: Interface for authentication attempts
 *
 * An object implementing the #ESourceAuthenticator interface gets passed
 * to e_source_registry_authenticate().  The job of an #ESourceAuthenticator
 * is to test whether a remote server will accept a given password, and then
 * indicate the result by returning an #ESourceAuthenticationResult value.
 *
 * Typically only #EBackend subclasses need to implement this interface,
 * as client applications are not involved in authentication.
 *
 * Note this interface is designed around "stateful authentication", where
 * one connects to a server, provides credentials for authentication once,
 * and then issues commands in an authenticated state for the remainder of
 * the session.
 *
 * Backends requiring "stateless authentication" -- where credentials are
 * included with each command -- will typically want to cache the password
 * internally once it's verified as part of implementing this interface.
 **/

#include "e-source-authenticator.h"

#include <config.h>
#include <glib/gi18n-lib.h>

/* These are for building an authentication prompt. */
#include <libedataserver/e-source-address-book.h>
#include <libedataserver/e-source-authentication.h>
#include <libedataserver/e-source-calendar.h>
#include <libedataserver/e-source-collection.h>
#include <libedataserver/e-source-mail-account.h>
#include <libedataserver/e-source-mail-identity.h>
#include <libedataserver/e-source-mail-transport.h>

typedef struct _AsyncContext AsyncContext;

struct _AsyncContext {
	GString *password;
	ESourceAuthenticationResult result;
};

G_DEFINE_INTERFACE (
	ESourceAuthenticator,
	e_source_authenticator,
	G_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	g_string_free (async_context->password, TRUE);

	g_slice_free (AsyncContext, async_context);
}

static void
source_authenticator_get_prompt_strings (ESourceAuthenticator *auth,
                                         ESource *source,
                                         gchar **prompt_title,
                                         gchar **prompt_message,
                                         gchar **prompt_description)
{
	ESourceAuthentication *extension;
	GString *description;
	const gchar *message;
	const gchar *extension_name;
	gchar *display_name;
	gchar *host_name;
	gchar *user_name;

	/* Known types */
	enum {
		TYPE_UNKNOWN,
		TYPE_AMBIGUOUS,
		TYPE_ADDRESS_BOOK,
		TYPE_CALENDAR,
		TYPE_MAIL_ACCOUNT,
		TYPE_MAIL_TRANSPORT,
		TYPE_MEMO_LIST,
		TYPE_TASK_LIST
	} type = TYPE_UNKNOWN;

	/* XXX This is kind of a hack but it should work for now.  Build a
	 *     suitable password prompt by checking for various extensions
	 *     in the ESource.  If no recognizable extensions are found, or
	 *     if the result is ambiguous, just refer to the data source as
	 *     an "account". */

	display_name = e_source_dup_display_name (source);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);
	host_name = e_source_authentication_dup_host (extension);
	user_name = e_source_authentication_dup_user (extension);

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (source, extension_name)) {
		type = TYPE_ADDRESS_BOOK;
	}

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (source, extension_name)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_CALENDAR;
		else
			type = TYPE_AMBIGUOUS;
	}

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	if (e_source_has_extension (source, extension_name)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MAIL_ACCOUNT;
		else
			type = TYPE_AMBIGUOUS;
	}

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	if (e_source_has_extension (source, extension_name)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MAIL_TRANSPORT;
		else
			type = TYPE_AMBIGUOUS;
	}

	extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	if (e_source_has_extension (source, extension_name)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_MEMO_LIST;
		else
			type = TYPE_AMBIGUOUS;
	}

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (source, extension_name)) {
		if (type == TYPE_UNKNOWN)
			type = TYPE_TASK_LIST;
		else
			type = TYPE_AMBIGUOUS;
	}

	switch (type) {
		case TYPE_ADDRESS_BOOK:
			message = _("Address book authentication request");
			break;
		case TYPE_CALENDAR:
		case TYPE_MEMO_LIST:
		case TYPE_TASK_LIST:
			message = _("Calendar authentication request");
			break;
		case TYPE_MAIL_ACCOUNT:
		case TYPE_MAIL_TRANSPORT:
			message = _("Mail authentication request");
			break;
		default:  /* generic account prompt */
			message = _("Authentication request");
			break;
	}

	description = g_string_sized_new (256);

	switch (type) {
		case TYPE_ADDRESS_BOOK:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"address book \"%s\"."), display_name);
			break;
		case TYPE_CALENDAR:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"calendar \"%s\"."), display_name);
			break;
		case TYPE_MAIL_ACCOUNT:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"mail account \"%s\"."), display_name);
			break;
		case TYPE_MAIL_TRANSPORT:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"mail transport \"%s\"."), display_name);
			break;
		case TYPE_MEMO_LIST:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"memo list \"%s\"."), display_name);
			break;
		case TYPE_TASK_LIST:
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"task list \"%s\"."), display_name);
			break;
		default:  /* generic account prompt */
			g_string_append_printf (
				description,
				_("Please enter the password for "
				"account \"%s\"."), display_name);
			break;
	}

	if (host_name != NULL && user_name != NULL)
		g_string_append_printf (
			description, "\n(user: %s, host: %s)",
			user_name, host_name);
	else if (host_name != NULL)
		g_string_append_printf (
			description, "\n(host: %s)", host_name);
	else if (user_name != NULL)
		g_string_append_printf (
			description, "\n(user: %s)", user_name);

	*prompt_title = g_strdup ("");
	*prompt_message = g_strdup (message);
	*prompt_description = g_string_free (description, FALSE);

	g_free (display_name);
	g_free (host_name);
	g_free (user_name);
}

static gboolean
source_authenticator_get_without_password (ESourceAuthenticator *auth)
{
	/* require password by default */
	return FALSE;
}

/* Helper for source_authenticator_try_password() */
static void
source_authenticator_try_password_thread (GSimpleAsyncResult *simple,
                                          GObject *object,
                                          GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->result =
		e_source_authenticator_try_password_sync (
			E_SOURCE_AUTHENTICATOR (object),
			async_context->password,
			cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
source_authenticator_try_password (ESourceAuthenticator *auth,
                                   const GString *password,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->password = g_string_new (password->str);

	simple = g_simple_async_result_new (
		G_OBJECT (auth), callback, user_data,
		source_authenticator_try_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_authenticator_try_password_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static ESourceAuthenticationResult
source_authenticator_try_password_finish (ESourceAuthenticator *auth,
                                          GAsyncResult *result,
                                          GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (auth),
		source_authenticator_try_password),
		E_SOURCE_AUTHENTICATION_REJECTED);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return E_SOURCE_AUTHENTICATION_ERROR;

	return async_context->result;
}

static void
e_source_authenticator_default_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_prompt_strings = source_authenticator_get_prompt_strings;
	iface->get_without_password = source_authenticator_get_without_password;
	iface->try_password = source_authenticator_try_password;
	iface->try_password_finish = source_authenticator_try_password_finish;
}

/**
 * e_source_authenticator_get_prompt_strings:
 * @auth: an #ESourceAuthenticator
 * @source: an #ESource
 * @prompt_title: (out): the title of the prompt
 * @prompt_message: (out): the prompt message for the user
 * @prompt_description: (out): the detailed description of the prompt
 *
 * Generates authentication prompt strings for @source.
 *
 * For registry service clients, #ESourceRegistry calls this function as
 * part of e_source_registry_authenticate_sync().  In the registry service
 * itself, #EAuthenticationSession calls this function during initialization.
 * This function should rarely need to be called explicitly outside of those
 * two cases.
 *
 * The #ESourceAuthenticatorInterface defines a default behavior for this
 * method which should suffice in most cases.  But implementors can still
 * override the method if needed for special circumstances.
 *
 * Free each of the returned prompt strings with g_free().
 *
 * Since: 3.6
 **/
void
e_source_authenticator_get_prompt_strings (ESourceAuthenticator *auth,
                                           ESource *source,
                                           gchar **prompt_title,
                                           gchar **prompt_message,
                                           gchar **prompt_description)
{
	ESourceAuthenticatorInterface *iface;

	g_return_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (prompt_title != NULL);
	g_return_if_fail (prompt_message != NULL);
	g_return_if_fail (prompt_description != NULL);

	iface = E_SOURCE_AUTHENTICATOR_GET_INTERFACE (auth);
	g_return_if_fail (iface->get_prompt_strings);

	iface->get_prompt_strings (
		auth, source,
		prompt_title,
		prompt_message,
		prompt_description);
}

/**
 * e_source_authenticator_get_without_password:
 * @auth: an #ESourceAuthenticator
 *
 * Returns whether the used authentication method can be used without
 * a password prompt. If so, then user is not asked for the password,
 * only if the authentication fails. The default implementation returns
 * %FALSE, which means always asks for the password (or read it from
 * a keyring).
 *
 * Returns: whether to try to authenticate without asking for the password
 *
 * Since: 3.10
 **/
gboolean
e_source_authenticator_get_without_password (ESourceAuthenticator *auth)
{
	ESourceAuthenticatorInterface *iface;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth), FALSE);

	iface = E_SOURCE_AUTHENTICATOR_GET_INTERFACE (auth);
	g_return_val_if_fail (iface->get_without_password, FALSE);

	return iface->get_without_password (auth);
}

/**
 * e_source_authenticator_try_password_sync:
 * @auth: an #ESourceAuthenticator
 * @password: a user-provided password
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Attempts to authenticate using @password.
 *
 * The password is passed in a #GString container so its content is not
 * accidentally revealed in a stack trace.
 *
 * If an error occurs, the function sets @error and returns
 * #E_SOURCE_AUTHENTICATION_ERROR.
 *
 * Returns: the authentication result
 *
 * Since: 3.6
 **/
ESourceAuthenticationResult
e_source_authenticator_try_password_sync (ESourceAuthenticator *auth,
                                          const GString *password,
                                          GCancellable *cancellable,
                                          GError **error)
{
	ESourceAuthenticatorInterface *iface;

	g_return_val_if_fail (
		E_IS_SOURCE_AUTHENTICATOR (auth),
		E_SOURCE_AUTHENTICATION_REJECTED);
	g_return_val_if_fail (
		password != NULL,
		E_SOURCE_AUTHENTICATION_REJECTED);

	iface = E_SOURCE_AUTHENTICATOR_GET_INTERFACE (auth);
	g_return_val_if_fail (
		iface->try_password_sync != NULL,
		E_SOURCE_AUTHENTICATION_REJECTED);

	return iface->try_password_sync (
		auth, password, cancellable, error);
}

/**
 * e_source_authenticator_try_password:
 * @auth: an #ESourceAuthenticator
 * @password: a user-provided password
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asyncrhonously attempts to authenticate using @password.
 *
 * The password is passed in a #GString container so its content is not
 * accidentally revealed in a stack trace.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_authenticator_try_password_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
e_source_authenticator_try_password (ESourceAuthenticator *auth,
                                     const GString *password,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	ESourceAuthenticatorInterface *iface;

	g_return_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth));
	g_return_if_fail (password != NULL);

	iface = E_SOURCE_AUTHENTICATOR_GET_INTERFACE (auth);
	g_return_if_fail (iface->try_password != NULL);

	iface->try_password (
		auth, password, cancellable, callback, user_data);
}

/**
 * e_source_authenticator_try_password_finish:
 * @auth: an #ESourceAuthenticator
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_authenticator_try_password().
 *
 * If an error occurred, the function sets @error and returns
 * #E_SOURCE_AUTHENTICATION_ERROR.
 *
 * Returns: the authentication result
 *
 * Since: 3.6
 **/
ESourceAuthenticationResult
e_source_authenticator_try_password_finish (ESourceAuthenticator *auth,
                                            GAsyncResult *result,
                                            GError **error)
{
	ESourceAuthenticatorInterface *iface;

	g_return_val_if_fail (
		E_IS_SOURCE_AUTHENTICATOR (auth),
		E_SOURCE_AUTHENTICATION_REJECTED);
	g_return_val_if_fail (
		G_IS_ASYNC_RESULT (result),
		E_SOURCE_AUTHENTICATION_REJECTED);

	iface = E_SOURCE_AUTHENTICATOR_GET_INTERFACE (auth);
	g_return_val_if_fail (
		iface->try_password_finish != NULL,
		E_SOURCE_AUTHENTICATION_REJECTED);

	return iface->try_password_finish (auth, result, error);
}


/*
 * e-oauth2-support.c
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

/**
 * SECTION: e-oauth2-support
 * @include: libebackend/libebackend.h
 * @short_description: An interface for OAuth 2.0 support
 *
 * Support for OAuth 2.0 access tokens is typically provided through
 * dynamically loaded modules.  The module will provide an extension
 * class which implements the #EOAuth2SupportInterface, which can be
 * plugged into all appropriate #EServerSideSource instances through
 * e_server_side_source_set_oauth2_support().  Incoming requests for
 * access tokens are then forwarded to the extension providing OAuth
 * 2.0 support through e_oauth2_support_get_access_token().
 **/

#include "e-oauth2-support.h"

typedef struct _AsyncContext AsyncContext;

struct _AsyncContext {
	gchar *access_token;
	gint expires_in;
};

G_DEFINE_INTERFACE (
	EOAuth2Support,
	e_oauth2_support,
	G_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	g_clear_pointer (&async_context->access_token, g_free);

	g_slice_free (AsyncContext, async_context);
}

/* Helper for oauth2_support_get_access_token() */
static void
oauth2_support_get_access_token_thread (GTask *task,
                                        gpointer source_object,
                                        gpointer task_data,
                                        GCancellable *cancellable)
{
	ESource *source = task_data;
	GError *error = NULL;
	AsyncContext *async_context = g_slice_new0 (AsyncContext);

	if (e_oauth2_support_get_access_token_sync (
		E_OAUTH2_SUPPORT (source_object),
		source,
		cancellable,
		&async_context->access_token,
		&async_context->expires_in,
		&error)) {
		g_task_return_pointer (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);
	} else {
		g_task_return_error (task, g_steal_pointer (&error));
		g_clear_pointer (&async_context, async_context_free);
	}
}

static void
oauth2_support_get_access_token (EOAuth2Support *support,
                                 ESource *source,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GTask *task;

	task = g_task_new (support, cancellable, callback, user_data);
	g_task_set_source_tag (task, oauth2_support_get_access_token);
	g_task_set_check_cancellable (task, TRUE);
	g_task_set_task_data (task, g_object_ref (source), g_object_unref);

	g_task_run_in_thread (task, oauth2_support_get_access_token_thread);

	g_object_unref (task);
}

static gboolean
oauth2_support_get_access_token_finish (EOAuth2Support *support,
                                        GAsyncResult *result,
                                        gchar **out_access_token,
                                        gint *out_expires_in,
                                        GError **error)
{
	AsyncContext *async_context;

	g_return_val_if_fail (g_task_is_valid (result, support), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, oauth2_support_get_access_token), FALSE);

	async_context = g_task_propagate_pointer (G_TASK (result), error);
	if (!async_context)
		return FALSE;

	g_return_val_if_fail (async_context->access_token != NULL, FALSE);

	if (out_access_token != NULL)
		*out_access_token = g_steal_pointer (&async_context->access_token);

	if (out_expires_in != NULL)
		*out_expires_in = async_context->expires_in;

	g_clear_pointer (&async_context, async_context_free);
	return TRUE;
}

static void
e_oauth2_support_default_init (EOAuth2SupportInterface *iface)
{
	iface->get_access_token =
		oauth2_support_get_access_token;
	iface->get_access_token_finish =
		oauth2_support_get_access_token_finish;
}

/**
 * e_oauth2_support_get_access_token_sync:
 * @support: an #EOAuth2Support
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @out_access_token: (out) (optional): return location for the access token, or %NULL
 * @out_expires_in: (out) (optional): return location for the token expiry, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Obtains the OAuth 2.0 access token for @source along with its expiry
 * in seconds from the current time (or 0 if unknown).
 *
 * Free the returned access token with g_free() when finished with it.
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.8
 **/
gboolean
e_oauth2_support_get_access_token_sync (EOAuth2Support *support,
                                        ESource *source,
                                        GCancellable *cancellable,
                                        gchar **out_access_token,
                                        gint *out_expires_in,
                                        GError **error)
{
	EOAuth2SupportInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SUPPORT (support), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	iface = E_OAUTH2_SUPPORT_GET_INTERFACE (support);
	g_return_val_if_fail (iface->get_access_token_sync != NULL, FALSE);

	return iface->get_access_token_sync (
		support, source, cancellable,
		out_access_token, out_expires_in, error);
}

/**
 * e_oauth2_support_get_access_token:
 * @support: an #EOAuth2Support
 * @source: an #ESource
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously obtains the OAuth 2.0 access token for @source along
 * with its expiry in seconds from the current time (or 0 if unknown).
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_oauth2_support_get_access_token_finish() to get the result of the
 * operation.
 *
 * Since: 3.8
 **/
void
e_oauth2_support_get_access_token (EOAuth2Support *support,
                                   ESource *source,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	EOAuth2SupportInterface *iface;

	g_return_if_fail (E_IS_OAUTH2_SUPPORT (support));
	g_return_if_fail (E_IS_SOURCE (source));

	iface = E_OAUTH2_SUPPORT_GET_INTERFACE (support);
	g_return_if_fail (iface->get_access_token != NULL);

	return iface->get_access_token (
		support, source, cancellable, callback, user_data);
}

/**
 * e_oauth2_support_get_access_token_finish:
 * @support: an #EOAuth2Support
 * @result: a #GAsyncResult
 * @out_access_token: (out) (optional): return location for the access token, or %NULL
 * @out_expires_in: (out) (optional): return location for the token expiry, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_oauth2_support_get_access_token().
 *
 * Free the returned access token with g_free() when finished with it.
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.8
 **/
gboolean
e_oauth2_support_get_access_token_finish (EOAuth2Support *support,
                                          GAsyncResult *result,
                                          gchar **out_access_token,
                                          gint *out_expires_in,
                                          GError **error)
{
	EOAuth2SupportInterface *iface;

	g_return_val_if_fail (E_IS_OAUTH2_SUPPORT (support), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	iface = E_OAUTH2_SUPPORT_GET_INTERFACE (support);
	g_return_val_if_fail (iface->get_access_token_finish != NULL, FALSE);

	return iface->get_access_token_finish (
		support, result, out_access_token, out_expires_in, error);
}


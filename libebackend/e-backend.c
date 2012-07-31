/*
 * e-backend.c
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
 */

/**
 * SECTION: e-backend
 * @include: libebackend/libebackend.h
 * @short_description: An abstract base class for backends
 *
 * An #EBackend is paired with an #ESource to facilitate performing
 * actions on the local or remote resource described by the #ESource.
 *
 * In other words, whereas a certain backend type knows how to talk to a
 * certain type of server or data store, the #ESource fills in configuration
 * details such as host name, user name, resource path, etc.
 *
 * All #EBackend instances are created by an #EBackendFactory.
 **/

#include "e-backend.h"

#include <config.h>
#include <glib/gi18n-lib.h>

#include <gio/gio.h>

#define E_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BACKEND, EBackendPrivate))

typedef struct _AsyncContext AsyncContext;

struct _EBackendPrivate {
	ESource *source;
	gboolean online;
};

struct _AsyncContext {
	ESourceAuthenticator *auth;
};

enum {
	PROP_0,
	PROP_ONLINE,
	PROP_SOURCE
};

G_DEFINE_ABSTRACT_TYPE (EBackend, e_backend, G_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->auth != NULL)
		g_object_unref (async_context->auth);

	g_slice_free (AsyncContext, async_context);
}

static void
backend_set_source (EBackend *backend,
                    ESource *source)
{
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (backend->priv->source == NULL);

	backend->priv->source = g_object_ref (source);
}

static void
backend_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ONLINE:
			e_backend_set_online (
				E_BACKEND (object),
				g_value_get_boolean (value));
			return;

		case PROP_SOURCE:
			backend_set_source (
				E_BACKEND (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
backend_get_property (GObject *object,
                      guint property_id,
                      GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ONLINE:
			g_value_set_boolean (
				value, e_backend_get_online (
				E_BACKEND (object)));
			return;

		case PROP_SOURCE:
			g_value_set_object (
				value, e_backend_get_source (
				E_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
backend_dispose (GObject *object)
{
	EBackendPrivate *priv;

	priv = E_BACKEND_GET_PRIVATE (object);

	if (priv->source != NULL) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_backend_parent_class)->dispose (object);
}

static void
backend_constructed (GObject *object)
{
	GNetworkMonitor *monitor;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_backend_parent_class)->constructed (object);

	/* Synchronize network monitoring. */

	monitor = g_network_monitor_get_default ();

	g_object_bind_property (
		monitor, "network-available",
		object, "online",
		G_BINDING_SYNC_CREATE);
}

static void
backend_authenticate_thread (GSimpleAsyncResult *simple,
                             GObject *object,
                             GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_backend_authenticate_sync (
		E_BACKEND (object),
		async_context->auth,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static gboolean
backend_authenticate_sync (EBackend *backend,
                           ESourceAuthenticator *auth,
                           GCancellable *cancellable,
                           GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("%s does not support authentication"),
		G_OBJECT_TYPE_NAME (backend));

	return FALSE;
}

static void
backend_authenticate (EBackend *backend,
                      ESourceAuthenticator *auth,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->auth = g_object_ref (auth);

	simple = g_simple_async_result_new (
		G_OBJECT (backend), callback,
		user_data, backend_authenticate);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, backend_authenticate_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
backend_authenticate_finish (EBackend *backend,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (backend),
		backend_authenticate), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
e_backend_class_init (EBackendClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = backend_set_property;
	object_class->get_property = backend_get_property;
	object_class->dispose = backend_dispose;
	object_class->constructed = backend_constructed;

	class->authenticate_sync = backend_authenticate_sync;
	class->authenticate = backend_authenticate;
	class->authenticate_finish = backend_authenticate_finish;

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			"Online",
			"Whether the backend is online",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"The data source being acted upon",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_backend_init (EBackend *backend)
{
	backend->priv = E_BACKEND_GET_PRIVATE (backend);
}

/**
 * e_backend_get_online:
 * @backend: an #EBackend
 *
 * Returns the online state of @backend: %TRUE if @backend is online,
 * %FALSE if offline.  The online state of each backend is bound to the
 * online state of the #EDataFactory that created it.
 *
 * Returns: the online state
 *
 * Since: 3.4
 **/
gboolean
e_backend_get_online (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);

	return backend->priv->online;
}

/**
 * e_backend_set_online:
 * @backend: an #EBackend
 * @online: the online state
 *
 * Sets the online state of @backend: %TRUE if @backend is online,
 * @FALSE if offline.  The online state of each backend is bound to
 * the online state of the #EDataFactory that created it.
 *
 * Since: 3.4
 **/
void
e_backend_set_online (EBackend *backend,
                      gboolean online)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	/* Avoid unnecessary "notify" signals. */
	if ((online ? 1 : 0) == (backend->priv->online ? 1 : 0))
		return;

	backend->priv->online = online;

	g_object_notify (G_OBJECT (backend), "online");
}

/**
 * e_backend_get_source:
 * @backend: an #EBackend
 *
 * Returns the #ESource to which @backend is paired.
 *
 * Returns: the #ESource to which @backend is paired
 *
 * Since: 3.4
 **/
ESource *
e_backend_get_source (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->source;
}

/**
 * e_backend_authenticate_sync:
 * @backend: an #EBackend
 * @auth: an #ESourceAuthenticator
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Convenience function providing a consistent authentication interface
 * for backends running in either the registry service itself or a client
 * process communicating with the registry service over D-Bus.
 *
 * Authenticates @backend's #EBackend:source, using @auth to handle
 * authentication attempts.  The @backend and @auth arguments may be one
 * and the same if @backend implements the #ESourceAuthenticator interface.
 * The operation loops until authentication is successful or the user aborts
 * further authentication attempts.  If an error occurs, the function will
 * set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_backend_authenticate_sync (EBackend *backend,
                             ESourceAuthenticator *auth,
                             GCancellable *cancellable,
                             GError **error)
{
	EBackendClass *class;

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth), FALSE);

	class = E_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class->authenticate_sync != NULL, FALSE);

	return class->authenticate_sync (backend, auth, cancellable, error);
}

/**
 * e_backend_authenticate:
 * @backend: an #EBackend
 * @auth: an #ESourceAuthenticator
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Convenience function providing a consistent authentication interface
 * for backends running in either the registry service itself or a client
 * process communicating with the registry service over D-Bus.
 *
 * Asynchronously authenticates @backend's #EBackend:source, using @auth
 * to handle authentication attempts.  The @backend and @auth arguments may
 * be one and the same if @backend implements the #ESourceAuthenticator
 * interface.  The operation loops until authentication is succesful or the
 * user aborts further authentication attempts.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_backend_authenticate_finish() to get the result of the operation.
 *
 * Since: 3.6
 **/
void
e_backend_authenticate (EBackend *backend,
                        ESourceAuthenticator *auth,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	EBackendClass *class;

	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth));

	class = E_BACKEND_GET_CLASS (backend);
	g_return_if_fail (class->authenticate != NULL);

	class->authenticate (backend, auth, cancellable, callback, user_data);
}

/**
 * e_backend_authenticate_finish:
 * @backend: an #EBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_backend_authenticate().  If
 * an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_backend_authenticate_finish (EBackend *backend,
                               GAsyncResult *result,
                               GError **error)
{
	EBackendClass *class;

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = E_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class->authenticate_finish != NULL, FALSE);

	return class->authenticate_finish (backend, result, error);
}


/*
 * e-backend.c
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

#include <config.h>
#include <glib/gi18n-lib.h>

#include <gio/gio.h>

#include <libedataserver/libedataserver.h>

#include "e-backend.h"
#include "e-user-prompter.h"

#define E_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BACKEND, EBackendPrivate))

#define G_IS_IO_ERROR(error, code) \
	(g_error_matches ((error), G_IO_ERROR, (code)))

#define G_IS_RESOLVER_ERROR(error, code) \
	(g_error_matches ((error), G_RESOLVER_ERROR, (code)))

typedef struct _AsyncContext AsyncContext;

struct _EBackendPrivate {
	GMutex property_lock;
	ESource *source;
	EUserPrompter *prompter;
	GMainContext *main_context;
	GSocketConnectable *connectable;
	gboolean online;

	GNetworkMonitor *network_monitor;
	gulong network_changed_handler_id;

	GSource *update_online_state;
	GMutex update_online_state_lock;

	GMutex network_monitor_cancellable_lock;
	GCancellable *network_monitor_cancellable;
};

struct _AsyncContext {
	ESourceAuthenticator *auth;
};

enum {
	PROP_0,
	PROP_CONNECTABLE,
	PROP_MAIN_CONTEXT,
	PROP_ONLINE,
	PROP_SOURCE,
	PROP_USER_PROMPTER
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
backend_network_monitor_can_reach_cb (GObject *source_object,
                                      GAsyncResult *result,
                                      gpointer user_data)
{
	EBackend *backend = E_BACKEND (user_data);
	gboolean host_is_reachable;
	GError *error = NULL;

	host_is_reachable = g_network_monitor_can_reach_finish (
		G_NETWORK_MONITOR (source_object), result, &error);

	/* Sanity check. */
	g_return_if_fail (
		(host_is_reachable && (error == NULL)) ||
		(!host_is_reachable && (error != NULL)));

	if (G_IS_IO_ERROR (error, G_IO_ERROR_CANCELLED) ||
	    host_is_reachable == e_backend_get_online (backend)) {
		g_clear_error (&error);
		g_object_unref (backend);
		return;
	}

	g_clear_error (&error);

	e_backend_set_online (backend, host_is_reachable);

	g_object_unref (backend);
}

static gboolean
backend_update_online_state_timeout_cb (gpointer user_data)
{
	EBackend *backend;
	GSocketConnectable *connectable;
	GCancellable *cancellable;
	GSource *current_source;

	current_source = g_main_current_source ();
	if (current_source && g_source_is_destroyed (current_source))
		return FALSE;

	backend = E_BACKEND (user_data);
	connectable = e_backend_ref_connectable (backend);

	g_mutex_lock (&backend->priv->update_online_state_lock);
	g_source_unref (backend->priv->update_online_state);
	backend->priv->update_online_state = NULL;
	g_mutex_unlock (&backend->priv->update_online_state_lock);

	g_mutex_lock (&backend->priv->network_monitor_cancellable_lock);

	cancellable = backend->priv->network_monitor_cancellable;
	backend->priv->network_monitor_cancellable = NULL;

	if (cancellable != NULL) {
		g_cancellable_cancel (cancellable);
		g_object_unref (cancellable);
		cancellable = NULL;
	}

	if (!connectable) {
		gchar *host = NULL;
		guint16 port = 0;

		if (e_backend_get_destination_address (backend, &host, &port) && host)
			connectable = g_network_address_new (host, port);

		g_free (host);
	}

	if (connectable == NULL) {
		e_backend_set_online (backend, TRUE);
	} else {
		cancellable = g_cancellable_new ();

		g_network_monitor_can_reach_async (
			backend->priv->network_monitor,
			connectable, cancellable,
			backend_network_monitor_can_reach_cb,
			g_object_ref (backend));
	}

	backend->priv->network_monitor_cancellable = cancellable;

	g_mutex_unlock (&backend->priv->network_monitor_cancellable_lock);

	if (connectable != NULL)
		g_object_unref (connectable);

	return FALSE;
}

static void
backend_update_online_state (EBackend *backend)
{
	g_mutex_lock (&backend->priv->update_online_state_lock);

	if (backend->priv->update_online_state) {
		g_source_destroy (backend->priv->update_online_state);
		g_source_unref (backend->priv->update_online_state);
		backend->priv->update_online_state = NULL;
	}

	if (backend->priv->update_online_state == NULL) {
		GMainContext *main_context;
		GSource *timeout_source;

		main_context = e_backend_ref_main_context (backend);

		timeout_source = g_timeout_source_new_seconds (5);
		g_source_set_priority (timeout_source, G_PRIORITY_LOW);
		g_source_set_callback (
			timeout_source,
			backend_update_online_state_timeout_cb,
			g_object_ref (backend),
			(GDestroyNotify) g_object_unref);
		g_source_attach (timeout_source, main_context);
		backend->priv->update_online_state =
			g_source_ref (timeout_source);
		g_source_unref (timeout_source);

		g_main_context_unref (main_context);
	}

	g_mutex_unlock (&backend->priv->update_online_state_lock);
}

static void
backend_network_changed_cb (GNetworkMonitor *network_monitor,
                            gboolean network_available,
                            EBackend *backend)
{
	backend_update_online_state (backend);
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
		case PROP_CONNECTABLE:
			e_backend_set_connectable (
				E_BACKEND (object),
				g_value_get_object (value));
			return;

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
		case PROP_CONNECTABLE:
			g_value_take_object (
				value, e_backend_ref_connectable (
				E_BACKEND (object)));
			return;

		case PROP_MAIN_CONTEXT:
			g_value_take_boxed (
				value, e_backend_ref_main_context (
				E_BACKEND (object)));
			return;

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

		case PROP_USER_PROMPTER:
			g_value_set_object (
				value, e_backend_get_user_prompter (
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

	if (priv->network_changed_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->network_monitor,
			priv->network_changed_handler_id);
		priv->network_changed_handler_id = 0;
	}

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	if (priv->update_online_state != NULL) {
		g_source_destroy (priv->update_online_state);
		g_source_unref (priv->update_online_state);
		priv->update_online_state = NULL;
	}

	g_clear_object (&priv->source);
	g_clear_object (&priv->prompter);
	g_clear_object (&priv->connectable);
	g_clear_object (&priv->network_monitor);
	g_clear_object (&priv->network_monitor_cancellable);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_backend_parent_class)->dispose (object);
}

static void
backend_finalize (GObject *object)
{
	EBackendPrivate *priv;

	priv = E_BACKEND_GET_PRIVATE (object);

	g_mutex_clear (&priv->property_lock);
	g_mutex_clear (&priv->update_online_state_lock);
	g_mutex_clear (&priv->network_monitor_cancellable_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_backend_parent_class)->finalize (object);
}

static void
backend_constructed (GObject *object)
{
	EBackend *backend;
	ESource *source;
	const gchar *extension_name;

	backend = E_BACKEND (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_backend_parent_class)->constructed (object);

	/* Get an initial GSocketConnectable from the data
	 * source's [Authentication] extension, if present. */
	source = e_backend_get_source (backend);
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (e_source_has_extension (source, extension_name)) {
		ESourceAuthentication *extension;

		extension = e_source_get_extension (source, extension_name);

		backend->priv->connectable =
			e_source_authentication_ref_connectable (extension);

		backend_update_online_state (backend);
	}
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

static gboolean
backend_get_destination_address (EBackend *backend,
                                 gchar **host,
                                 guint16 *port)
{
	/* default implementation returns FALSE, indicating
	 * no remote destination being used for this backend */
	return FALSE;
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
	object_class->finalize = backend_finalize;
	object_class->constructed = backend_constructed;

	class->authenticate_sync = backend_authenticate_sync;
	class->authenticate = backend_authenticate;
	class->authenticate_finish = backend_authenticate_finish;
	class->get_destination_address = backend_get_destination_address;

	g_object_class_install_property (
		object_class,
		PROP_CONNECTABLE,
		g_param_spec_object (
			"connectable",
			"Connectable",
			"Socket endpoint of a network service",
			G_TYPE_SOCKET_CONNECTABLE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_MAIN_CONTEXT,
		g_param_spec_boxed (
			"main-context",
			"Main Context",
			"The main loop context on "
			"which to attach event sources",
			G_TYPE_MAIN_CONTEXT,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			"Online",
			"Whether the backend is online",
			TRUE,
			G_PARAM_READWRITE |
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

	g_object_class_install_property (
		object_class,
		PROP_USER_PROMPTER,
		g_param_spec_object (
			"user-prompter",
			"User Prompter",
			"User prompter instance",
			E_TYPE_USER_PROMPTER,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_backend_init (EBackend *backend)
{
	GNetworkMonitor *network_monitor;
	gulong handler_id;

	backend->priv = E_BACKEND_GET_PRIVATE (backend);
	backend->priv->prompter = e_user_prompter_new ();
	backend->priv->main_context = g_main_context_ref_thread_default ();
	backend->priv->online = TRUE;

	g_mutex_init (&backend->priv->property_lock);
	g_mutex_init (&backend->priv->update_online_state_lock);
	g_mutex_init (&backend->priv->network_monitor_cancellable_lock);

	/* Configure network monitoring. */

	network_monitor = g_network_monitor_get_default ();
	backend->priv->network_monitor = g_object_ref (network_monitor);

	handler_id = g_signal_connect (
		backend->priv->network_monitor, "network-changed",
		G_CALLBACK (backend_network_changed_cb), backend);
	backend->priv->network_changed_handler_id = handler_id;
}

/**
 * e_backend_get_online:
 * @backend: an #EBackend
 *
 * Returns the online state of @backend: %TRUE if @backend is online,
 * %FALSE if offline.
 *
 * If the #EBackend:connectable property is non-%NULL, the @backend will
 * automatically determine whether the network service should be reachable,
 * and hence whether the @backend is #EBackend:online.  But subclasses may
 * override the online state if, for example, a connection attempt fails.
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
 * @FALSE if offline.
 *
 * If the #EBackend:connectable property is non-%NULL, the @backend will
 * automatically determine whether the network service should be reachable,
 * and hence whether the @backend is #EBackend:online.  But subclasses may
 * override the online state if, for example, a connection attempt fails.
 *
 * Since: 3.4
 **/
void
e_backend_set_online (EBackend *backend,
                      gboolean online)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	/* Avoid unnecessary "notify" signals. */
	if (backend->priv->online == online)
		return;

	backend->priv->online = online;

	/* Cancel any automatic "online" state update in progress. */
	g_mutex_lock (&backend->priv->network_monitor_cancellable_lock);
	g_cancellable_cancel (backend->priv->network_monitor_cancellable);
	g_mutex_unlock (&backend->priv->network_monitor_cancellable_lock);

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
 * e_backend_ref_connectable:
 * @backend: an #EBackend
 *
 * Returns the socket endpoint for the network service to which @backend
 * is a client, or %NULL if @backend does not use network sockets.
 *
 * The initial value of the #EBackend:connectable property is derived from
 * the #ESourceAuthentication extension of the @backend's #EBackend:source
 * property, if the extension is present.
 *
 * The returned #GSocketConnectable is referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #GSocketConnectable, or %NULL
 *
 * Since: 3.8
 **/
GSocketConnectable *
e_backend_ref_connectable (EBackend *backend)
{
	GSocketConnectable *connectable = NULL;

	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	g_mutex_lock (&backend->priv->property_lock);

	if (backend->priv->connectable != NULL)
		connectable = g_object_ref (backend->priv->connectable);

	g_mutex_unlock (&backend->priv->property_lock);

	return connectable;
}

/**
 * e_backend_set_connectable:
 * @backend: an #EBackend
 * @connectable: a #GSocketConnectable, or %NULL
 *
 * Sets the socket endpoint for the network service to which @backend is
 * a client.  This can be %NULL if @backend does not use network sockets.
 *
 * The initial value of the #EBackend:connectable property is derived from
 * the #ESourceAuthentication extension of the @backend's #EBackend:source
 * property, if the extension is present.
 *
 * Since: 3.8
 **/
void
e_backend_set_connectable (EBackend *backend,
                           GSocketConnectable *connectable)
{
	g_return_if_fail (E_IS_BACKEND (backend));

	if (connectable != NULL) {
		g_return_if_fail (G_IS_SOCKET_CONNECTABLE (connectable));
		g_object_ref (connectable);
	}

	g_mutex_lock (&backend->priv->property_lock);

	if (backend->priv->connectable != NULL)
		g_object_unref (backend->priv->connectable);

	backend->priv->connectable = connectable;

	g_mutex_unlock (&backend->priv->property_lock);

	backend_update_online_state (backend);

	g_object_notify (G_OBJECT (backend), "connectable");
}

/**
 * e_backend_ref_main_context:
 * @backend: an #EBackend
 *
 * Returns the #GMainContext on which event sources for @backend are to
 * be attached.
 *
 * The returned #GMainContext is referenced for thread-safety and must be
 * unreferenced with g_main_context_unref() when finished with it.
 *
 * Returns: (transfer full): a #GMainContext
 *
 * Since: 3.8
 **/
GMainContext *
e_backend_ref_main_context (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return g_main_context_ref (backend->priv->main_context);
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

/**
 * e_backend_get_user_prompter:
 * @backend: an #EBackend
 *
 * Gets an instance of #EUserPrompter, associated with this @backend.
 *
 * The returned instance is owned by the @backend.
 *
 * Returns: (transfer none): an #EUserPrompter instance
 *
 * Since: 3.8
 **/
EUserPrompter *
e_backend_get_user_prompter (EBackend *backend)
{
	g_return_val_if_fail (E_IS_BACKEND (backend), NULL);

	return backend->priv->prompter;
}

/**
 * e_backend_trust_prompt_sync:
 * @backend: an #EBackend
 * @parameters: an #ENamedParameters with values for the trust prompt
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Asks a user a trust prompt with given @parameters, and returns what
 * user responded. This blocks until the response is delivered.
 *
 * Returns: an #ETrustPromptResponse what user responded
 *
 * Note: The function can return also %E_TRUST_PROMPT_RESPONSE_UNKNOWN,
 *    it's on error or if user closes the trust prompt dialog with other
 *    than the offered buttons. Usual behaviour in such case is to treat
 *    it as a temporary reject.
 *
 * Since: 3.8
 **/
ETrustPromptResponse
e_backend_trust_prompt_sync (EBackend *backend,
                             const ENamedParameters *parameters,
                             GCancellable *cancellable,
                             GError **error)
{
	EUserPrompter *prompter;
	gint response;

	g_return_val_if_fail (
		E_IS_BACKEND (backend), E_TRUST_PROMPT_RESPONSE_UNKNOWN);
	g_return_val_if_fail (
		parameters != NULL, E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	prompter = e_backend_get_user_prompter (backend);
	g_return_val_if_fail (
		prompter != NULL, E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	response = e_user_prompter_extension_prompt_sync (
		prompter, "ETrustPrompt::trust-prompt",
		parameters, NULL, cancellable, error);

	if (response == 0)
		return E_TRUST_PROMPT_RESPONSE_REJECT;
	if (response == 1)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT;
	if (response == 2)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY;
	if (response == -1)
		return E_TRUST_PROMPT_RESPONSE_REJECT_TEMPORARILY;

	return E_TRUST_PROMPT_RESPONSE_UNKNOWN;
}

/**
 * e_backend_trust_prompt:
 * @backend: an #EBackend
 * @parameters: an #ENamedParameters with values for the trust prompt
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Initiates a user trust prompt with given @parameters.
 *
 * When the operation is finished, @callback will be called. You can then
 * call e_backend_trust_prompt_finish() to get the result of the operation.
 *
 * Since: 3.8
 **/
void
e_backend_trust_prompt (EBackend *backend,
                        const ENamedParameters *parameters,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	EUserPrompter *prompter;

	g_return_if_fail (E_IS_BACKEND (backend));
	g_return_if_fail (parameters != NULL);

	prompter = e_backend_get_user_prompter (backend);
	g_return_if_fail (prompter != NULL);

	e_user_prompter_extension_prompt (
		prompter, "ETrustPrompt::trust-prompt",
		parameters, cancellable, callback, user_data);
}

/**
 * e_backend_trust_prompt_finish:
 * @backend: an #EBackend
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_backend_trust_prompt().
 * If an error occurred, the function will set @error and return
 * %E_TRUST_PROMPT_RESPONSE_UNKNOWN.
 *
 * Returns: an #ETrustPromptResponse what user responded
 *
 * Note: The function can return also %E_TRUST_PROMPT_RESPONSE_UNKNOWN,
 *    it's on error or if user closes the trust prompt dialog with other
 *    than the offered buttons. Usual behaviour in such case is to treat
 *    it as a temporary reject.
 *
 * Since: 3.8
 **/
ETrustPromptResponse
e_backend_trust_prompt_finish (EBackend *backend,
                               GAsyncResult *result,
                               GError **error)
{
	EUserPrompter *prompter;
	gint response;

	g_return_val_if_fail (
		E_IS_BACKEND (backend), E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	prompter = e_backend_get_user_prompter (backend);
	g_return_val_if_fail (
		prompter != NULL, E_TRUST_PROMPT_RESPONSE_UNKNOWN);

	response = e_user_prompter_extension_prompt_finish (
		prompter, result, NULL, error);

	if (response == 0)
		return E_TRUST_PROMPT_RESPONSE_REJECT;
	if (response == 1)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT;
	if (response == 2)
		return E_TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY;
	if (response == -1)
		return E_TRUST_PROMPT_RESPONSE_REJECT_TEMPORARILY;

	return E_TRUST_PROMPT_RESPONSE_UNKNOWN;
}

/**
 * e_backend_get_destination_address:
 * @backend: an #EBackend instance
 * @host: (out): destination server host name
 * @port: (out): destination server port
 *
 * Provides destination server host name and port to which
 * the backend connects. This is used to determine required
 * connection point for e_backend_destination_is_reachable().
 * The @host is a newly allocated string, which will be freed
 * with g_free(). When @backend sets both @host and @port, then
 * it should return %TRUE, indicating it's a remote backend.
 * Default implementation returns %FALSE, which is treated
 * like the backend is local, no checking for server reachability
 * is possible.
 *
 * Returns: %TRUE, when it's a remote backend and provides both
 *   @host and @port; %FALSE otherwise.
 *
 * Since: 3.8
 **/
gboolean
e_backend_get_destination_address (EBackend *backend,
                                   gchar **host,
                                   guint16 *port)
{
	EBackendClass *klass;

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (host != NULL, FALSE);
	g_return_val_if_fail (port != NULL, FALSE);

	klass = E_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (klass->get_destination_address != NULL, FALSE);

	return klass->get_destination_address (backend, host, port);
}

/**
 * e_backend_is_destination_reachable:
 * @backend: an #EBackend instance
 * @cancellable: a #GCancellable instance, or %NULL
 * @error: a #GError for errors, or %NULL
 *
 * Checks whether the @backend<!-- -->'s destination server, as returned
 * by e_backend_get_destination_address(), is reachable.
 * If the e_backend_get_destination_address() returns %FALSE, this function
 * returns %TRUE, meaning the destination is always reachable.
 * This uses #GNetworkMonitor<!-- -->'s g_network_monitor_can_reach()
 * for reachability tests.
 *
 * Returns: %TRUE, when destination server address is reachable or
 *    the backend doesn't provide destination address; %FALSE if
 *    the backend destination server cannot be reached currently.
 *
 * Since: 3.8
 **/
gboolean
e_backend_is_destination_reachable (EBackend *backend,
                                    GCancellable *cancellable,
                                    GError **error)
{
	gboolean reachable = TRUE;
	gchar *host = NULL;
	guint16 port = 0;

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);

	if (e_backend_get_destination_address (backend, &host, &port)) {
		g_warn_if_fail (host != NULL);

		if (host) {
			GNetworkMonitor *network_monitor;
			GSocketConnectable *connectable;

			network_monitor = backend->priv->network_monitor;

			connectable = g_network_address_new (host, port);
			if (connectable) {
				reachable = g_network_monitor_can_reach (
					network_monitor, connectable,
					cancellable, error);
				g_object_unref (connectable);
			} else {
				reachable = FALSE;
			}
		}
	}

	g_free (host);

	return reachable;
}

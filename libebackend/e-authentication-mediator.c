/*
 * e-authentication-mediator.c
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
 * SECTION: e-authentication-mediator
 * @include: libebackend/libebackend.h
 * @short_description: Authenticator proxy for remote clients
 *
 * #EAuthenticationMediator runs on the registry D-Bus service.  It mediates
 * authentication attempts between the client requesting authentication and
 * the server-side #EAuthenticationSession interacting with the user and/or
 * secret service.  It implements the #ESourceAuthenticator interface and
 * securely transmits passwords to a remote #ESourceRegistry over D-Bus.
 **/

#include "e-authentication-mediator.h"

/* XXX Yeah, yeah... */
#define GCR_API_SUBJECT_TO_CHANGE

#include <config.h>
#include <glib/gi18n-lib.h>
#include <gcr/gcr-base.h>

/* Private D-Bus classes. */
#include <e-dbus-authenticator.h>

#define E_AUTHENTICATION_MEDIATOR_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_AUTHENTICATION_MEDIATOR, EAuthenticationMediatorPrivate))

/* How long should clients have to respond before timing out the
 * authentication session?  Need to balance allowing adequate time
 * without blocking other requests too long if a client gets stuck. */
#define INACTIVITY_TIMEOUT (2 * 60)  /* in seconds */

typedef struct _AsyncContext AsyncContext;
typedef struct _ThreadClosure ThreadClosure;

struct _EAuthenticationMediatorPrivate {
	GDBusConnection *connection;
	EDBusAuthenticator *dbus_interface;
	GcrSecretExchange *secret_exchange;
	gchar *object_path;
	gchar *sender;

	ThreadClosure *thread_closure;

	GMutex shared_data_lock;

	GQueue try_password_queue;
	GQueue wait_for_client_queue;

	gboolean client_is_ready;
	gboolean client_cancelled;
	gboolean client_vanished;

	guint watcher_id;
};

struct _AsyncContext {
	/* These point into the EAuthenticationMediatorPrivate
	 * struct.  Do not free them in async_context_free(). */
	GMutex *shared_data_lock;
	GQueue *operation_queue;

	GCancellable *cancellable;
	gulong cancel_id;
	guint timeout_id;
};

struct _ThreadClosure {
	volatile gint ref_count;
	GWeakRef mediator;
	GMainContext *main_context;
	GMainLoop *main_loop;
	GCond main_loop_cond;
	GMutex main_loop_mutex;
	GError *export_error;
};

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_OBJECT_PATH,
	PROP_SENDER
};

/* Forward Declarations */
static void	e_authentication_mediator_initable_init
				(GInitableIface *iface);
static void	e_authentication_mediator_interface_init
				(ESourceAuthenticatorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EAuthenticationMediator,
	e_authentication_mediator,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_authentication_mediator_initable_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_authentication_mediator_interface_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->cancellable != NULL) {
		g_cancellable_disconnect (
			async_context->cancellable,
			async_context->cancel_id);
		g_object_unref (async_context->cancellable);
	}

	if (async_context->timeout_id > 0)
		g_source_remove (async_context->timeout_id);

	g_slice_free (AsyncContext, async_context);
}

static ThreadClosure *
thread_closure_new (EAuthenticationMediator *mediator)
{
	ThreadClosure *closure;

	closure = g_slice_new0 (ThreadClosure);
	closure->ref_count = 1;
	g_weak_ref_init (&closure->mediator, mediator);
	closure->main_context = g_main_context_new ();
	/* It's important to pass 'is_running=FALSE' here because
	 * we wait for the main loop to start running as a way of
	 * synchronizing with the manager thread. */
	closure->main_loop = g_main_loop_new (closure->main_context, FALSE);
	g_cond_init (&closure->main_loop_cond);
	g_mutex_init (&closure->main_loop_mutex);

	return closure;
}

static ThreadClosure *
thread_closure_ref (ThreadClosure *closure)
{
	g_return_val_if_fail (closure != NULL, NULL);
	g_return_val_if_fail (closure->ref_count > 0, NULL);

	g_atomic_int_inc (&closure->ref_count);

	return closure;
}

static void
thread_closure_unref (ThreadClosure *closure)
{
	g_return_if_fail (closure != NULL);
	g_return_if_fail (closure->ref_count > 0);

	if (g_atomic_int_dec_and_test (&closure->ref_count)) {
		g_weak_ref_clear (&closure->mediator);
		g_main_context_unref (closure->main_context);
		g_main_loop_unref (closure->main_loop);
		g_cond_clear (&closure->main_loop_cond);
		g_mutex_clear (&closure->main_loop_mutex);

		g_slice_free (ThreadClosure, closure);
	}
}

static void
authentication_mediator_name_vanished_cb (GDBusConnection *connection,
                                          const gchar *name,
                                          gpointer user_data)
{
	EAuthenticationMediator *mediator;
	GSimpleAsyncResult *simple;
	GQueue *queue;

	mediator = E_AUTHENTICATION_MEDIATOR (user_data);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	mediator->priv->client_vanished = TRUE;

	queue = &mediator->priv->try_password_queue;

	/* Notify any unfinished try_password() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Bus name vanished (client terminated?)"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	queue = &mediator->priv->wait_for_client_queue;

	/* Notify any unfinished wait_for_client() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Bus name vanished (client terminated?)"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_bus_unwatch_name (mediator->priv->watcher_id);
	mediator->priv->watcher_id = 0;

	g_mutex_unlock (&mediator->priv->shared_data_lock);
}

static void
authentication_mediator_cancelled_cb (GCancellable *cancellable,
                                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	g_mutex_lock (async_context->shared_data_lock);

	/* Because we called g_simple_async_result_set_check_cancellable(),
	 * g_simple_async_result_propagate_error() will automatically set a
	 * cancelled error so we don't need to explicitly set one here. */
	if (g_queue_remove (async_context->operation_queue, simple)) {
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (async_context->shared_data_lock);
}

static gboolean
authentication_mediator_timeout_cb (gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	g_mutex_lock (async_context->shared_data_lock);

	if (g_queue_remove (async_context->operation_queue, simple)) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
			"%s", _("No response from client"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (async_context->shared_data_lock);

	return FALSE;
}

static gboolean
authentication_mediator_handle_ready (EDBusAuthenticator *dbus_interface,
                                      GDBusMethodInvocation *invocation,
                                      const gchar *encrypted_key,
                                      ThreadClosure *closure)
{
	EAuthenticationMediator *mediator;
	GcrSecretExchange *secret_exchange;
	GSimpleAsyncResult *simple;
	GQueue *queue;

	mediator = g_weak_ref_get (&closure->mediator);
	g_return_val_if_fail (mediator != NULL, FALSE);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	mediator->priv->client_is_ready = TRUE;

	secret_exchange = mediator->priv->secret_exchange;
	gcr_secret_exchange_receive (secret_exchange, encrypted_key);

	queue = &mediator->priv->wait_for_client_queue;

	/* Notify any unfinished wait_for_client() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	e_dbus_authenticator_complete_ready (dbus_interface, invocation);

	g_object_unref (mediator);

	return TRUE;
}

static gboolean
authentication_mediator_handle_cancel (EDBusAuthenticator *dbus_interface,
                                       GDBusMethodInvocation *invocation,
                                       ThreadClosure *closure)
{
	EAuthenticationMediator *mediator;
	GSimpleAsyncResult *simple;
	GQueue *queue;

	mediator = g_weak_ref_get (&closure->mediator);
	g_return_val_if_fail (mediator != NULL, FALSE);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	mediator->priv->client_cancelled = TRUE;

	queue = &mediator->priv->try_password_queue;

	/* Notify any unfinished try_password() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Client cancelled the operation"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	queue = &mediator->priv->wait_for_client_queue;

	/* Notify any unfinished wait_for_client() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Client cancelled the operation"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	e_dbus_authenticator_complete_cancel (dbus_interface, invocation);

	g_object_unref (mediator);

	return TRUE;
}

static gboolean
authentication_mediator_handle_accepted (EDBusAuthenticator *dbus_interface,
                                         GDBusMethodInvocation *invocation,
                                         ThreadClosure *closure)
{
	EAuthenticationMediator *mediator;
	GSimpleAsyncResult *simple;
	GQueue *queue;

	mediator = g_weak_ref_get (&closure->mediator);
	g_return_val_if_fail (mediator != NULL, FALSE);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	queue = &mediator->priv->try_password_queue;

	if (g_queue_is_empty (queue))
		g_warning ("%s: Unexpected 'accepted' signal", G_STRFUNC);

	/* Notify any unfinished try_password() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	e_dbus_authenticator_complete_accepted (dbus_interface, invocation);

	g_object_unref (mediator);

	return TRUE;
}

static gboolean
authentication_mediator_handle_rejected (EDBusAuthenticator *dbus_interface,
                                         GDBusMethodInvocation *invocation,
                                         ThreadClosure *closure)
{
	EAuthenticationMediator *mediator;
	GSimpleAsyncResult *simple;
	GQueue *queue;

	mediator = g_weak_ref_get (&closure->mediator);
	g_return_val_if_fail (mediator != NULL, FALSE);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	queue = &mediator->priv->try_password_queue;

	if (g_queue_is_empty (queue))
		g_warning ("%s: Unexpected 'rejected' signal", G_STRFUNC);

	/* Notify any unfinished try_password() operations. */
	while ((simple = g_queue_pop_head (queue)) != NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			"%s", _("Client reports password was rejected"));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	e_dbus_authenticator_complete_rejected (dbus_interface, invocation);

	g_object_unref (mediator);

	return TRUE;
}

static gboolean
authentication_mediator_authenticator_running (gpointer data)
{
	ThreadClosure *closure = data;

	g_mutex_lock (&closure->main_loop_mutex);
	g_cond_broadcast (&closure->main_loop_cond);
	g_mutex_unlock (&closure->main_loop_mutex);

	return FALSE;
}

static gpointer
authentication_mediator_authenticator_thread (gpointer data)
{
	EAuthenticationMediator *mediator;
	GDBusInterfaceSkeleton *dbus_interface;
	GDBusConnection *connection;
	ThreadClosure *closure = data;
	GSource *idle_source;
	const gchar *object_path;
	gulong handle_ready_id;
	gulong handle_cancel_id;
	gulong handle_accepted_id;
	gulong handle_rejected_id;

	/* This is similar to the manager thread in ESourceRegistry.
	 * GDBusInterfaceSkeleton will emit "handle-*" signals from
	 * the GMainContext that was the thread-default at the time
	 * the interface was exported.  So we export the interface
	 * from an isolated thread to prevent its signal emissions
	 * from being inhibited by someone pushing a thread-default
	 * GMainContext. */

	mediator = g_weak_ref_get (&closure->mediator);
	g_return_val_if_fail (mediator != NULL, NULL);

	/* Keep our own reference to the GDBusInterfaceSkeleton so
	 * we can clean up signals after the mediator is disposed. */
	dbus_interface = g_object_ref (mediator->priv->dbus_interface);

	connection = e_authentication_mediator_get_connection (mediator);
	object_path = e_authentication_mediator_get_object_path (mediator);

	/* This becomes the GMainContext from which the Authenticator
	 * interface will emit method invocation signals.  Make it the
	 * thread-default context for this thread before exporting the
	 * interface. */

	g_main_context_push_thread_default (closure->main_context);

	/* Listen for method invocations. */

	handle_ready_id = g_signal_connect_data (
		dbus_interface, "handle-ready",
		G_CALLBACK (authentication_mediator_handle_ready),
		thread_closure_ref (closure),
		(GClosureNotify) thread_closure_unref, 0);

	handle_cancel_id = g_signal_connect_data (
		dbus_interface, "handle-cancel",
		G_CALLBACK (authentication_mediator_handle_cancel),
		thread_closure_ref (closure),
		(GClosureNotify) thread_closure_unref, 0);

	handle_accepted_id = g_signal_connect_data (
		dbus_interface, "handle-accepted",
		G_CALLBACK (authentication_mediator_handle_accepted),
		thread_closure_ref (closure),
		(GClosureNotify) thread_closure_unref, 0);

	handle_rejected_id = g_signal_connect_data (
		dbus_interface, "handle-rejected",
		G_CALLBACK (authentication_mediator_handle_rejected),
		thread_closure_ref (closure),
		(GClosureNotify) thread_closure_unref, 0);

	/* Export the Authenticator interface. */

	g_dbus_interface_skeleton_export (
		dbus_interface, connection, object_path, &closure->export_error);

	/* Schedule a one-time idle callback to broadcast through a
	 * condition variable that our main loop is up and running. */

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		authentication_mediator_authenticator_running,
		closure, (GDestroyNotify) NULL);
	g_source_attach (idle_source, closure->main_context);
	g_source_unref (idle_source);

	/* Unreference this before starting the main loop since
	 * the mediator's dispose() method tells us when to quit.
	 * If we don't do this then dispose() will never run. */
	g_object_unref (mediator);

	/* Now we mostly idle here until authentication is complete. */

	g_main_loop_run (closure->main_loop);

	/* Clean up and exit. */

	g_signal_handler_disconnect (dbus_interface, handle_ready_id);
	g_signal_handler_disconnect (dbus_interface, handle_cancel_id);
	g_signal_handler_disconnect (dbus_interface, handle_accepted_id);
	g_signal_handler_disconnect (dbus_interface, handle_rejected_id);

	g_main_context_pop_thread_default (closure->main_context);

	g_object_unref (dbus_interface);

	thread_closure_unref (closure);

	return NULL;
}

static void
authentication_mediator_set_connection (EAuthenticationMediator *mediator,
                                        GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (mediator->priv->connection == NULL);

	mediator->priv->connection = g_object_ref (connection);
}

static void
authentication_mediator_set_object_path (EAuthenticationMediator *mediator,
                                         const gchar *object_path)
{
	g_return_if_fail (object_path != NULL);
	g_return_if_fail (mediator->priv->object_path == NULL);

	mediator->priv->object_path = g_strdup (object_path);
}

static void
authentication_mediator_set_sender (EAuthenticationMediator *mediator,
                                    const gchar *sender)
{
	g_return_if_fail (sender != NULL);
	g_return_if_fail (mediator->priv->sender == NULL);

	mediator->priv->sender = g_strdup (sender);
}

static void
authentication_mediator_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			authentication_mediator_set_connection (
				E_AUTHENTICATION_MEDIATOR (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			authentication_mediator_set_object_path (
				E_AUTHENTICATION_MEDIATOR (object),
				g_value_get_string (value));
			return;

		case PROP_SENDER:
			authentication_mediator_set_sender (
				E_AUTHENTICATION_MEDIATOR (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
authentication_mediator_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_authentication_mediator_get_connection (
				E_AUTHENTICATION_MEDIATOR (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_authentication_mediator_get_object_path (
				E_AUTHENTICATION_MEDIATOR (object)));
			return;

		case PROP_SENDER:
			g_value_set_string (
				value,
				e_authentication_mediator_get_sender (
				E_AUTHENTICATION_MEDIATOR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
authentication_mediator_dispose (GObject *object)
{
	EAuthenticationMediatorPrivate *priv;
	GQueue *queue;

	priv = E_AUTHENTICATION_MEDIATOR_GET_PRIVATE (object);

	/* Signal the authenticator thread to terminate. */
	if (priv->thread_closure != NULL) {
		g_main_loop_quit (priv->thread_closure->main_loop);
		thread_closure_unref (priv->thread_closure);
		priv->thread_closure = NULL;
	}

	if (priv->connection != NULL) {
		g_object_unref (priv->connection);
		priv->connection = NULL;
	}

	if (priv->dbus_interface != NULL) {
		g_object_unref (priv->dbus_interface);
		priv->dbus_interface = NULL;
	}

	if (priv->secret_exchange != NULL) {
		g_object_unref (priv->secret_exchange);
		priv->secret_exchange = NULL;
	}

	queue = &priv->wait_for_client_queue;

	while (!g_queue_is_empty (queue))
		g_object_unref (g_queue_pop_head (queue));

	if (priv->watcher_id > 0) {
		g_bus_unwatch_name (priv->watcher_id);
		priv->watcher_id = 0;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_authentication_mediator_parent_class)->
		dispose (object);
}

static void
authentication_mediator_finalize (GObject *object)
{
	EAuthenticationMediatorPrivate *priv;

	priv = E_AUTHENTICATION_MEDIATOR_GET_PRIVATE (object);

	g_mutex_clear (&priv->shared_data_lock);

	g_free (priv->object_path);
	g_free (priv->sender);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_authentication_mediator_parent_class)->
		finalize (object);
}

static void
authentication_mediator_constructed (GObject *object)
{
	EAuthenticationMediator *mediator;
	GDBusConnection *connection;
	const gchar *sender;

	mediator = E_AUTHENTICATION_MEDIATOR (object);
	connection = e_authentication_mediator_get_connection (mediator);
	sender = e_authentication_mediator_get_sender (mediator);

	/* This should notify us if the client process terminates. */
	mediator->priv->watcher_id =
		g_bus_watch_name_on_connection (
			connection, sender,
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			NULL,
			authentication_mediator_name_vanished_cb,
			mediator, (GDestroyNotify) NULL);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_authentication_mediator_parent_class)->constructed (object);
}

static gboolean
authentication_mediator_initable_init (GInitable *initable,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EAuthenticationMediator *mediator;
	ThreadClosure *closure;
	GThread *thread;

	mediator = E_AUTHENTICATION_MEDIATOR (initable);

	/* The first closure reference is for the authenticator thread. */
	closure = thread_closure_new (mediator);

	/* The mediator holds the second closure reference since we need
	 * to tell the authenticator thread's main loop to quit when the
	 * mediator is disposed.  Terminating the authenticator thread's
	 * main loop will signal the thread itself to terminate. */
	mediator->priv->thread_closure = thread_closure_ref (closure);

	thread = g_thread_new (
		NULL,
		authentication_mediator_authenticator_thread,
		closure);

	if (thread == NULL) {
		thread_closure_unref (closure);
		return FALSE;
	}

	g_thread_unref (thread);

	/* Wait for notification that the Authenticator interface
	 * has been exported and the thread's main loop started. */
	g_mutex_lock (&closure->main_loop_mutex);
	while (!g_main_loop_is_running (closure->main_loop))
		g_cond_wait (
			&closure->main_loop_cond,
			&closure->main_loop_mutex);
	g_mutex_unlock (&closure->main_loop_mutex);

	/* Check whether the interface failed to export. */
	if (closure->export_error != NULL) {
		g_propagate_error (error, closure->export_error);
		closure->export_error = NULL;
		return FALSE;
	}

	return TRUE;
}

static gboolean
authentication_mediator_get_without_password (ESourceAuthenticator *auth)
{
	EAuthenticationMediator *mediator;

	mediator = E_AUTHENTICATION_MEDIATOR (auth);

	return e_dbus_authenticator_get_without_password (mediator->priv->dbus_interface);
}

static ESourceAuthenticationResult
authentication_mediator_try_password_sync (ESourceAuthenticator *auth,
                                           const GString *password,
                                           GCancellable *cancellable,
                                           GError **error)
{
	ESourceAuthenticationResult auth_result;
	GAsyncResult *async_result;
	EAsyncClosure *closure;

	closure = e_async_closure_new ();

	e_source_authenticator_try_password (
		auth, password, cancellable,
		e_async_closure_callback, closure);

	async_result = e_async_closure_wait (closure);

	auth_result = e_source_authenticator_try_password_finish (
		auth, async_result, error);

	e_async_closure_free (closure);

	return auth_result;
}

static void
authentication_mediator_try_password (ESourceAuthenticator *auth,
                                      const GString *password,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	EAuthenticationMediator *mediator;
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	mediator = E_AUTHENTICATION_MEDIATOR (auth);

	async_context = g_slice_new0 (AsyncContext);
	async_context->shared_data_lock = &mediator->priv->shared_data_lock;
	async_context->operation_queue = &mediator->priv->try_password_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (mediator), callback, user_data,
		authentication_mediator_try_password);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	if (G_IS_CANCELLABLE (cancellable)) {
		async_context->cancellable = g_object_ref (cancellable);
		async_context->cancel_id = g_cancellable_connect (
			async_context->cancellable,
			G_CALLBACK (authentication_mediator_cancelled_cb),
			simple, (GDestroyNotify) NULL);
	}

	async_context->timeout_id = e_named_timeout_add_seconds (
		INACTIVITY_TIMEOUT,
		authentication_mediator_timeout_cb, simple);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	if (mediator->priv->client_cancelled) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Client cancelled the operation"));
		g_simple_async_result_complete_in_idle (simple);

	} else if (mediator->priv->client_vanished) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Bus name vanished (client terminated?)"));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		gchar *encrypted_secret;

		g_queue_push_tail (
			async_context->operation_queue,
			g_object_ref (simple));

		encrypted_secret = gcr_secret_exchange_send (
			mediator->priv->secret_exchange, password->str, -1);

		e_dbus_authenticator_emit_authenticate (
			mediator->priv->dbus_interface, encrypted_secret);

		g_free (encrypted_secret);
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	g_object_unref (simple);
}

static ESourceAuthenticationResult
authentication_mediator_try_password_finish (ESourceAuthenticator *auth,
                                             GAsyncResult *result,
                                             GError **error)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (!g_simple_async_result_propagate_error (simple, &local_error))
		return E_SOURCE_AUTHENTICATION_ACCEPTED;

	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
		g_clear_error (&local_error);
		return E_SOURCE_AUTHENTICATION_REJECTED;
	}

	g_propagate_error (error, local_error);

	return E_SOURCE_AUTHENTICATION_ERROR;
}

static void
e_authentication_mediator_class_init (EAuthenticationMediatorClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EAuthenticationMediatorPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = authentication_mediator_set_property;
	object_class->get_property = authentication_mediator_get_property;
	object_class->dispose = authentication_mediator_dispose;
	object_class->finalize = authentication_mediator_finalize;
	object_class->constructed = authentication_mediator_constructed;

	g_object_class_install_property (
		object_class,
		PROP_CONNECTION,
		g_param_spec_object (
			"connection",
			"Connection",
			"The GDBusConnection on which to "
			"export the authenticator interface",
			G_TYPE_DBUS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OBJECT_PATH,
		g_param_spec_string (
			"object-path",
			"Object Path",
			"The object path at which to "
			"export the authenticator interface",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SENDER,
		g_param_spec_string (
			"sender",
			"Sender",
			"Unique bus name of the process that "
			"initiated the authentication session",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_authentication_mediator_initable_init (GInitableIface *iface)
{
	iface->init = authentication_mediator_initable_init;
}

static void
e_authentication_mediator_interface_init (ESourceAuthenticatorInterface *iface)
{
	iface->get_without_password =
		authentication_mediator_get_without_password;
	iface->try_password_sync =
		authentication_mediator_try_password_sync;
	iface->try_password =
		authentication_mediator_try_password;
	iface->try_password_finish =
		authentication_mediator_try_password_finish;
}

static void
e_authentication_mediator_init (EAuthenticationMediator *mediator)
{
	mediator->priv = E_AUTHENTICATION_MEDIATOR_GET_PRIVATE (mediator);

	mediator->priv->dbus_interface = e_dbus_authenticator_skeleton_new ();
	mediator->priv->secret_exchange = gcr_secret_exchange_new (NULL);

	g_mutex_init (&mediator->priv->shared_data_lock);
}

/**
 * e_authentication_mediator_new:
 * @connection: a #GDBusConnection
 * @object_path: object path of the authentication session
 * @sender: bus name of the client requesting authentication
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EAuthenticationMediator and exports the Authenticator
 * D-Bus interface on @connection at @object_path.  If the Authenticator
 * interface fails to export, the function sets @error and returns %NULL.
 *
 * #EAuthenticationMediator watches the bus name of the client requesting
 * authentication, given by @sender.  If it sees the bus name vanish, it
 * cancels the authentication session so the next authentication session
 * can begin without delay.
 *
 * Returns: an #EAuthenticationMediator, or %NULL on error
 *
 * Since: 3.6
 **/
ESourceAuthenticator *
e_authentication_mediator_new (GDBusConnection *connection,
                               const gchar *object_path,
                               const gchar *sender,
                               GError **error)
{
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (object_path != NULL, NULL);
	g_return_val_if_fail (sender != NULL, NULL);

	return g_initable_new (
		E_TYPE_AUTHENTICATION_MEDIATOR, NULL, error,
		"connection", connection,
		"object-path", object_path,
		"sender", sender, NULL);
}

/**
 * e_authentication_mediator_get_connection:
 * @mediator: an #EAuthenticationMediator
 *
 * Returns the #GDBusConnection on which the Authenticator D-Bus interface
 * is exported.
 *
 * Returns: the #GDBusConnection
 *
 * Since: 3.6
 **/
GDBusConnection *
e_authentication_mediator_get_connection (EAuthenticationMediator *mediator)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator), NULL);

	return mediator->priv->connection;
}

/**
 * e_authentication_mediator_get_object_path:
 * @mediator: an #EAuthenticationMediator
 *
 * Returns the object path at which the Authenticator D-Bus interface is
 * exported.
 *
 * Returns: the object path
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_mediator_get_object_path (EAuthenticationMediator *mediator)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator), NULL);

	return mediator->priv->object_path;
}

/**
 * e_authentication_mediator_get_sender:
 * @mediator: an #EAuthenticationMediator
 *
 * Returns the authentication client's unique bus name.
 *
 * Returns: the client's bus name
 *
 * Since: 3.6
 **/
const gchar *
e_authentication_mediator_get_sender (EAuthenticationMediator *mediator)
{
	g_return_val_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator), NULL);

	return mediator->priv->sender;
}

/**
 * e_authentication_mediator_wait_for_client_sync:
 * @mediator: an #EAuthenticationMediator
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Waits for the authentication client to indicate it is ready to begin
 * authentication attempts.  Call this function to synchronize with the
 * client before initiating any authentication attempts through @mediator.
 *
 * If the authentication client's bus name vanishes or the client fails
 * to signal it is ready before a timer expires, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE if the client is ready, %FALSE if an error occurred
 *
 * Since: 3.6
 **/
gboolean
e_authentication_mediator_wait_for_client_sync (EAuthenticationMediator *mediator,
                                                GCancellable *cancellable,
                                                GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator), FALSE);

	closure = e_async_closure_new ();

	e_authentication_mediator_wait_for_client (
		mediator, cancellable, e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_authentication_mediator_wait_for_client_finish (
		mediator, result, error);

	e_async_closure_free (closure);

	return success;
}

/**
 * e_authentication_mediator_wait_for_client:
 * @mediator: an #EAuthenticationMediator
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously waits for the authentication client to indicate it
 * is ready to being authentication attempts.  Call this function to
 * synchronize with the client before initiating any authentication
 * attempts through @mediator.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_authentication_mediator_wait_for_client_finished() to get the
 * result of the operation.
 *
 * Since: 3.6
 **/
void
e_authentication_mediator_wait_for_client (EAuthenticationMediator *mediator,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator));

	async_context = g_slice_new0 (AsyncContext);
	async_context->shared_data_lock = &mediator->priv->shared_data_lock;
	async_context->operation_queue = &mediator->priv->wait_for_client_queue;

	simple = g_simple_async_result_new (
		G_OBJECT (mediator), callback, user_data,
		e_authentication_mediator_wait_for_client);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	if (G_IS_CANCELLABLE (cancellable)) {
		async_context->cancellable = g_object_ref (cancellable);
		async_context->cancel_id = g_cancellable_connect (
			cancellable,
			G_CALLBACK (authentication_mediator_cancelled_cb),
			simple, (GDestroyNotify) NULL);
	}

	async_context->timeout_id = e_named_timeout_add_seconds (
		INACTIVITY_TIMEOUT,
		authentication_mediator_timeout_cb, simple);

	g_mutex_lock (&mediator->priv->shared_data_lock);

	if (mediator->priv->client_is_ready) {
		g_simple_async_result_complete_in_idle (simple);

	} else if (mediator->priv->client_cancelled) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Client cancelled the operation"));
		g_simple_async_result_complete_in_idle (simple);

	} else if (mediator->priv->client_vanished) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_CANCELLED,
			"%s", _("Bus name vanished (client terminated?)"));
		g_simple_async_result_complete_in_idle (simple);

	} else {
		g_queue_push_tail (
			async_context->operation_queue,
			g_object_ref (simple));
	}

	g_mutex_unlock (&mediator->priv->shared_data_lock);

	g_object_unref (simple);
}

/**
 * e_authentication_mediator_wait_for_client_finish:
 * @mediator: an #EAuthenticationMediator
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with
 * e_authentication_mediator_wait_for_client().
 *
 * If the authentication client's bus name vanishes or the client fails
 * to signal it is ready before a timer expires, the function sets @error
 * and returns %FALSE.
 *
 * Returns: %TRUE if the client is ready, %FALSE if an error occurred
 *
 * Since: 3.6
 **/
gboolean
e_authentication_mediator_wait_for_client_finish (EAuthenticationMediator *mediator,
                                                  GAsyncResult *result,
                                                  GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (mediator),
		e_authentication_mediator_wait_for_client), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_authentication_mediator_dismiss:
 * @mediator: an #EAuthenticationMediator
 *
 * Signals to the authentication client that the user declined to provide a
 * password when prompted and that the authentication session has terminated.
 * This is also called when a server-side error has occurred, but the client
 * doesn't need to know the difference.
 *
 * Since: 3.6
 **/
void
e_authentication_mediator_dismiss (EAuthenticationMediator *mediator)
{
	g_return_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator));

	e_dbus_authenticator_emit_dismissed (mediator->priv->dbus_interface);
}

/**
 * e_authentication_mediator_server_error:
 * @mediator: an #EAuthenticationMediator
 * @error: the #GError to report
 *
 * Signals to the authentication client that the authentication session has
 * terminated with a server-side error.
 *
 * Since: 3.12
 **/
void
e_authentication_mediator_server_error (EAuthenticationMediator *mediator,
                                        const GError *error)
{
	gchar *name;

	g_return_if_fail (E_IS_AUTHENTICATION_MEDIATOR (mediator));
	g_return_if_fail (error != NULL);

	name = g_dbus_error_encode_gerror (error);
	g_return_if_fail (name != NULL);

	e_dbus_authenticator_emit_server_error (mediator->priv->dbus_interface, name, error->message);

	g_free (name);
}

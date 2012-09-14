/*
 * e-client.c
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

#include <glib/gi18n-lib.h>
#include <gio/gio.h>

#include <libedataserver/e-data-server-util.h>

#include "e-gdbus-marshallers.h"

#include "e-client.h"
#include "e-client-private.h"

#define E_CLIENT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CLIENT, EClientPrivate))

struct _EClientPrivate {
	GStaticRecMutex prop_mutex;

	ESource *source;
	gchar *uri;
	gboolean online;
	gboolean readonly;
	gboolean opened;
	gboolean capabilities_retrieved;
	GSList *capabilities;

	GHashTable *backend_property_cache;

	GStaticRecMutex ops_mutex;
	guint32 last_opid;
	GHashTable *ops; /* opid to GCancellable */
};

enum {
	PROP_0,
	PROP_SOURCE,
	PROP_CAPABILITIES,
	PROP_READONLY,
	PROP_ONLINE,
	PROP_OPENED
};

enum {
	OPENED,
	BACKEND_ERROR,
	BACKEND_DIED,
	BACKEND_PROPERTY_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (EClient, e_client, G_TYPE_OBJECT)

/*
 * Well-known client backend properties, which are common for each #EClient:
 * @CLIENT_BACKEND_PROPERTY_OPENED: Is set to "TRUE" or "FALSE" depending
 *   whether the backend is fully opened.
 * @CLIENT_BACKEND_PROPERTY_OPENING: Is set to "TRUE" or "FALSE" depending
 *   whether the backend is processing its opening phase.
 * @CLIENT_BACKEND_PROPERTY_ONLINE: Is set to "TRUE" or "FALSE" depending
 *   on the backend's loaded state. See also e_client_is_online().
 * @CLIENT_BACKEND_PROPERTY_READONLY: Is set to "TRUE" or "FALSE" depending
 *   on the backend's readonly state. See also e_client_is_readonly().
 * @CLIENT_BACKEND_PROPERTY_CACHE_DIR: Local folder with cached data used
 *   by the backend.
 * @CLIENT_BACKEND_PROPERTY_CAPABILITIES: Retrieves comma-separated list
 *   of	capabilities supported by the backend. Preferred method of retreiving
 *   and working with capabilities is e_client_get_capabilities() and
 *   e_client_check_capability().
 */

GQuark
e_client_error_quark (void)
{
	static GQuark q = 0;

	if (q == 0)
		q = g_quark_from_static_string ("e-client-error-quark");

	return q;
}

/**
 * e_client_error_to_string:
 *
 * FIXME: Document me.
 *
 * Since: 3.2
 **/
const gchar *
e_client_error_to_string (EClientError code)
{
	switch (code) {
	case E_CLIENT_ERROR_INVALID_ARG:
		return _("Invalid argument");
	case E_CLIENT_ERROR_BUSY:
		return _("Backend is busy");
	case E_CLIENT_ERROR_SOURCE_NOT_LOADED:
		return _("Source not loaded");
	case E_CLIENT_ERROR_SOURCE_ALREADY_LOADED:
		return _("Source already loaded");
	case E_CLIENT_ERROR_AUTHENTICATION_FAILED:
		return _("Authentication failed");
	case E_CLIENT_ERROR_AUTHENTICATION_REQUIRED:
		return _("Authentication required");
	case E_CLIENT_ERROR_REPOSITORY_OFFLINE:
		return _("Repository offline");
	case E_CLIENT_ERROR_OFFLINE_UNAVAILABLE:
		/* Translators: This means that the EClient does not support offline mode, or
		 * it's not set to by a user, thus it is unavailable while user is not connected. */
		return _("Offline unavailable");
	case E_CLIENT_ERROR_PERMISSION_DENIED:
		return _("Permission denied");
	case E_CLIENT_ERROR_CANCELLED:
		return _("Cancelled");
	case E_CLIENT_ERROR_COULD_NOT_CANCEL:
		return _("Could not cancel");
	case E_CLIENT_ERROR_NOT_SUPPORTED:
		return _("Not supported");
	case E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD:
		return _("Unsupported authentication method");
	case E_CLIENT_ERROR_TLS_NOT_AVAILABLE:
		return _("TLS not available");
	case E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED:
		return _("Search size limit exceeded");
	case E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED:
		return _("Search time limit exceeded");
	case E_CLIENT_ERROR_INVALID_QUERY:
		return _("Invalid query");
	case E_CLIENT_ERROR_QUERY_REFUSED:
		return _("Query refused");
	case E_CLIENT_ERROR_DBUS_ERROR:
		return _("D-Bus error");
	case E_CLIENT_ERROR_OTHER_ERROR:
		return _("Other error");
	case E_CLIENT_ERROR_NOT_OPENED:
		return _("Backend is not opened yet");
	}

	return _("Unknown error");
}

/**
 * e_client_error_create:
 * @code: an #EClientError code to create
 * @custom_msg: custom message to use for the error; can be %NULL
 *
 * Returns: a new #GError containing an E_CLIENT_ERROR of the given
 * @code. If the @custom_msg is NULL, then the error message is
 * the one returned from e_client_error_to_string() for the @code,
 * otherwise the given message is used.
 *
 * Returned pointer should be freed with g_error_free().
 *
 * Since: 3.2
 **/
GError *
e_client_error_create (EClientError code,
                       const gchar *custom_msg)
{
	return g_error_new_literal (E_CLIENT_ERROR, code, custom_msg ? custom_msg : e_client_error_to_string (code));
}

static void client_set_source (EClient *client, ESource *source);

static void
e_client_init (EClient *client)
{
	client->priv = E_CLIENT_GET_PRIVATE (client);

	client->priv->readonly = TRUE;

	g_static_rec_mutex_init (&client->priv->prop_mutex);
	client->priv->backend_property_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	g_static_rec_mutex_init (&client->priv->ops_mutex);
	client->priv->last_opid = 0;
	client->priv->ops = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

static void
client_dispose (GObject *object)
{
	EClient *client;

	client = E_CLIENT (object);

	e_client_cancel_all (client);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_client_parent_class)->dispose (object);
}

static void
client_finalize (GObject *object)
{
	EClient *client;
	EClientPrivate *priv;

	client = E_CLIENT (object);

	priv = client->priv;

	g_static_rec_mutex_lock (&priv->prop_mutex);

	if (priv->source) {
		g_object_unref (priv->source);
		priv->source = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->capabilities) {
		g_slist_foreach (priv->capabilities, (GFunc) g_free, NULL);
		g_slist_free (priv->capabilities);
		priv->capabilities = NULL;
	}

	if (priv->backend_property_cache) {
		g_hash_table_destroy (priv->backend_property_cache);
		priv->backend_property_cache = NULL;
	}

	if (priv->ops) {
		g_hash_table_destroy (priv->ops);
		priv->ops = NULL;
	}

	g_static_rec_mutex_unlock (&priv->prop_mutex);
	g_static_rec_mutex_free (&priv->prop_mutex);
	g_static_rec_mutex_free (&priv->ops_mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_client_parent_class)->finalize (object);
}

static void
client_set_property (GObject *object,
                     guint property_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			client_set_source (E_CLIENT (object), g_value_get_object (value));
			return;

		case PROP_ONLINE:
			e_client_set_online (E_CLIENT (object), g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
client_get_property (GObject *object,
                     guint property_id,
                     GValue *value,
                     GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_set_object (value, e_client_get_source (E_CLIENT (object)));
			return;

		case PROP_CAPABILITIES:
			g_value_set_pointer (value, (gpointer) e_client_get_capabilities (E_CLIENT (object)));
			return;

		case PROP_READONLY:
			g_value_set_boolean (value, e_client_is_readonly (E_CLIENT (object)));
			return;

		case PROP_ONLINE:
			g_value_set_boolean (value, e_client_is_online (E_CLIENT (object)));
			return;

		case PROP_OPENED:
			g_value_set_boolean (value, e_client_is_opened (E_CLIENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
client_remove_thread (GSimpleAsyncResult *simple,
                      GObject *object,
                      GCancellable *cancellable)
{
	GError *error = NULL;

	e_client_remove_sync (E_CLIENT (object), cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
client_remove (EClient *client,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data, client_remove);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, client_remove_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

static gboolean
client_remove_finish (EClient *client,
                      GAsyncResult *result,
                      GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (client), client_remove), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
client_remove_sync (EClient *client,
                    GCancellable *cancellable,
                    GError **error)
{
	ESource *source;

	source = e_client_get_source (client);

	return e_source_remove_sync (source, cancellable, error);
}

static void
e_client_class_init (EClientClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EClientPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = client_set_property;
	object_class->get_property = client_get_property;
	object_class->dispose = client_dispose;
	object_class->finalize = client_finalize;

	class->remove = client_remove;
	class->remove_finish = client_remove_finish;
	class->remove_sync = client_remove_sync;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			NULL,
			NULL,
			E_TYPE_SOURCE,
			G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_CAPABILITIES,
		g_param_spec_pointer (
			"capabilities",
			NULL,
			NULL,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_READONLY,
		g_param_spec_boolean (
			"readonly",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READABLE));

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_OPENED,
		g_param_spec_boolean (
			"opened",
			NULL,
			NULL,
			FALSE,
			G_PARAM_READABLE));

	signals[OPENED] = g_signal_new (
		"opened",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientClass, opened),
		NULL, NULL,
		g_cclosure_marshal_VOID__BOXED,
		G_TYPE_NONE, 1,
		G_TYPE_ERROR);

	signals[BACKEND_ERROR] = g_signal_new (
		"backend-error",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (EClientClass, backend_error),
		NULL, NULL,
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE, 1,
		G_TYPE_STRING);

	signals[BACKEND_DIED] = g_signal_new (
		"backend-died",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientClass, backend_died),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	signals[BACKEND_PROPERTY_CHANGED] = g_signal_new (
		"backend-property-changed",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EClientClass, backend_property_changed),
		NULL, NULL,
		e_gdbus_marshallers_VOID__STRING_STRING,
		G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
client_set_source (EClient *client,
                   ESource *source)
{
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (E_IS_SOURCE (source));

	g_object_ref (source);

	if (client->priv->source)
		g_object_unref (client->priv->source);

	client->priv->source = source;
}

/**
 * e_client_get_source:
 * @client: an #EClient
 *
 * Get the #ESource that this client has assigned.
 *
 * Returns: (transfer none): The source.
 *
 * Since: 3.2
 **/
ESource *
e_client_get_source (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);

	return client->priv->source;
}

static void
client_ensure_capabilities (EClient *client)
{
	gchar *capabilities;

	g_return_if_fail (E_IS_CLIENT (client));

	if (client->priv->capabilities_retrieved || client->priv->capabilities)
		return;

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	capabilities = NULL;
	e_client_retrieve_capabilities_sync (client, &capabilities, NULL, NULL);
	/* e_client_set_capabilities is called inside the previous function */
	g_free (capabilities);

	client->priv->capabilities_retrieved = TRUE;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);
}

/**
 * e_client_get_capabilities:
 * @client: an #EClient
 *
 * Get list of strings with capabilities advertised by a backend.
 * This list, together with inner strings, is owned by the @client.
 * To check for individual capabilities use e_client_check_capability().
 *
 * Returns: (element-type utf8) (transfer none): #GSList of const strings
 *          of capabilities
 *
 * Since: 3.2
 **/
const GSList *
e_client_get_capabilities (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), NULL);

	client_ensure_capabilities (client);

	return client->priv->capabilities;
}

/**
 * e_client_check_capability:
 * @client: an #EClient
 * @capability: a capability
 *
 * Check if backend supports particular capability.
 * To get all capabilities use e_client_get_capabilities().
 *
 * Returns: #GSList of const strings of capabilities
 *
 * Since: 3.2
 **/
gboolean
e_client_check_capability (EClient *client,
                           const gchar *capability)
{
	GSList *iter;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (capability, FALSE);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	client_ensure_capabilities (client);

	for (iter = client->priv->capabilities; iter; iter = g_slist_next (iter)) {
		const gchar *cap = iter->data;

		if (cap && g_ascii_strcasecmp (cap, capability) == 0) {
			g_static_rec_mutex_unlock (&client->priv->prop_mutex);
			return TRUE;
		}
	}

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	return FALSE;
}

/**
 * e_client_check_refresh_supported:
 * @client: A client.
 *
 * Checks whether a client supports explicit refreshing
 * (see e_client_refresh()).
 *
 * Returns: TRUE if the client supports refreshing, FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_check_refresh_supported (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	return e_client_check_capability (client, "refresh-supported");
}

/* capabilities - comma-separated list of capabilities; can be NULL to unset */
void
e_client_set_capabilities (EClient *client,
                           const gchar *capabilities)
{
	g_return_if_fail (E_IS_CLIENT (client));

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	if (!capabilities)
		client->priv->capabilities_retrieved = FALSE;

	g_slist_foreach (client->priv->capabilities, (GFunc) g_free, NULL);
	g_slist_free (client->priv->capabilities);
	client->priv->capabilities = e_client_util_parse_comma_strings (capabilities);

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "capabilities");
}

/**
 * e_client_is_readonly:
 * @client: an #EClient
 *
 * Check if this @client is read-only.
 *
 * Returns: %TRUE if this @client is read-only, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_client_is_readonly (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), TRUE);

	return client->priv->readonly;
}

void
e_client_set_readonly (EClient *client,
                       gboolean readonly)
{
	g_return_if_fail (E_IS_CLIENT (client));

	g_static_rec_mutex_lock (&client->priv->prop_mutex);
	if ((readonly ? 1 : 0) == (client->priv->readonly ? 1 : 0)) {
		g_static_rec_mutex_unlock (&client->priv->prop_mutex);
		return;
	}

	client->priv->readonly = readonly;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "readonly");
}

/**
 * e_client_is_online:
 * @client: an #EClient
 *
 * Check if this @client is connected.
 *
 * Returns: %TRUE if this @client is connected, otherwise %FALSE.
 *
 * Since: 3.2
 **/
gboolean
e_client_is_online (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	return client->priv->online;
}

void
e_client_set_online (EClient *client,
                     gboolean is_online)
{
	g_return_if_fail (E_IS_CLIENT (client));

	/* newly connected/disconnected => make sure capabilities will be correct */
	e_client_set_capabilities (client, NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);
	if ((is_online ? 1: 0) == (client->priv->online ? 1 : 0)) {
		g_static_rec_mutex_unlock (&client->priv->prop_mutex);
		return;
	}

	client->priv->online = is_online;

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	g_object_notify (G_OBJECT (client), "online");
}

/**
 * e_client_is_opened:
 * @client: an #EClient
 *
 * Check if this @client is fully opened. This includes
 * everything from e_client_open() call up to the authentication,
 * if required by a backend. Client cannot do any other operation
 * during the opening phase except of authenticate or cancel it.
 * Every other operation results in an %E_CLIENT_ERROR_BUSY error.
 *
 * Returns: %TRUE if this @client is fully opened, otherwise %FALSE.
 *
 * Since: 3.2.
 **/
gboolean
e_client_is_opened (EClient *client)
{
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	return client->priv->opened;
}

/*
 * client_cancel_op:
 * @client: an #EClient
 * @opid: asynchronous operation ID
 *
 * Cancels particular asynchronous operation. The @opid is returned from
 * an e_client_register_op(). The function does nothing if the asynchronous
 * operation doesn't exist any more.
 *
 * Since: 3.2
 */
static void
client_cancel_op (EClient *client,
                  guint32 opid)
{
	GCancellable *cancellable;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	cancellable = g_hash_table_lookup (client->priv->ops, GINT_TO_POINTER (opid));
	if (cancellable)
		g_cancellable_cancel (cancellable);

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

static void
gather_opids_cb (gpointer opid,
                 gpointer cancellable,
                 gpointer ids_list)
{
	GSList **ids = ids_list;

	g_return_if_fail (ids_list != NULL);

	*ids = g_slist_prepend (*ids, opid);
}

static void
cancel_op_cb (gpointer opid,
              gpointer client)
{
	client_cancel_op (client, GPOINTER_TO_INT (opid));
}

/**
 * e_client_cancel_all:
 * @client: an #EClient
 *
 * Cancels all pending operations started on @client.
 *
 * Since: 3.2
 **/
void
e_client_cancel_all (EClient *client)
{
	GSList *opids = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	g_hash_table_foreach (client->priv->ops, gather_opids_cb, &opids);

	g_slist_foreach (opids, cancel_op_cb, client);
	g_slist_free (opids);

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

guint32
e_client_register_op (EClient *client,
                      GCancellable *cancellable)
{
	guint32 opid;

	g_return_val_if_fail (E_IS_CLIENT (client), 0);
	g_return_val_if_fail (client->priv->ops != NULL, 0);
	g_return_val_if_fail (cancellable != NULL, 0);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);

	client->priv->last_opid++;
	if (!client->priv->last_opid)
		client->priv->last_opid++;

	while (g_hash_table_lookup (client->priv->ops, GINT_TO_POINTER (client->priv->last_opid)))
		client->priv->last_opid++;

	g_return_val_if_fail (client->priv->last_opid != 0, 0);

	opid = client->priv->last_opid;
	g_hash_table_insert (client->priv->ops, GINT_TO_POINTER (opid), g_object_ref (cancellable));

	g_static_rec_mutex_unlock (&client->priv->ops_mutex);

	return opid;
}

void
e_client_unregister_op (EClient *client,
                        guint32 opid)
{
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (client->priv->ops != NULL);

	g_static_rec_mutex_lock (&client->priv->ops_mutex);
	g_hash_table_remove (client->priv->ops, GINT_TO_POINTER (opid));
	g_static_rec_mutex_unlock (&client->priv->ops_mutex);
}

void
e_client_emit_opened (EClient *client,
                      const GError *dbus_error)
{
	GError *local_error = NULL;

	g_return_if_fail (E_IS_CLIENT (client));

	client->priv->opened = dbus_error == NULL;

	if (dbus_error) {
		local_error = g_error_copy (dbus_error);
		e_client_unwrap_dbus_error (client, local_error, &local_error);
	}

	g_object_notify (G_OBJECT (client), "opened");
	g_signal_emit (client, signals[OPENED], 0, local_error);

	if (local_error)
		g_error_free (local_error);
}

void
e_client_emit_backend_error (EClient *client,
                             const gchar *error_msg)
{
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (error_msg != NULL);

	g_signal_emit (client, signals[BACKEND_ERROR], 0, error_msg);
}

void
e_client_emit_backend_died (EClient *client)
{
	g_return_if_fail (E_IS_CLIENT (client));

	g_signal_emit (client, signals[BACKEND_DIED], 0);
}

void
e_client_emit_backend_property_changed (EClient *client,
                                        const gchar *prop_name,
                                        const gchar *prop_value)
{
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name);
	g_return_if_fail (prop_value != NULL);

	e_client_update_backend_property_cache (client, prop_name, prop_value);

	g_signal_emit (client, signals[BACKEND_PROPERTY_CHANGED], 0, prop_name, prop_value);
}

void
e_client_update_backend_property_cache (EClient *client,
                                        const gchar *prop_name,
                                        const gchar *prop_value)
{
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (*prop_name);
	g_return_if_fail (prop_value != NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	if (client->priv->backend_property_cache)
		g_hash_table_insert (client->priv->backend_property_cache, g_strdup (prop_name), g_strdup (prop_value));

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);
}

gchar *
e_client_get_backend_property_from_cache (EClient *client,
                                          const gchar *prop_name)
{
	gchar *prop_value = NULL;

	g_return_val_if_fail (E_IS_CLIENT (client), NULL);
	g_return_val_if_fail (prop_name != NULL, NULL);
	g_return_val_if_fail (*prop_name, NULL);

	g_static_rec_mutex_lock (&client->priv->prop_mutex);

	if (client->priv->backend_property_cache)
		prop_value = g_strdup (g_hash_table_lookup (client->priv->backend_property_cache, prop_name));

	g_static_rec_mutex_unlock (&client->priv->prop_mutex);

	return prop_value;
}

/**
 * e_client_retrieve_capabilities:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Initiates retrieval of capabilities on the @client. This is usually
 * required only once, after the @client is opened. The returned value
 * is cached and any subsequent call of e_client_get_capabilities() and
 * e_client_check_capability() is using the cached value.
 * The call is finished by e_client_retrieve_capabilities_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_client_retrieve_capabilities (EClient *client,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->retrieve_capabilities != NULL);

	class->retrieve_capabilities (client, cancellable, callback, user_data);
}

/**
 * e_client_retrieve_capabilities_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @capabilities: (out): Comma-separated list of capabilities of the @client
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_retrieve_capabilities().
 * Returned value of @capabilities should be freed with g_free(),
 * when no longer needed.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_retrieve_capabilities_finish (EClient *client,
                                       GAsyncResult *result,
                                       gchar **capabilities,
                                       GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (capabilities != NULL, FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->retrieve_capabilities_finish != NULL, FALSE);

	*capabilities = NULL;
	res = class->retrieve_capabilities_finish (client, result, capabilities, error);

	e_client_set_capabilities (client, res ? *capabilities : NULL);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_retrieve_capabilities_sync:
 * @client: an #EClient
 * @capabilities: (out): Comma-separated list of capabilities of the @client
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Initiates retrieval of capabilities on the @client. This is usually
 * required only once, after the @client is opened. The returned value
 * is cached and any subsequent call of e_client_get_capabilities() and
 * e_client_check_capability() is using the cached value. Returned value
 * of @capabilities should be freed with g_free(), when no longer needed.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_retrieve_capabilities_sync (EClient *client,
                                     gchar **capabilities,
                                     GCancellable *cancellable,
                                     GError **error)
{
	EClientClass *class;
	gboolean res = FALSE;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (capabilities != NULL, FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->retrieve_capabilities_sync != NULL, FALSE);

	*capabilities = NULL;
	res = class->retrieve_capabilities_sync (client, capabilities, cancellable, error);

	e_client_set_capabilities (client, res ? *capabilities : NULL);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_get_backend_property:
 * @client: an #EClient
 * @prop_name: property name, whose value to retrieve; cannot be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries @client's backend for a property of name @prop_name.
 * The call is finished by e_client_get_backend_property_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_client_get_backend_property (EClient *client,
                               const gchar *prop_name,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (callback != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (prop_name != NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->get_backend_property != NULL);

	class->get_backend_property (client, prop_name, cancellable, callback, user_data);
}

/**
 * e_client_get_backend_property_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @prop_value: (out): Retrieved backend property value; cannot be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_get_backend_property().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_get_backend_property_finish (EClient *client,
                                      GAsyncResult *result,
                                      gchar **prop_value,
                                      GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->get_backend_property_finish != NULL, FALSE);

	res = class->get_backend_property_finish (client, result, prop_value, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_get_backend_property_sync:
 * @client: an #EClient
 * @prop_name: property name, whose value to retrieve; cannot be %NULL
 * @prop_value: (out): Retrieved backend property value; cannot be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Queries @client's backend for a property of name @prop_name.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_get_backend_property_sync (EClient *client,
                                    const gchar *prop_name,
                                    gchar **prop_value,
                                    GCancellable *cancellable,
                                    GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->get_backend_property_sync != NULL, FALSE);

	res = class->get_backend_property_sync (client, prop_name, prop_value, cancellable, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_set_backend_property:
 * @client: an #EClient
 * @prop_name: property name, whose value to change; cannot be %NULL
 * @prop_value: property value, to set; cannot be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Sets @client's backend property of name @prop_name
 * to value @prop_value. The call is finished
 * by e_client_set_backend_property_finish() from the @callback.
 *
 * Since: 3.2
 **/
void
e_client_set_backend_property (EClient *client,
                               const gchar *prop_name,
                               const gchar *prop_value,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (callback != NULL);
	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (prop_name != NULL);
	g_return_if_fail (prop_value != NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->set_backend_property != NULL);

	class->set_backend_property (client, prop_name, prop_value, cancellable, callback, user_data);
}

/**
 * e_client_set_backend_property_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_set_backend_property().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_set_backend_property_finish (EClient *client,
                                      GAsyncResult *result,
                                      GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->set_backend_property_finish != NULL, FALSE);

	res = class->set_backend_property_finish (client, result, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_set_backend_property_sync:
 * @client: an #EClient
 * @prop_name: property name, whose value to change; cannot be %NULL
 * @prop_value: property value, to set; cannot be %NULL
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Sets @client's backend property of name @prop_name
 * to value @prop_value.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_set_backend_property_sync (EClient *client,
                                    const gchar *prop_name,
                                    const gchar *prop_value,
                                    GCancellable *cancellable,
                                    GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->set_backend_property_sync != NULL, FALSE);

	res = class->set_backend_property_sync (client, prop_name, prop_value, cancellable, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_open:
 * @client: an #EClient
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Opens the @client, making it ready for queries and other operations.
 * The call is finished by e_client_open_finish() from the @callback.
 *
 * Since: 3.2
 **/
void
e_client_open (EClient *client,
               gboolean only_if_exists,
               GCancellable *cancellable,
               GAsyncReadyCallback callback,
               gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (callback != NULL);
	g_return_if_fail (E_IS_CLIENT (client));

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->open != NULL);

	class->open (client, only_if_exists, cancellable, callback, user_data);
}

/**
 * e_client_open_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_open().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_open_finish (EClient *client,
                      GAsyncResult *result,
                      GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->open_finish != NULL, FALSE);

	res = class->open_finish (client, result, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_open_sync:
 * @client: an #EClient
 * @only_if_exists: if %TRUE, fail if this book doesn't already exist, otherwise create it first
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Opens the @client, making it ready for queries and other operations.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_open_sync (EClient *client,
                    gboolean only_if_exists,
                    GCancellable *cancellable,
                    GError **error)
{
	EClientClass *class;
	gboolean res;

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->open_sync != NULL, FALSE);

	res = class->open_sync (client, only_if_exists, cancellable, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_remove:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Removes the backing data for this #EClient. For example, with the file
 * backend this deletes the database file. You cannot get it back!
 * The call is finished by e_client_remove_finish() from the @callback.
 *
 * Since: 3.2
 *
 * Deprecated: 3.6: Use e_source_remove() instead.
 **/
void
e_client_remove (EClient *client,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->remove != NULL);

	class->remove (client, cancellable, callback, user_data);
}

/**
 * e_client_remove_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_remove().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 *
 * Deprecated: 3.6: Use e_source_remove_finish() instead.
 **/
gboolean
e_client_remove_finish (EClient *client,
                        GAsyncResult *result,
                        GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->remove_finish != NULL, FALSE);

	res = class->remove_finish (client, result, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_remove_sync:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Removes the backing data for this #EClient. For example, with the file
 * backend this deletes the database file. You cannot get it back!
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 *
 * Deprecated: 3.6: Use e_source_remove_sync() instead.
 **/
gboolean
e_client_remove_sync (EClient *client,
                      GCancellable *cancellable,
                      GError **error)
{
	EClientClass *class;
	gboolean res;

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->remove_sync != NULL, FALSE);

	res = class->remove_sync (client, cancellable, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_refresh:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Initiates refresh on the @client. Finishing the method doesn't mean
 * that the refresh is done, backend only notifies whether it started
 * refreshing or not. Use e_client_check_refresh_supported() to check
 * whether the backend supports this method.
 * The call is finished by e_client_refresh_finish() from the @callback.
 *
 * Since: 3.2
 **/
void
e_client_refresh (EClient *client,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
	EClientClass *class;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->refresh != NULL);

	class->refresh (client, cancellable, callback, user_data);
}

/**
 * e_client_refresh_finish:
 * @client: an #EClient
 * @result: a #GAsyncResult
 * @error: (out): a #GError to set an error, if any
 *
 * Finishes previous call of e_client_refresh().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_refresh_finish (EClient *client,
                         GAsyncResult *result,
                         GError **error)
{
	EClientClass *class;
	gboolean res;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->refresh_finish != NULL, FALSE);

	res = class->refresh_finish (client, result, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_refresh_sync:
 * @client: an #EClient
 * @cancellable: a #GCancellable; can be %NULL
 * @error: (out): a #GError to set an error, if any
 *
 * Initiates refresh on the @client. Finishing the method doesn't mean
 * that the refresh is done, backend only notifies whether it started
 * refreshing or not. Use e_client_check_refresh_supported() to check
 * whether the backend supports this method.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_client_refresh_sync (EClient *client,
                       GCancellable *cancellable,
                       GError **error)
{
	EClientClass *class;
	gboolean res;

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, FALSE);
	g_return_val_if_fail (class->refresh_sync != NULL, FALSE);

	res = class->refresh_sync (client, cancellable, error);

	if (error && *error)
		e_client_unwrap_dbus_error (client, *error, error);

	return res;
}

/**
 * e_client_util_slist_to_strv:
 * @strings: (element-type utf8): a #GSList of strings (const gchar *)
 *
 * Convert a list of strings into a %NULL-terminated array of strings.
 *
 * Returns: (transfer full): Newly allocated %NULL-terminated array of strings.
 * The returned pointer should be freed with g_strfreev().
 *
 * Note: Paired function for this is e_client_util_strv_to_slist().
 *
 * Since: 3.2
 **/
gchar **
e_client_util_slist_to_strv (const GSList *strings)
{
	return e_util_slist_to_strv (strings);
}

/**
 * e_client_util_strv_to_slist:
 * @strv: a %NULL-terminated array of strings (const gchar *)
 *
 * Convert a %NULL-terminated array of strings to a list of strings.
 *
 * Returns: (transfer full) (element-type utf8): Newly allocated #GSList of
 * newly allocated strings. The returned pointer should be freed with
 * e_client_util_free_string_slist().
 *
 * Note: Paired function for this is e_client_util_slist_to_strv().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_strv_to_slist (const gchar * const *strv)
{
	return e_util_strv_to_slist (strv);
}

/**
 * e_client_util_copy_string_slist:
 * @copy_to: (element-type utf8) (allow-none): Where to copy; may be %NULL
 * @strings: (element-type utf8): #GSList of strings to be copied
 *
 * Copies the #GSList of strings to the end of @copy_to.
 *
 * Returns: (transfer full) (element-type utf8): New head of @copy_to.
 * The returned pointer can be freed with e_client_util_free_string_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_copy_string_slist (GSList *copy_to,
                                 const GSList *strings)
{
	return e_util_copy_string_slist (copy_to, strings);
}

/**
 * e_client_util_copy_object_slist:
 * @copy_to: (element-type GObject) (allow-none): Where to copy; may be %NULL
 * @objects: (element-type GObject): #GSList of #GObject<!-- -->s to be copied
 *
 * Copies a #GSList of #GObject<!-- -->s to the end of @copy_to.
 *
 * Returns: (transfer full) (element-type GObject): New head of @copy_to.
 * The returned pointer can be freed with e_client_util_free_object_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_copy_object_slist (GSList *copy_to,
                                 const GSList *objects)
{
	return e_util_copy_object_slist (copy_to, objects);
}

/**
 * e_client_util_free_string_slist:
 * @strings: (element-type utf8): a #GSList of strings (gchar *)
 *
 * Frees memory previously allocated by e_client_util_strv_to_slist().
 *
 * Since: 3.2
 **/
void
e_client_util_free_string_slist (GSList *strings)
{
	e_util_free_string_slist (strings);
}

/**
 * e_client_util_free_object_slist:
 * @objects: (element-type GObject): a #GSList of #GObject<!-- -->s
 *
 * Calls g_object_unref() on each member of @objects and then frees @objects
 * itself.
 *
 * Since: 3.2
 **/
void
e_client_util_free_object_slist (GSList *objects)
{
	e_util_free_object_slist (objects);
}

/**
 * e_client_util_parse_comma_strings:
 * @strings: string of comma-separated values
 *
 * Parses comma-separated list of values into #GSList.
 *
 * Returns: (transfer full) (element-type utf8): Newly allocated #GSList of
 * newly allocated strings corresponding to values parsed from @strings.
 * Free the returned pointer with e_client_util_free_string_slist().
 *
 * Since: 3.2
 **/
GSList *
e_client_util_parse_comma_strings (const gchar *strings)
{
	GSList *strs_slist = NULL;
	gchar **strs_strv = NULL;
	gint ii;

	if (!strings || !*strings)
		return NULL;

	strs_strv = g_strsplit (strings, ",", -1);
	g_return_val_if_fail (strs_strv != NULL, NULL);

	for (ii = 0; strs_strv && strs_strv[ii]; ii++) {
		gchar *str = g_strstrip (strs_strv[ii]);

		if (str && *str)
			strs_slist = g_slist_prepend (strs_slist, g_strdup (str));
	}

	g_strfreev (strs_strv);

	return g_slist_reverse (strs_slist);
}

void
e_client_finish_async_without_dbus (EClient *client,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data,
                                    gpointer source_tag,
                                    gpointer op_res,
                                    GDestroyNotify destroy_op_res)
{
	GCancellable *use_cancellable;
	GSimpleAsyncResult *simple;
	guint32 opid;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	opid = e_client_register_op (client, use_cancellable);
	g_return_if_fail (opid > 0);

	simple = g_simple_async_result_new (
		G_OBJECT (client), callback, user_data, source_tag);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, op_res, destroy_op_res);

	g_simple_async_result_complete_in_idle (simple);

	g_object_unref (simple);

	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);
}

GDBusProxy *
e_client_get_dbus_proxy (EClient *client)
{
	EClientClass *class;

	g_return_val_if_fail (E_IS_CLIENT (client), NULL);

	class = E_CLIENT_GET_CLASS (client);
	g_return_val_if_fail (class != NULL, NULL);
	g_return_val_if_fail (class->get_dbus_proxy != NULL, NULL);

	return class->get_dbus_proxy (client);
}

/**
 * e_client_unwrap_dbus_error:
 * @client: an #EClient
 * @dbus_error: a #GError returned bu D-Bus
 * @out_error: a #GError variable where to store the result
 *
 * Unwraps D-Bus error to local error. @dbus_error is automatically freed.
 * @dbus_erorr and @out_error can point to the same variable.
 *
 * Since: 3.2
 **/
void
e_client_unwrap_dbus_error (EClient *client,
                            GError *dbus_error,
                            GError **out_error)
{
	EClientClass *class;

	g_return_if_fail (E_IS_CLIENT (client));

	class = E_CLIENT_GET_CLASS (client);
	g_return_if_fail (class != NULL);
	g_return_if_fail (class->unwrap_dbus_error != NULL);

	if (!dbus_error || !out_error) {
		if (dbus_error)
			g_error_free (dbus_error);
	} else {
		class->unwrap_dbus_error (client, dbus_error, out_error);
	}
}

/**
 * e_client_util_unwrap_dbus_error:
 * @dbus_error: DBus #GError to unwrap
 * @client_error: (out): Resulting #GError; can be %NULL
 * @known_errors: List of known errors against which try to match
 * @known_errors_count: How many items are stored in @known_errors
 * @known_errors_domain: Error domain for @known_errors
 * @fail_when_none_matched: Whether to fail when none of @known_errors matches
 *
 * The function takes a @dbus_error and tries to find a match in @known_errors
 * for it, if it is a G_IO_ERROR, G_IO_ERROR_DBUS_ERROR. If it is anything else
 * then the @dbus_error is moved to @client_error.
 *
 * The @fail_when_none_matched influences behaviour. If it's %TRUE, and none of
 * @known_errors matches, or this is not a G_IO_ERROR_DBUS_ERROR, then %FALSE
 * is returned and the @client_error is left without change. Otherwise, the
 * @fail_when_none_matched is %FALSE, the error is always processed and will
 * result in E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR if none of @known_error
 * matches.
 *
 * Returns: Whether was @dbus_error processed into @client_error.
 *
 * Note: The @dbus_error is automatically freed if returned %TRUE.
 *
 * Since: 3.2
 **/
gboolean
e_client_util_unwrap_dbus_error (GError *dbus_error,
                                 GError **client_error,
                                 const EClientErrorsList *known_errors,
                                 guint known_errors_count,
                                 GQuark known_errors_domain,
                                 gboolean fail_when_none_matched)
{
	if (!client_error) {
		if (dbus_error)
			g_error_free (dbus_error);
		return TRUE;
	}

	if (!dbus_error) {
		*client_error = NULL;
		return TRUE;
	}

	if (dbus_error->domain == known_errors_domain) {
		*client_error = dbus_error;
		return TRUE;
	}

	if (known_errors) {
		if (g_error_matches (dbus_error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
			gchar *name;
			gint ii;

			name = g_dbus_error_get_remote_error (dbus_error);

			for (ii = 0; ii < known_errors_count; ii++) {
				if (g_ascii_strcasecmp (known_errors[ii].name, name) == 0) {
					g_free (name);

					g_dbus_error_strip_remote_error (dbus_error);
					*client_error = g_error_new_literal (known_errors_domain, known_errors[ii].err_code, dbus_error->message);
					g_error_free (dbus_error);
					return TRUE;
				}
			}

			g_free (name);
		}
	}

	if (fail_when_none_matched)
		return FALSE;

	if (g_error_matches (dbus_error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
		g_dbus_error_strip_remote_error (dbus_error);
		*client_error = g_error_new_literal (E_CLIENT_ERROR, E_CLIENT_ERROR_OTHER_ERROR, dbus_error->message);
		g_error_free (dbus_error);
	} else {
		g_dbus_error_strip_remote_error (dbus_error);
		*client_error = dbus_error;
	}

	return TRUE;
}

typedef struct _EClientAsyncOpData
{
	EClient *client;
	guint32 opid;

	gpointer source_tag;
	gchar *res_op_data; /* optional string to set on a GAsyncResult object as "res-op-data" user data */
	GAsyncReadyCallback callback;
	gpointer user_data;

	gboolean result; /* result of the finish function call */

	/* only one can be non-NULL, and the type is telling which 'out' value is valid */
	EClientProxyFinishVoidFunc finish_void;
	EClientProxyFinishBooleanFunc finish_boolean;
	EClientProxyFinishStringFunc finish_string;
	EClientProxyFinishStrvFunc finish_strv;
	EClientProxyFinishUintFunc finish_uint;

	union {
		gboolean val_boolean;
		gchar *val_string;
		gchar **val_strv;
		guint val_uint;
	} out;
} EClientAsyncOpData;

static void
async_data_free (EClientAsyncOpData *async_data)
{
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->client != NULL);

	e_client_unregister_op (async_data->client, async_data->opid);

	if (async_data->finish_string)
		g_free (async_data->out.val_string);
	else if (async_data->finish_strv)
		g_strfreev (async_data->out.val_strv);

	g_object_unref (async_data->client);
	g_free (async_data->res_op_data);
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

static void
finish_async_op (EClientAsyncOpData *async_data,
                 const GError *error,
                 gboolean in_idle)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->source_tag != NULL);
	g_return_if_fail (async_data->client != NULL);

	simple = g_simple_async_result_new (G_OBJECT (async_data->client), async_data->callback, async_data->user_data, async_data->source_tag);
	g_simple_async_result_set_op_res_gpointer (simple, async_data, (GDestroyNotify) async_data_free);

	if (async_data->res_op_data)
		g_object_set_data_full (G_OBJECT (simple), "res-op-data", g_strdup (async_data->res_op_data), g_free);

	if (error != NULL)
		g_simple_async_result_set_from_error (simple, error);

	if (in_idle) {
		g_object_set_data (G_OBJECT (simple), "run-main-depth", GINT_TO_POINTER (g_main_depth ()));
		g_idle_add (complete_async_op_in_idle_cb, simple);
	} else {
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	}
}

static void
async_result_ready_cb (GObject *source_object,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GError *error = NULL;
	EClientAsyncOpData *async_data;
	EClient *client;

	g_return_if_fail (result != NULL);
	g_return_if_fail (source_object != NULL);

	async_data = user_data;
	g_return_if_fail (async_data != NULL);
	g_return_if_fail (async_data->client != NULL);

	client = async_data->client;
	g_return_if_fail (e_client_get_dbus_proxy (client) == G_DBUS_PROXY (source_object));

	if (async_data->finish_void)
		async_data->result = async_data->finish_void (G_DBUS_PROXY (source_object), result, &error);
	else if (async_data->finish_boolean)
		async_data->result = async_data->finish_boolean (G_DBUS_PROXY (source_object), result, &async_data->out.val_boolean, &error);
	else if (async_data->finish_string)
		async_data->result = async_data->finish_string (G_DBUS_PROXY (source_object), result, &async_data->out.val_string, &error);
	else if (async_data->finish_strv)
		async_data->result = async_data->finish_strv (G_DBUS_PROXY (source_object), result, &async_data->out.val_strv, &error);
	else if (async_data->finish_uint)
		async_data->result = async_data->finish_uint (G_DBUS_PROXY (source_object), result, &async_data->out.val_uint, &error);
	else
		g_warning ("%s: Do not know how to finish async operation", G_STRFUNC);

	finish_async_op (async_data, error, FALSE);

	if (error != NULL)
		g_error_free (error);
}

static EClientAsyncOpData *
prepare_async_data (EClient *client,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data,
                    gpointer source_tag,
                    gboolean error_report_only,
                    EClientProxyFinishVoidFunc finish_void,
                    EClientProxyFinishBooleanFunc finish_boolean,
                    EClientProxyFinishStringFunc finish_string,
                    EClientProxyFinishStrvFunc finish_strv,
                    EClientProxyFinishUintFunc finish_uint,
                    GDBusProxy **proxy,
                    GCancellable **out_cancellable)
{
	EClientAsyncOpData *async_data;
	GCancellable *use_cancellable;
	guint32 opid;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (callback != NULL, NULL);
	g_return_val_if_fail (source_tag != NULL, NULL);

	if (!error_report_only) {
		g_return_val_if_fail (proxy != NULL, NULL);
		g_return_val_if_fail (out_cancellable != NULL, NULL);
		g_return_val_if_fail (finish_void || finish_boolean || finish_string || finish_strv || finish_uint, NULL);

		if (finish_void) {
			g_return_val_if_fail (finish_boolean == NULL, NULL);
			g_return_val_if_fail (finish_string == NULL, NULL);
			g_return_val_if_fail (finish_strv == NULL, NULL);
			g_return_val_if_fail (finish_uint == NULL, NULL);
		}

		if (finish_boolean) {
			g_return_val_if_fail (finish_void == NULL, NULL);
			g_return_val_if_fail (finish_string == NULL, NULL);
			g_return_val_if_fail (finish_strv == NULL, NULL);
			g_return_val_if_fail (finish_uint == NULL, NULL);
		}

		if (finish_string) {
			g_return_val_if_fail (finish_void == NULL, NULL);
			g_return_val_if_fail (finish_boolean == NULL, NULL);
			g_return_val_if_fail (finish_strv == NULL, NULL);
			g_return_val_if_fail (finish_uint == NULL, NULL);
		}

		if (finish_strv) {
			g_return_val_if_fail (finish_void == NULL, NULL);
			g_return_val_if_fail (finish_boolean == NULL, NULL);
			g_return_val_if_fail (finish_string == NULL, NULL);
			g_return_val_if_fail (finish_uint == NULL, NULL);
		}

		if (finish_uint) {
			g_return_val_if_fail (finish_void == NULL, NULL);
			g_return_val_if_fail (finish_boolean == NULL, NULL);
			g_return_val_if_fail (finish_string == NULL, NULL);
			g_return_val_if_fail (finish_strv == NULL, NULL);
		}

		*proxy = e_client_get_dbus_proxy (client);
		if (!*proxy)
			return NULL;
	}

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	opid = e_client_register_op (client, use_cancellable);
	async_data = g_new0 (EClientAsyncOpData, 1);
	async_data->client = g_object_ref (client);
	async_data->opid = opid;
	async_data->source_tag = source_tag;
	async_data->callback = callback;
	async_data->user_data = user_data;
	async_data->finish_void = finish_void;
	async_data->finish_boolean = finish_boolean;
	async_data->finish_string = finish_string;
	async_data->finish_strv = finish_strv;
	async_data->finish_uint = finish_uint;

	/* EClient from e_client_register_op() took ownership of the use_cancellable */
	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);

	if (out_cancellable)
		*out_cancellable = use_cancellable;

	return async_data;
}

void
e_client_proxy_return_async_error (EClient *client,
                                   const GError *error,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data,
                                   gpointer source_tag)
{
	EClientAsyncOpData *async_data;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (error != NULL);
	g_return_if_fail (callback != NULL);

	async_data = prepare_async_data (client, NULL, callback, user_data, source_tag, TRUE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	g_return_if_fail (async_data != NULL);

	finish_async_op (async_data, error, TRUE);
}

void
e_client_proxy_call_void (EClient *client,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data,
                          gpointer source_tag,
                          void (*func) (GDBusProxy *proxy,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data),
                          EClientProxyFinishVoidFunc finish_void,
                          EClientProxyFinishBooleanFunc finish_boolean,
                          EClientProxyFinishStringFunc finish_string,
                          EClientProxyFinishStrvFunc finish_strv,
                          EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	func (proxy, cancellable, async_result_ready_cb, async_data);
}

void
e_client_proxy_call_boolean (EClient *client,
                             gboolean in_boolean,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data,
                             gpointer source_tag,
                             void (*func) (GDBusProxy *proxy,
                                           gboolean in_boolean,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data),
                             EClientProxyFinishVoidFunc finish_void,
                             EClientProxyFinishBooleanFunc finish_boolean,
                             EClientProxyFinishStringFunc finish_string,
                             EClientProxyFinishStrvFunc finish_strv,
                             EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	func (proxy, in_boolean, cancellable, async_result_ready_cb, async_data);
}

void
e_client_proxy_call_string (EClient *client,
                            const gchar *in_string,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data,
                            gpointer source_tag,
                            void (*func) (GDBusProxy *proxy,
                                          const gchar *in_string,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data),
                            EClientProxyFinishVoidFunc finish_void,
                            EClientProxyFinishBooleanFunc finish_boolean,
                            EClientProxyFinishStringFunc finish_string,
                            EClientProxyFinishStrvFunc finish_strv,
                            EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);
	e_client_return_async_if_fail (in_string != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	func (proxy, in_string, cancellable, async_result_ready_cb, async_data);
}

void
e_client_proxy_call_string_with_res_op_data (EClient *client,
                                             const gchar *in_string,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data,
                                             gpointer source_tag,
                                             const gchar *res_op_data,
                                             void (*func) (GDBusProxy *proxy,
                                                           const gchar *in_string,
                                                           GCancellable *cancellable,
                                                           GAsyncReadyCallback callback,
                                                           gpointer user_data),
                                             EClientProxyFinishVoidFunc finish_void,
                                             EClientProxyFinishBooleanFunc finish_boolean,
                                             EClientProxyFinishStringFunc finish_string,
                                             EClientProxyFinishStrvFunc finish_strv,
                                             EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);
	e_client_return_async_if_fail (in_string != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	async_data->res_op_data = g_strdup (res_op_data);

	func (proxy, in_string, cancellable, async_result_ready_cb, async_data);
}

void
e_client_proxy_call_strv (EClient *client,
                          const gchar * const *in_strv,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data,
                          gpointer source_tag,
                          void (*func) (GDBusProxy *proxy,
                                        const gchar * const * in_strv,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data),
                          EClientProxyFinishVoidFunc finish_void,
                          EClientProxyFinishBooleanFunc finish_boolean,
                          EClientProxyFinishStringFunc finish_string,
                          EClientProxyFinishStrvFunc finish_strv,
                          EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);
	e_client_return_async_if_fail (in_strv != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	func (proxy, in_strv, cancellable, async_result_ready_cb, async_data);
}

void
e_client_proxy_call_uint (EClient *client,
                          guint in_uint,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data,
                          gpointer source_tag,
                          void (*func) (GDBusProxy *proxy,
                                        guint in_uint,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data),
                          EClientProxyFinishVoidFunc finish_void,
                          EClientProxyFinishBooleanFunc finish_boolean,
                          EClientProxyFinishStringFunc finish_string,
                          EClientProxyFinishStrvFunc finish_strv,
                          EClientProxyFinishUintFunc finish_uint)
{
	EClientAsyncOpData *async_data;
	GDBusProxy *proxy = NULL;

	g_return_if_fail (E_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (source_tag != NULL);
	e_client_return_async_if_fail (func != NULL, client, callback, user_data, source_tag);

	async_data = prepare_async_data (client, cancellable, callback, user_data, source_tag, FALSE, finish_void, finish_boolean, finish_string, finish_strv, finish_uint, &proxy, &cancellable);
	e_client_return_async_if_fail (async_data != NULL, client, callback, user_data, source_tag);

	func (proxy, in_uint, cancellable, async_result_ready_cb, async_data);
}

gboolean
e_client_proxy_call_finish_void (EClient *client,
                                 GAsyncResult *result,
                                 GError **error,
                                 gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_boolean (EClient *client,
                                    GAsyncResult *result,
                                    gboolean *out_boolean,
                                    GError **error,
                                    gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_boolean != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_boolean = async_data->out.val_boolean;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_string (EClient *client,
                                   GAsyncResult *result,
                                   gchar **out_string,
                                   GError **error,
                                   gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_string != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_string = async_data->out.val_string;
	async_data->out.val_string = NULL;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_strv (EClient *client,
                                 GAsyncResult *result,
                                 gchar ***out_strv,
                                 GError **error,
                                 gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_strv != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_strv = async_data->out.val_strv;
	async_data->out.val_strv = NULL;

	return async_data->result;
}

gboolean
e_client_proxy_call_finish_uint (EClient *client,
                                 GAsyncResult *result,
                                 guint *out_uint,
                                 GError **error,
                                 gpointer source_tag)
{
	GSimpleAsyncResult *simple;
	GError *local_error = NULL;
	EClientAsyncOpData *async_data;

	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (result != NULL, FALSE);
	g_return_val_if_fail (source_tag != NULL, FALSE);
	g_return_val_if_fail (out_uint != NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (client), source_tag), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, &local_error)) {
		e_client_unwrap_dbus_error (client, local_error, error);
		return FALSE;
	}

	async_data = g_simple_async_result_get_op_res_gpointer (simple);
	g_return_val_if_fail (async_data != NULL, FALSE);

	*out_uint = async_data->out.val_uint;

	return async_data->result;
}

#define SYNC_CALL_TEMPLATE(_out_test,_the_call)			\
	GDBusProxy *proxy;					\
	GCancellable *use_cancellable;				\
	guint32 opid;						\
	gboolean result;					\
	GError *local_error = NULL;				\
								\
	g_return_val_if_fail (E_IS_CLIENT (client), FALSE);	\
	g_return_val_if_fail (func != NULL, FALSE);		\
	g_return_val_if_fail (_out_test != NULL, FALSE);	\
								\
	proxy = e_client_get_dbus_proxy (client);		\
	g_return_val_if_fail (proxy != NULL, FALSE);		\
								\
	use_cancellable = cancellable;				\
	if (!use_cancellable)					\
		use_cancellable = g_cancellable_new ();		\
								\
	g_object_ref (client);					\
	opid = e_client_register_op (client, use_cancellable);	\
								\
	result = func _the_call;				\
								\
	e_client_unregister_op (client, opid);			\
	g_object_unref (client);				\
								\
	if (use_cancellable != cancellable)			\
		g_object_unref (use_cancellable);		\
								\
	e_client_unwrap_dbus_error (client, local_error, error);\
								\
	return result;

gboolean
e_client_proxy_call_sync_void__void (EClient *client,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__boolean (EClient *client,
                                        gboolean *out_boolean,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          gboolean *out_boolean,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__string (EClient *client,
                                       gchar **out_string,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         gchar **out_string,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__strv (EClient *client,
                                     gchar ***out_strv,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       gchar ***out_strv,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_void__uint (EClient *client,
                                     guint *out_uint,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       guint *out_uint,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__void (EClient *client,
                                        gboolean in_boolean,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          gboolean in_boolean,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__boolean (EClient *client,
                                           gboolean in_boolean,
                                           gboolean *out_boolean,
                                           GCancellable *cancellable,
                                           GError **error,
                                           gboolean (*func) (GDBusProxy *proxy,
                                                             gboolean in_boolean,
                                                             gboolean *out_boolean,
                                                             GCancellable *cancellable,
                                                             GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_boolean, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__string (EClient *client,
                                          gboolean in_boolean,
                                          gchar **out_string,
                                          GCancellable *cancellable,
                                          GError **error,
                                          gboolean (*func) (GDBusProxy *proxy,
                                                            gboolean in_boolean,
                                                            gchar **out_string,
                                                            GCancellable *cancellable,
                                                            GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_boolean, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__strv (EClient *client,
                                        gboolean in_boolean,
                                        gchar ***out_strv,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          gboolean in_boolean,
                                                          gchar ***out_strv,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_boolean, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_boolean__uint (EClient *client,
                                        gboolean in_boolean,
                                        guint *out_uint,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          gboolean in_boolean,
                                                          guint *out_uint,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_boolean, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__void (EClient *client,
                                       const gchar *in_string,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         const gchar *in_string,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__boolean (EClient *client,
                                          const gchar *in_string,
                                          gboolean *out_boolean,
                                          GCancellable *cancellable,
                                          GError **error,
                                          gboolean (*func) (GDBusProxy *proxy,
                                                            const gchar *in_string,
                                                            gboolean *out_boolean,
                                                            GCancellable *cancellable,
                                                            GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_string, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__string (EClient *client,
                                         const gchar *in_string,
                                         gchar **out_string,
                                         GCancellable *cancellable,
                                         GError **error,
                                         gboolean (*func) (GDBusProxy *proxy,
                                                           const gchar *in_string,
                                                           gchar **out_string,
                                                           GCancellable *cancellable,
                                                           GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_string, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__strv (EClient *client,
                                       const gchar *in_string,
                                       gchar ***out_strv,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         const gchar *in_string,
                                                         gchar ***out_strv,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_string, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_string__uint (EClient *client,
                                       const gchar *in_string,
                                       guint *out_uint,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         const gchar *in_string,
                                                         guint *out_uint,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_string, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__void (EClient *client,
                                     const gchar * const *in_strv,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       const gchar * const *in_strv,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__boolean (EClient *client,
                                        const gchar * const *in_strv,
                                        gboolean *out_boolean,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          const gchar * const *in_strv,
                                                          gboolean *out_boolean,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_strv, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__string (EClient *client,
                                       const gchar * const *in_strv,
                                       gchar **out_string,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         const gchar * const *in_strv,
                                                         gchar **out_string,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_strv, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__strv (EClient *client,
                                     const gchar * const *in_strv,
                                     gchar ***out_strv,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       const gchar * const *in_strv,
                                                       gchar ***out_strv,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_strv, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_strv__uint (EClient *client,
                                     const gchar * const *in_strv,
                                     guint *out_uint,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       const gchar * const *in_strv,
                                                       guint *out_uint,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_strv, out_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__void (EClient *client,
                                     guint in_uint,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       guint in_uint,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (client, (proxy, in_uint, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__boolean (EClient *client,
                                        guint in_uint,
                                        gboolean *out_boolean,
                                        GCancellable *cancellable,
                                        GError **error,
                                        gboolean (*func) (GDBusProxy *proxy,
                                                          guint in_uint,
                                                          gboolean *out_boolean,
                                                          GCancellable *cancellable,
                                                          GError **error))
{
	SYNC_CALL_TEMPLATE (out_boolean, (proxy, in_uint, out_boolean, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__string (EClient *client,
                                       guint in_uint,
                                       gchar **out_string,
                                       GCancellable *cancellable,
                                       GError **error,
                                       gboolean (*func) (GDBusProxy *proxy,
                                                         guint in_uint,
                                                         gchar **out_string,
                                                         GCancellable *cancellable,
                                                         GError **error))
{
	SYNC_CALL_TEMPLATE (out_string, (proxy, in_uint, out_string, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__strv (EClient *client,
                                     guint in_uint,
                                     gchar ***out_strv,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       guint in_uint,
                                                       gchar ***out_strv,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_strv, (proxy, in_uint, out_strv, use_cancellable, &local_error))
}

gboolean
e_client_proxy_call_sync_uint__uint (EClient *client,
                                     guint in_uint,
                                     guint *out_uint,
                                     GCancellable *cancellable,
                                     GError **error,
                                     gboolean (*func) (GDBusProxy *proxy,
                                                       guint in_uint,
                                                       guint *out_uint,
                                                       GCancellable *cancellable,
                                                       GError **error))
{
	SYNC_CALL_TEMPLATE (out_uint, (proxy, in_uint, out_uint, use_cancellable, &local_error))
}

#undef SYNC_CALL_TEMPLATE

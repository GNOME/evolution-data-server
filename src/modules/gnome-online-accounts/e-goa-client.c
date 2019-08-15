/*
 * e-goa-client.c
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

#include <libedataserver/libedataserver.h>

#include "e-goa-client.h"

struct _EGoaClientPrivate {
	GDBusObjectManager *object_manager;
	gulong object_added_handler_id;
	gulong object_removed_handler_id;
	gulong notify_name_owner_handler_id;

	/* ID -> GoaObject */
	GHashTable *orphans;
	GMutex orphans_lock;
};

enum {
	PROP_0,
	PROP_OBJECT_MANAGER
};

enum {
	ACCOUNT_ADDED,
	ACCOUNT_REMOVED,
	ACCOUNT_SWAPPED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* Forward Declarations */
static void	e_goa_client_interface_init	(GInitableIface *iface);

/* By default, the GAsyncInitable interface calls GInitable.init()
 * from a separate thread, so we only have to override GInitable. */
G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EGoaClient,
	e_goa_client,
	G_TYPE_OBJECT,
	0,
	G_ADD_PRIVATE_DYNAMIC (EGoaClient)
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		G_TYPE_INITABLE,
		e_goa_client_interface_init)
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		G_TYPE_ASYNC_INITABLE,
		NULL))

static void
e_goa_client_stash_orphan (EGoaClient *client,
                           GoaObject *goa_object)
{
	GoaAccount *goa_account;
	const gchar *goa_account_id;

	goa_account = goa_object_peek_account (goa_object);
	g_return_if_fail (goa_account != NULL);

	goa_account_id = goa_account_get_id (goa_account);
	g_return_if_fail (goa_account_id != NULL);

	e_source_registry_debug_print ("GOA: Stashing orphaned account '%s'\n", goa_account_id);

	g_mutex_lock (&client->priv->orphans_lock);

	g_hash_table_replace (
		client->priv->orphans,
		g_strdup (goa_account_id),
		g_object_ref (goa_object));

	g_mutex_unlock (&client->priv->orphans_lock);
}

static GoaObject *
e_goa_client_claim_one_orphan (EGoaClient *client,
                               GoaObject *new_goa_object)
{
	GHashTable *orphans;
	GoaAccount *goa_account;
	GoaObject *old_goa_object;
	const gchar *goa_account_id;

	orphans = client->priv->orphans;

	goa_account = goa_object_peek_account (new_goa_object);
	g_return_val_if_fail (goa_account != NULL, NULL);

	goa_account_id = goa_account_get_id (goa_account);
	g_return_val_if_fail (goa_account_id != NULL, NULL);

	g_mutex_lock (&client->priv->orphans_lock);

	old_goa_object = g_hash_table_lookup (orphans, goa_account_id);

	if (old_goa_object != NULL) {
		g_object_ref (old_goa_object);
		g_hash_table_remove (orphans, goa_account_id);
	}

	g_mutex_unlock (&client->priv->orphans_lock);

	if (old_goa_object != NULL)
		e_source_registry_debug_print (
			"GOA: Claiming orphaned account '%s'\n",
			goa_account_id);

	return old_goa_object;
}

static GList *
e_goa_client_claim_all_orphans (EGoaClient *client)
{
	GList *list;

	g_mutex_lock (&client->priv->orphans_lock);

	list = g_hash_table_get_values (client->priv->orphans);
	g_list_foreach (list, (GFunc) g_object_ref, NULL);
	g_hash_table_remove_all (client->priv->orphans);

	g_mutex_unlock (&client->priv->orphans_lock);

	if (list != NULL)
		e_source_registry_debug_print ("GOA: Claiming orphaned account(s)\n");

	return list;
}

static void
e_goa_client_object_added_cb (GDBusObjectManager *manager,
                              GDBusObject *object,
                              EGoaClient *client)
{
	GoaObject *new_goa_object;
	GoaObject *old_goa_object;

	new_goa_object = GOA_OBJECT (object);

	/* Only interested in objects with GoaAccount interfaces. */
	if (goa_object_peek_account (new_goa_object) == NULL)
		return;

	old_goa_object =
		e_goa_client_claim_one_orphan (client, new_goa_object);

	if (old_goa_object != NULL) {
		g_signal_emit (
			client,
			signals[ACCOUNT_SWAPPED], 0,
			old_goa_object,
			new_goa_object);
	} else {
		g_signal_emit (
			client,
			signals[ACCOUNT_ADDED], 0,
			new_goa_object);
	}

	g_clear_object (&old_goa_object);
}

static void
e_goa_client_object_removed_cb (GDBusObjectManager *manager,
                                GDBusObject *object,
                                EGoaClient *client)
{
	GoaObject *goa_object;
	gchar *name_owner;

	goa_object = GOA_OBJECT (object);

	/* Only interested in objects with GoaAccount interfaces. */
	if (goa_object_peek_account (goa_object) == NULL)
		return;

	name_owner = g_dbus_object_manager_client_get_name_owner (
		G_DBUS_OBJECT_MANAGER_CLIENT (manager));

	if (name_owner != NULL) {
		g_signal_emit (
			client,
			signals[ACCOUNT_REMOVED], 0,
			goa_object);
	} else {
		/* The goa-daemon went bye-bye. */
		e_goa_client_stash_orphan (client, goa_object);
	}

	g_free (name_owner);
}

static void
e_goa_client_notify_name_owner_cb (GDBusObjectManager *manager,
                                   GParamSpec *pspec,
                                   EGoaClient *client)
{
	gchar *name_owner;

	name_owner = g_dbus_object_manager_client_get_name_owner (
		G_DBUS_OBJECT_MANAGER_CLIENT (manager));

	if (name_owner != NULL)
		e_source_registry_debug_print ("GOA: 'org.gnome.OnlineAccounts' name appeared\n");
	else
		e_source_registry_debug_print ("GOA: 'org.gnome.OnlineAccounts' name vanished\n");

	if (name_owner != NULL) {
		GList *list, *link;

		/* The goa-daemon (re)started.  Any unclaimed accounts
		 * from the previous session were legitimately removed. */

		list = e_goa_client_claim_all_orphans (client);

		for (link = list; link != NULL; link = g_list_next (link)) {
			g_signal_emit (
				client,
				signals[ACCOUNT_REMOVED], 0,
				GOA_OBJECT (link->data));
		}

		g_list_free_full (list, (GDestroyNotify) g_object_unref);

		g_free (name_owner);
	}
}

static void
e_goa_client_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_OBJECT_MANAGER:
			g_value_take_object (
				value,
				e_goa_client_ref_object_manager (
				E_GOA_CLIENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
e_goa_client_dispose (GObject *object)
{
	EGoaClientPrivate *priv;

	priv = E_GOA_CLIENT (object)->priv;

	if (priv->object_added_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->object_manager,
			priv->object_added_handler_id);
		priv->object_added_handler_id = 0;
	}

	if (priv->object_removed_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->object_manager,
			priv->object_removed_handler_id);
		priv->object_removed_handler_id = 0;
	}

	if (priv->notify_name_owner_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->object_manager,
			priv->notify_name_owner_handler_id);
		priv->notify_name_owner_handler_id = 0;
	}

	g_clear_object (&priv->object_manager);

	g_hash_table_remove_all (priv->orphans);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_goa_client_parent_class)->dispose (object);
}

static void
e_goa_client_finalize (GObject *object)
{
	EGoaClientPrivate *priv;

	priv = E_GOA_CLIENT (object)->priv;

	g_hash_table_destroy (priv->orphans);
	g_mutex_clear (&priv->orphans_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_goa_client_parent_class)->finalize (object);
}

static gboolean
e_goa_client_initable_init (GInitable *initable,
                            GCancellable *cancellable,
                            GError **error)
{
	EGoaClientPrivate *priv;
	gulong handler_id;

	priv = E_GOA_CLIENT (initable)->priv;

	priv->object_manager = goa_object_manager_client_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
		"org.gnome.OnlineAccounts",
		"/org/gnome/OnlineAccounts",
		cancellable, error);

	if (priv->object_manager == NULL)
		return FALSE;

	handler_id = g_signal_connect (
		priv->object_manager, "object-added",
		G_CALLBACK (e_goa_client_object_added_cb),
		E_GOA_CLIENT (initable));
	priv->object_added_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->object_manager, "object-removed",
		G_CALLBACK (e_goa_client_object_removed_cb),
		E_GOA_CLIENT (initable));
	priv->object_removed_handler_id = handler_id;

	handler_id = g_signal_connect (
		priv->object_manager, "notify::name-owner",
		G_CALLBACK (e_goa_client_notify_name_owner_cb),
		E_GOA_CLIENT (initable));
	priv->notify_name_owner_handler_id = handler_id;

	return TRUE;
}

static void
e_goa_client_class_init (EGoaClientClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = e_goa_client_get_property;
	object_class->dispose = e_goa_client_dispose;
	object_class->finalize = e_goa_client_finalize;

	g_object_class_install_property (
		object_class,
		PROP_OBJECT_MANAGER,
		g_param_spec_object (
			"object-manager",
			"Object Manager",
			"The GDBusObjectManager used by the EGoaClient",
			G_TYPE_DBUS_OBJECT_MANAGER,
			G_PARAM_READABLE));

	signals[ACCOUNT_ADDED] = g_signal_new (
		"account-added",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EGoaClientClass, account_added),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		GOA_TYPE_OBJECT);

	signals[ACCOUNT_REMOVED] = g_signal_new (
		"account-removed",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EGoaClientClass, account_removed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		GOA_TYPE_OBJECT);

	signals[ACCOUNT_SWAPPED] = g_signal_new (
		"account-swapped",
		G_TYPE_FROM_CLASS (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (EGoaClientClass, account_swapped),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2,
		GOA_TYPE_OBJECT,
		GOA_TYPE_OBJECT);
}

static void
e_goa_client_class_finalize (EGoaClientClass *class)
{
}

static void
e_goa_client_interface_init (GInitableIface *iface)
{
	iface->init = e_goa_client_initable_init;
}

static void
e_goa_client_init (EGoaClient *client)
{
	client->priv = e_goa_client_get_instance_private (client);

	client->priv->orphans = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);
	g_mutex_init (&client->priv->orphans_lock);
}

void
e_goa_client_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_goa_client_register_type (type_module);
}

void
e_goa_client_new (GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
	g_async_initable_new_async (
		E_TYPE_GOA_CLIENT,
		G_PRIORITY_DEFAULT, cancellable,
		callback, user_data, NULL);
}

EGoaClient *
e_goa_client_new_finish (GAsyncResult *result,
                         GError **error)
{
	GObject *source_object;
	GObject *result_object;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	source_object = g_async_result_get_source_object (result);
	g_return_val_if_fail (source_object != NULL, NULL);

	result_object = g_async_initable_new_finish (
		G_ASYNC_INITABLE (source_object), result, error);

	g_object_unref (source_object);

	if (result_object == NULL)
		return NULL;

	return E_GOA_CLIENT (result_object);
}

GDBusObjectManager *
e_goa_client_ref_object_manager (EGoaClient *client)
{
	g_return_val_if_fail (E_IS_GOA_CLIENT (client), NULL);

	return g_object_ref (client->priv->object_manager);
}

GList *
e_goa_client_list_accounts (EGoaClient *client)
{
	GDBusObjectManager *object_manager;
	GQueue queue = G_QUEUE_INIT;
	GList *list, *link;

	g_return_val_if_fail (E_IS_GOA_CLIENT (client), NULL);

	object_manager = e_goa_client_ref_object_manager (client);
	list = g_dbus_object_manager_get_objects (object_manager);

	for (link = list; link != NULL; link = g_list_next (link)) {
		GoaObject *goa_object = GOA_OBJECT (link->data);

		if (goa_object_peek_account (goa_object) != NULL)
			g_queue_push_tail (&queue, g_object_ref (goa_object));
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	g_object_unref (object_manager);

	return g_queue_peek_head_link (&queue);
}

GoaObject *
e_goa_client_lookup_by_id (EGoaClient *client,
                           const gchar *id)
{
	GList *list, *link;
	GoaObject *match = NULL;

	g_return_val_if_fail (E_IS_GOA_CLIENT (client), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	list = e_goa_client_list_accounts (client);

	for (link = list; link != NULL; link = g_list_next (link)) {
		GoaObject *goa_object = GOA_OBJECT (link->data);
		GoaAccount *goa_account;
		const gchar *goa_account_id;

		goa_account = goa_object_peek_account (goa_object);
		if (goa_account == NULL)
			continue;

		goa_account_id = goa_account_get_id (goa_account);
		if (g_strcmp0 (goa_account_id, id) == 0) {
			match = g_object_ref (goa_object);
			break;
		}
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return match;
}


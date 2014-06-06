/*
 * e-source-registry.c
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
 * SECTION: e-source-registry
 * @include: libedataserver/libedataserver.h
 * @short_description: A central repository for data sources
 *
 * The #ESourceRegistry is a global singleton store for all #ESource
 * instances.  It uses file monitors to react to key file creation and
 * deletion events, either constructing an #ESource instance from the
 * newly created key file, or removing from the logical #ESource
 * hierarchy the instance corresponding to the deleted key file.
 *
 * The #ESourceRegistry can be queried for individual #ESource instances
 * by their unique identifier string or key file path, for collections of
 * #ESource instances having a particular extension, or for all available
 * #ESource instances.
 *
 * The #ESourceRegistry API also provides a front-end for the
 * "org.gnome.Evolution.DefaultSources" #GSettings schema which tracks
 * which #ESource instances are designated to be the user's default address
 * book, calendar, memo list and task list for desktop integration.
 **/

#include "e-source-registry.h"

#include <config.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

/* XXX Yeah, yeah... */
#define GCR_API_SUBJECT_TO_CHANGE

#include <gcr/gcr-base.h>

/* Private D-Bus classes. */
#include <e-dbus-source.h>
#include <e-dbus-source-manager.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-source-collection.h>

/* Needed for the defaults API. */
#include <libedataserver/e-source-address-book.h>
#include <libedataserver/e-source-calendar.h>
#include <libedataserver/e-source-mail-account.h>
#include <libedataserver/e-source-mail-identity.h>

#include "e-dbus-authenticator.h"

#define E_SOURCE_REGISTRY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_REGISTRY, ESourceRegistryPrivate))

#define DBUS_OBJECT_PATH "/org/gnome/evolution/dataserver/SourceManager"
#define GSETTINGS_SCHEMA "org.gnome.Evolution.DefaultSources"

/* Built-in data source UIDs. */
#define E_SOURCE_BUILTIN_ADDRESS_BOOK_UID	"system-address-book"
#define E_SOURCE_BUILTIN_CALENDAR_UID		"system-calendar"
#define E_SOURCE_BUILTIN_MAIL_ACCOUNT_UID 	"local"
#define E_SOURCE_BUILTIN_MEMO_LIST_UID		"system-memo-list"
#define E_SOURCE_BUILTIN_PROXY_UID		"system-proxy"
#define E_SOURCE_BUILTIN_TASK_LIST_UID		"system-task-list"

/* GSettings keys for default data sources. */
#define E_SETTINGS_DEFAULT_ADDRESS_BOOK_KEY	"default-address-book"
#define E_SETTINGS_DEFAULT_CALENDAR_KEY		"default-calendar"
#define E_SETTINGS_DEFAULT_MAIL_ACCOUNT_KEY	"default-mail-account"
#define E_SETTINGS_DEFAULT_MAIL_IDENTITY_KEY	"default-mail-identity"
#define E_SETTINGS_DEFAULT_MEMO_LIST_KEY	"default-memo-list"
#define E_SETTINGS_DEFAULT_TASK_LIST_KEY	"default-task-list"

typedef struct _AsyncContext AsyncContext;
typedef struct _AuthContext AuthContext;
typedef struct _CreateContext CreateContext;
typedef struct _SourceClosure SourceClosure;
typedef struct _ThreadClosure ThreadClosure;

struct _ESourceRegistryPrivate {
	GMainContext *main_context;

	GThread *manager_thread;
	ThreadClosure *thread_closure;

	GDBusObjectManager *dbus_object_manager;
	EDBusSourceManager *dbus_source_manager;

	GHashTable *object_path_table;
	GMutex object_path_table_lock;

	GHashTable *service_restart_table;
	GMutex service_restart_table_lock;

	GHashTable *sources;
	GMutex sources_lock;

	GSettings *settings;

	gboolean initialized;
	GError *init_error;
	GMutex init_lock;
};

struct _AsyncContext {
	ESource *source;
	GList *list_of_sources;
	ESourceAuthenticator *auth;
};

/* Used in e_source_registry_authenticate_sync() */
struct _AuthContext {
	ESourceAuthenticator *auth;
	EDBusAuthenticator *dbus_auth;
	GCancellable *cancellable;
	GMainLoop *main_loop;
	ESourceAuthenticationResult auth_result;
	GcrSecretExchange *secret_exchange;
	gboolean authenticating;
	GError **error;
};

/* Used in e_source_registry_create_sources_sync() */
struct _CreateContext {
	GHashTable *pending_uids;
	GMainContext *main_context;
	GMainLoop *main_loop;
};

struct _SourceClosure {
	GWeakRef registry;
	ESource *source;
};

struct _ThreadClosure {
	ESourceRegistry *registry;
	GMainContext *main_context;
	GMainLoop *main_loop;
	GCond main_loop_cond;
	GMutex main_loop_mutex;
	GError *error;
};

enum {
	PROP_0,
	PROP_DEFAULT_ADDRESS_BOOK,
	PROP_DEFAULT_CALENDAR,
	PROP_DEFAULT_MAIL_ACCOUNT,
	PROP_DEFAULT_MAIL_IDENTITY,
	PROP_DEFAULT_MEMO_LIST,
	PROP_DEFAULT_TASK_LIST
};

enum {
	SOURCE_ADDED,
	SOURCE_CHANGED,
	SOURCE_REMOVED,
	SOURCE_ENABLED,
	SOURCE_DISABLED,
	LAST_SIGNAL
};

/* Forward Declarations */
static void	source_registry_add_source	(ESourceRegistry *registry,
						 ESource *source);
static void	e_source_registry_initable_init	(GInitableIface *iface);

/* Private ESource function, for our use only. */
void		__e_source_private_replace_dbus_object
						(ESource *source,
						 GDBusObject *dbus_object);

static guint signals[LAST_SIGNAL];

/* By default, the GAsyncInitable interface calls GInitable.init()
 * from a separate thread, so we only have to override GInitable. */
G_DEFINE_TYPE_WITH_CODE (
	ESourceRegistry,
	e_source_registry,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE, e_source_registry_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_ASYNC_INITABLE, NULL))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->source != NULL)
		g_object_unref (async_context->source);

	g_list_free_full (
		async_context->list_of_sources,
		(GDestroyNotify) g_object_unref);

	if (async_context->auth != NULL)
		g_object_unref (async_context->auth);

	g_slice_free (AsyncContext, async_context);
}

static void
auth_context_free (AuthContext *auth_context)
{
	if (auth_context->auth != NULL)
		g_object_unref (auth_context->auth);

	if (auth_context->dbus_auth != NULL)
		g_object_unref (auth_context->dbus_auth);

	if (auth_context->cancellable != NULL)
		g_object_unref (auth_context->cancellable);

	if (auth_context->main_loop != NULL)
		g_main_loop_unref (auth_context->main_loop);

	if (auth_context->secret_exchange != NULL)
		g_object_unref (auth_context->secret_exchange);

	g_slice_free (AuthContext, auth_context);
}

static CreateContext *
create_context_new (void)
{
	CreateContext *create_context;

	create_context = g_slice_new0 (CreateContext);

	create_context->pending_uids = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	create_context->main_context = g_main_context_new ();

	create_context->main_loop = g_main_loop_new (
		create_context->main_context, FALSE);

	return create_context;
}

static void
create_context_free (CreateContext *create_context)
{
	g_main_loop_unref (create_context->main_loop);
	g_main_context_unref (create_context->main_context);
	g_hash_table_unref (create_context->pending_uids);

	g_slice_free (CreateContext, create_context);
}

static void
source_closure_free (SourceClosure *closure)
{
	g_weak_ref_clear (&closure->registry);
	g_object_unref (closure->source);

	g_slice_free (SourceClosure, closure);
}

static void
thread_closure_free (ThreadClosure *closure)
{
	/* The registry member is not referenced. */

	g_main_context_unref (closure->main_context);
	g_main_loop_unref (closure->main_loop);
	g_cond_clear (&closure->main_loop_cond);
	g_mutex_clear (&closure->main_loop_mutex);

	/* The GError should be NULL at this point,
	 * regardless of whether an error occurred. */
	g_warn_if_fail (closure->error == NULL);

	g_slice_free (ThreadClosure, closure);
}

G_LOCK_DEFINE_STATIC (singleton_lock);
static GWeakRef singleton;

static ESourceRegistry *
source_registry_dup_uninitialized_singleton (void)
{
	ESourceRegistry *registry;

	G_LOCK (singleton_lock);

	registry = g_weak_ref_get (&singleton);
	if (registry == NULL) {
		registry = g_object_new (E_TYPE_SOURCE_REGISTRY, NULL);
		g_weak_ref_set (&singleton, registry);
	}

	G_UNLOCK (singleton_lock);

	return registry;
}

static gchar *
source_registry_dbus_object_dup_uid (GDBusObject *dbus_object)
{
	EDBusObject *e_dbus_object;
	EDBusSource *e_dbus_source;

	/* EDBusSource interface should always be present. */
	e_dbus_object = E_DBUS_OBJECT (dbus_object);
	e_dbus_source = e_dbus_object_peek_source (e_dbus_object);

	return e_dbus_source_dup_uid (e_dbus_source);
}

static void
source_registry_object_path_table_insert (ESourceRegistry *registry,
                                          const gchar *object_path,
                                          ESource *source)
{
	GHashTable *object_path_table;

	g_return_if_fail (object_path != NULL);
	g_return_if_fail (E_IS_SOURCE (source));

	object_path_table = registry->priv->object_path_table;

	g_mutex_lock (&registry->priv->object_path_table_lock);

	g_hash_table_insert (
		object_path_table,
		g_strdup (object_path),
		g_object_ref (source));

	g_mutex_unlock (&registry->priv->object_path_table_lock);
}

static ESource *
source_registry_object_path_table_lookup (ESourceRegistry *registry,
                                          const gchar *object_path)
{
	GHashTable *object_path_table;
	ESource *source;

	g_return_val_if_fail (object_path != NULL, NULL);

	object_path_table = registry->priv->object_path_table;

	g_mutex_lock (&registry->priv->object_path_table_lock);

	source = g_hash_table_lookup (object_path_table, object_path);
	if (source != NULL)
		g_object_ref (source);

	g_mutex_unlock (&registry->priv->object_path_table_lock);

	return source;
}

static gboolean
source_registry_object_path_table_remove (ESourceRegistry *registry,
                                          const gchar *object_path)
{
	GHashTable *object_path_table;
	gboolean removed;

	g_return_val_if_fail (object_path != NULL, FALSE);

	object_path_table = registry->priv->object_path_table;

	g_mutex_lock (&registry->priv->object_path_table_lock);

	removed = g_hash_table_remove (object_path_table, object_path);

	g_mutex_unlock (&registry->priv->object_path_table_lock);

	return removed;
}

static void
source_registry_service_restart_table_add (ESourceRegistry *registry,
                                           const gchar *uid)
{
	GHashTable *service_restart_table;

	g_return_if_fail (uid != NULL);

	service_restart_table = registry->priv->service_restart_table;

	g_mutex_lock (&registry->priv->service_restart_table_lock);

	g_hash_table_add (service_restart_table, g_strdup (uid));

	g_mutex_unlock (&registry->priv->service_restart_table_lock);
}

static gboolean
source_registry_service_restart_table_remove (ESourceRegistry *registry,
                                              const gchar *uid)
{
	GHashTable *service_restart_table;
	gboolean removed;

	g_return_val_if_fail (uid != NULL, FALSE);

	service_restart_table = registry->priv->service_restart_table;

	g_mutex_lock (&registry->priv->service_restart_table_lock);

	removed = g_hash_table_remove (service_restart_table, uid);

	g_mutex_unlock (&registry->priv->service_restart_table_lock);

	return removed;
}

static GList *
source_registry_service_restart_table_steal_all (ESourceRegistry *registry)
{
	GHashTable *service_restart_table;
	GList *list;

	service_restart_table = registry->priv->service_restart_table;

	g_mutex_lock (&registry->priv->service_restart_table_lock);

	list = g_hash_table_get_keys (service_restart_table);
	g_hash_table_steal_all (service_restart_table);

	g_mutex_unlock (&registry->priv->service_restart_table_lock);

	return list;
}

static void
source_registry_sources_insert (ESourceRegistry *registry,
                                ESource *source)
{
	const gchar *uid;

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	g_mutex_lock (&registry->priv->sources_lock);

	g_hash_table_insert (
		registry->priv->sources,
		g_strdup (uid), g_object_ref (source));

	g_mutex_unlock (&registry->priv->sources_lock);
}

static gboolean
source_registry_sources_remove (ESourceRegistry *registry,
                                ESource *source)
{
	const gchar *uid;
	gboolean removed;

	uid = e_source_get_uid (source);
	g_return_val_if_fail (uid != NULL, FALSE);

	g_mutex_lock (&registry->priv->sources_lock);

	removed = g_hash_table_remove (registry->priv->sources, uid);

	g_mutex_unlock (&registry->priv->sources_lock);

	return removed;
}

static ESource *
source_registry_sources_lookup (ESourceRegistry *registry,
                                const gchar *uid)
{
	ESource *source;

	g_return_val_if_fail (uid != NULL, NULL);

	g_mutex_lock (&registry->priv->sources_lock);

	source = g_hash_table_lookup (registry->priv->sources, uid);

	if (source != NULL)
		g_object_ref (source);

	g_mutex_unlock (&registry->priv->sources_lock);

	return source;
}

static GList *
source_registry_sources_get_values (ESourceRegistry *registry)
{
	GList *values;

	g_mutex_lock (&registry->priv->sources_lock);

	values = g_hash_table_get_values (registry->priv->sources);

	g_list_foreach (values, (GFunc) g_object_ref, NULL);

	g_mutex_unlock (&registry->priv->sources_lock);

	return values;
}

static GNode *
source_registry_sources_build_tree (ESourceRegistry *registry)
{
	GNode *root;
	GHashTable *index;
	GHashTableIter iter;
	gpointer key, value;

	g_mutex_lock (&registry->priv->sources_lock);

	root = g_node_new (NULL);
	index = g_hash_table_new (g_str_hash, g_str_equal);

	/* Add a GNode for each ESource to the index. */
	g_hash_table_iter_init (&iter, registry->priv->sources);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		ESource *source = g_object_ref (value);
		g_hash_table_insert (index, key, g_node_new (source));
	}

	/* Traverse the index and link the nodes together. */
	g_hash_table_iter_init (&iter, index);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		ESource *source;
		GNode *source_node;
		GNode *parent_node;
		const gchar *parent_uid;

		source_node = (GNode *) value;
		source = E_SOURCE (source_node->data);
		parent_uid = e_source_get_parent (source);

		if (parent_uid == NULL || *parent_uid == '\0') {
			parent_node = root;
		} else {
			parent_node = g_hash_table_lookup (index, parent_uid);
			g_warn_if_fail (parent_node != NULL);
		}

		/* Should never be NULL, but just to be safe. */
		if (parent_node != NULL)
			g_node_append (parent_node, source_node);
	}

	g_hash_table_destroy (index);

	g_mutex_unlock (&registry->priv->sources_lock);

	return root;
}

static void
source_registry_settings_changed_cb (GSettings *settings,
                                     const gchar *key,
                                     ESourceRegistry *registry)
{
	/* We define a property name that matches every key in
	 * the "org.gnome.Evolution.DefaultSources" schema. */
	g_object_notify (G_OBJECT (registry), key);
}

static gboolean
source_registry_source_changed_idle_cb (gpointer user_data)
{
	SourceClosure *closure = user_data;
	ESourceRegistry *registry;

	registry = g_weak_ref_get (&closure->registry);

	if (registry != NULL) {
		g_signal_emit (
			registry,
			signals[SOURCE_CHANGED], 0,
			closure->source);
		g_object_unref (registry);
	}

	return FALSE;
}

static gboolean
source_registry_source_notify_enabled_idle_cb (gpointer user_data)
{
	SourceClosure *closure = user_data;
	ESourceRegistry *registry;

	registry = g_weak_ref_get (&closure->registry);

	if (registry != NULL) {
		if (e_source_get_enabled (closure->source)) {
			g_signal_emit (
				registry,
				signals[SOURCE_ENABLED], 0,
				closure->source);
		} else {
			g_signal_emit (
				registry,
				signals[SOURCE_DISABLED], 0,
				closure->source);
		}
		g_object_unref (registry);
	}

	return FALSE;
}

static void
source_registry_source_changed_cb (ESource *source,
                                   ESourceRegistry *registry)
{
	GSource *idle_source;
	SourceClosure *closure;

	closure = g_slice_new0 (SourceClosure);
	g_weak_ref_init (&closure->registry, registry);
	closure->source = g_object_ref (source);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		source_registry_source_changed_idle_cb,
		closure, (GDestroyNotify) source_closure_free);
	g_source_attach (idle_source, registry->priv->main_context);
	g_source_unref (idle_source);
}

static void
source_registry_source_notify_enabled_cb (ESource *source,
                                          GParamSpec *pspec,
                                          ESourceRegistry *registry)
{
	GSource *idle_source;
	SourceClosure *closure;

	closure = g_slice_new0 (SourceClosure);
	g_weak_ref_init (&closure->registry, registry);
	closure->source = g_object_ref (source);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		source_registry_source_notify_enabled_idle_cb,
		closure, (GDestroyNotify) source_closure_free);
	g_source_attach (idle_source, registry->priv->main_context);
	g_source_unref (idle_source);
}

static ESource *
source_registry_new_source (ESourceRegistry *registry,
                            GDBusObject *dbus_object)
{
	GMainContext *main_context;
	ESource *source;
	const gchar *object_path;
	GError *local_error = NULL;

	/* We don't want the ESource emitting "changed" signals from
	 * the manager thread, so we pass it the same main context the
	 * registry uses for scheduling signal emissions. */
	main_context = registry->priv->main_context;
	source = e_source_new (dbus_object, main_context, &local_error);
	object_path = g_dbus_object_get_object_path (dbus_object);

	/* The likelihood of an error here is slim, so it's
	 * sufficient to just print a warning if one occurs. */
	if (local_error != NULL) {
		g_warn_if_fail (source == NULL);
		g_critical (
			"ESourceRegistry: Failed to create a "
			"data source object for path '%s': %s",
			object_path, local_error->message);
		g_error_free (local_error);
		return NULL;
	}

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	/* Add the ESource to the object path table immediately. */
	source_registry_object_path_table_insert (
		registry, object_path, source);

	return source;
}

static void
source_registry_unref_source (ESource *source)
{
	g_signal_handlers_disconnect_matched (
		source, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
		source_registry_source_changed_cb, NULL);

	g_signal_handlers_disconnect_matched (
		source, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
		source_registry_source_notify_enabled_cb, NULL);

	g_object_unref (source);
}

static void
source_registry_add_source (ESourceRegistry *registry,
                            ESource *source)
{
	const gchar *uid;

	/* This is called in the manager thread during initialization
	 * and in response to "object-added" signals from the manager. */

	uid = e_source_get_uid (source);
	g_return_if_fail (uid != NULL);

	g_mutex_lock (&registry->priv->sources_lock);

	/* Check if we already have this source in the registry. */
	if (g_hash_table_lookup (registry->priv->sources, uid) != NULL) {
		g_mutex_unlock (&registry->priv->sources_lock);
		return;
	}

	g_signal_connect (
		source, "changed",
		G_CALLBACK (source_registry_source_changed_cb),
		registry);

	g_signal_connect (
		source, "notify::enabled",
		G_CALLBACK (source_registry_source_notify_enabled_cb),
		registry);

	g_mutex_unlock (&registry->priv->sources_lock);

	source_registry_sources_insert (registry, source);
}

static gboolean
source_registry_object_added_idle_cb (gpointer user_data)
{
	SourceClosure *closure = user_data;
	ESourceRegistry *registry;

	registry = g_weak_ref_get (&closure->registry);

	if (registry != NULL) {
		g_signal_emit (
			registry,
			signals[SOURCE_ADDED], 0,
			closure->source);
		g_object_unref (registry);
	}

	return FALSE;
}

static void
source_registry_object_added_by_owner (ESourceRegistry *registry,
                                       GDBusObject *dbus_object)
{
	SourceClosure *closure;
	GSource *idle_source;
	ESource *source;

	g_return_if_fail (E_DBUS_IS_OBJECT (dbus_object));

	source = source_registry_new_source (registry, dbus_object);
	g_return_if_fail (source != NULL);

	/* Add the new ESource to our internal hash table so it can be
	 * obtained through e_source_registry_ref_source() immediately. */
	source_registry_add_source (registry, source);

	/* Schedule a callback on the ESourceRegistry's GMainContext. */

	closure = g_slice_new0 (SourceClosure);
	g_weak_ref_init (&closure->registry, registry);
	closure->source = g_object_ref (source);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		source_registry_object_added_idle_cb,
		closure, (GDestroyNotify) source_closure_free);
	g_source_attach (idle_source, registry->priv->main_context);
	g_source_unref (idle_source);

	g_object_unref (source);
}

static void
source_registry_object_added_no_owner (ESourceRegistry *registry,
                                       GDBusObject *dbus_object)
{
	ESource *source = NULL;
	gchar *uid;

	uid = source_registry_dbus_object_dup_uid (dbus_object);

	if (source_registry_service_restart_table_remove (registry, uid))
		source = e_source_registry_ref_source (registry, uid);

	if (source != NULL) {
		const gchar *object_path;

		object_path = g_dbus_object_get_object_path (dbus_object);

		source_registry_object_path_table_insert (
			registry, object_path, source);

		__e_source_private_replace_dbus_object (source, dbus_object);

		g_object_unref (source);

	} else {
		source_registry_object_added_by_owner (registry, dbus_object);
	}

	g_free (uid);
}

static void
source_registry_object_added_cb (GDBusObjectManager *object_manager,
                                 GDBusObject *dbus_object,
                                 ESourceRegistry *registry)
{
	gchar *name_owner;

	name_owner = g_dbus_object_manager_client_get_name_owner (
		G_DBUS_OBJECT_MANAGER_CLIENT (object_manager));

	if (name_owner != NULL)
		source_registry_object_added_by_owner (registry, dbus_object);
	else
		source_registry_object_added_no_owner (registry, dbus_object);

	g_free (name_owner);
}

static gboolean
source_registry_object_removed_idle_cb (gpointer user_data)
{
	SourceClosure *closure = user_data;
	ESourceRegistry *registry;

	registry = g_weak_ref_get (&closure->registry);

	if (registry != NULL) {
		g_signal_emit (
			registry,
			signals[SOURCE_REMOVED], 0,
			closure->source);
		g_object_unref (registry);
	}

	return FALSE;
}

static void
source_registry_object_removed_by_owner (ESourceRegistry *registry,
                                         GDBusObject *dbus_object)
{
	SourceClosure *closure;
	GSource *idle_source;
	ESource *source;
	const gchar *object_path;

	/* Find the corresponding ESource in the object path table.
	 * Note that the lookup returns a new ESource reference. */
	object_path = g_dbus_object_get_object_path (dbus_object);
	source = source_registry_object_path_table_lookup (
		registry, object_path);
	g_return_if_fail (E_IS_SOURCE (source));

	/* Remove the ESource from the object path table immediately. */
	source_registry_object_path_table_remove (registry, object_path);

	/* Also remove the ESource from the sources table immediately. */
	if (!source_registry_sources_remove (registry, source)) {
		g_object_unref (source);
		g_return_if_reached ();
	}

	/* Strip the ESource of its GDBusObject. */
	__e_source_private_replace_dbus_object (source, NULL);

	/* Schedule a callback on the ESourceRegistry's GMainContext. */

	closure = g_slice_new0 (SourceClosure);
	g_weak_ref_init (&closure->registry, registry);
	closure->source = g_object_ref (source);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		source_registry_object_removed_idle_cb,
		closure, (GDestroyNotify) source_closure_free);
	g_source_attach (idle_source, registry->priv->main_context);
	g_source_unref (idle_source);

	g_object_unref (source);
}

static void
source_registry_object_removed_no_owner (ESourceRegistry *registry,
                                         GDBusObject *dbus_object)
{
	const gchar *object_path;

	object_path = g_dbus_object_get_object_path (dbus_object);

	if (source_registry_object_path_table_remove (registry, object_path)) {
		gchar *uid;

		uid = source_registry_dbus_object_dup_uid (dbus_object);
		source_registry_service_restart_table_add (registry, uid);
		g_free (uid);
	}
}

static void
source_registry_object_removed_cb (GDBusObjectManager *object_manager,
                                   GDBusObject *dbus_object,
                                   ESourceRegistry *registry)
{
	gchar *name_owner;

	name_owner = g_dbus_object_manager_client_get_name_owner (
		G_DBUS_OBJECT_MANAGER_CLIENT (object_manager));

	if (name_owner != NULL)
		source_registry_object_removed_by_owner (registry, dbus_object);
	else
		source_registry_object_removed_no_owner (registry, dbus_object);

	g_free (name_owner);
}

static void
source_registry_name_appeared (ESourceRegistry *registry)
{
	GList *list, *link;

	/* The D-Bus service restarted, and the GDBusObjectManager has
	 * just set its "name-owner" property having finished emitting
	 * an "object-added" signal for each GDBusObject. */

	list = source_registry_service_restart_table_steal_all (registry);

	for (link = list; link != NULL; link = g_list_next (link)) {
		SourceClosure *closure;
		GSource *idle_source;
		ESource *source;
		const gchar *uid = link->data;

		source = e_source_registry_ref_source (registry, uid);
		if (source == NULL)
			continue;

		closure = g_slice_new0 (SourceClosure);
		g_weak_ref_init (&closure->registry, registry);
		closure->source = g_object_ref (source);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			source_registry_object_removed_idle_cb,
			closure, (GDestroyNotify) source_closure_free);
		g_source_attach (idle_source, registry->priv->main_context);
		g_source_unref (idle_source);

		g_object_unref (source);
	}

	g_list_free_full (list, (GDestroyNotify) g_free);
}

static void
source_registry_name_vanished (ESourceRegistry *registry)
{
	/* This function is just a convenience breakpoint.  The D-Bus
	 * service aborted, so the GDBusObjectManager has cleared its
	 * "name-owner" property and will now emit a "object-removed"
	 * signal for each GDBusObject. */
}

static void
source_registry_notify_name_owner_cb (GDBusObjectManager *object_manager,
                                      GParamSpec *pspec,
                                      ESourceRegistry *registry)
{
	gchar *name_owner;

	name_owner = g_dbus_object_manager_client_get_name_owner (
		G_DBUS_OBJECT_MANAGER_CLIENT (object_manager));

	if (name_owner != NULL)
		source_registry_name_appeared (registry);
	else
		source_registry_name_vanished (registry);

	g_free (name_owner);
}

static gboolean
source_registry_object_manager_running (gpointer data)
{
	ThreadClosure *closure = data;

	g_mutex_lock (&closure->main_loop_mutex);
	g_cond_broadcast (&closure->main_loop_cond);
	g_mutex_unlock (&closure->main_loop_mutex);

	return FALSE;
}

static gpointer
source_registry_object_manager_thread (gpointer data)
{
	GDBusObjectManager *object_manager;
	ThreadClosure *closure = data;
	GSource *idle_source;
	GList *list, *link;
	gulong object_added_handler_id = 0;
	gulong object_removed_handler_id = 0;
	gulong notify_name_owner_handler_id = 0;

	/* GDBusObjectManagerClient grabs the thread-default GMainContext
	 * at creation time and only emits signals from that GMainContext.
	 * Running it in a separate thread prevents its signal emissions
	 * from being inhibited by someone overriding the thread-default
	 * GMainContext. */

	/* This becomes the GMainContext that GDBusObjectManagerClient
	 * will emit signals from.  Make it the thread-default context
	 * for this thread before creating the client. */
	g_main_context_push_thread_default (closure->main_context);

	object_manager = e_dbus_object_manager_client_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
		SOURCES_DBUS_SERVICE_NAME,
		DBUS_OBJECT_PATH,
		NULL, &closure->error);

	/* Sanity check. */
	g_warn_if_fail (
		((object_manager != NULL) && (closure->error == NULL)) ||
		((object_manager == NULL) && (closure->error != NULL)));

	/* If we failed to create the GDBusObjectManagerClient, skip
	 * straight to the main loop.  The GError will be propagated
	 * back to the caller, the main loop will terminate, and the
	 * partially-initialized ESourceRegistry will be destroyed. */
	if (object_manager == NULL)
		goto notify;

	/* Give the registry a handle to the object manager. */
	closure->registry->priv->dbus_object_manager =
		g_object_ref (object_manager);

	/* Now populate the registry with an initial set of ESources. */

	list = g_dbus_object_manager_get_objects (object_manager);

	for (link = list; link != NULL; link = g_list_next (link)) {
		GDBusObject *dbus_object;
		ESource *source;

		dbus_object = G_DBUS_OBJECT (link->data);

		source = source_registry_new_source (
			closure->registry, dbus_object);

		if (source != NULL) {
			source_registry_add_source (
				closure->registry, source);
			g_object_unref (source);
		}
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Listen for D-Bus object additions and removals. */

	object_added_handler_id = g_signal_connect (
		object_manager, "object-added",
		G_CALLBACK (source_registry_object_added_cb),
		closure->registry);

	object_removed_handler_id = g_signal_connect (
		object_manager, "object-removed",
		G_CALLBACK (source_registry_object_removed_cb),
		closure->registry);

	notify_name_owner_handler_id = g_signal_connect (
		object_manager, "notify::name-owner",
		G_CALLBACK (source_registry_notify_name_owner_cb),
		closure->registry);

notify:
	/* Schedule a one-time idle callback to broadcast through a
	 * condition variable that our main loop is up and running. */

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		source_registry_object_manager_running,
		closure, (GDestroyNotify) NULL);
	g_source_attach (idle_source, closure->main_context);
	g_source_unref (idle_source);

	/* Now we mostly idle here for the rest of the session. */

	g_main_loop_run (closure->main_loop);

	/* Clean up and exit. */

	if (object_manager != NULL) {
		g_signal_handler_disconnect (
			object_manager, object_added_handler_id);
		g_signal_handler_disconnect (
			object_manager, object_removed_handler_id);
		g_signal_handler_disconnect (
			object_manager, notify_name_owner_handler_id);
		g_object_unref (object_manager);
	}

	g_main_context_pop_thread_default (closure->main_context);

	return NULL;
}

static void
source_registry_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_ADDRESS_BOOK:
			e_source_registry_set_default_address_book (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;

		case PROP_DEFAULT_CALENDAR:
			e_source_registry_set_default_calendar (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;

		case PROP_DEFAULT_MAIL_ACCOUNT:
			e_source_registry_set_default_mail_account (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;

		case PROP_DEFAULT_MAIL_IDENTITY:
			e_source_registry_set_default_mail_identity (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;

		case PROP_DEFAULT_MEMO_LIST:
			e_source_registry_set_default_memo_list (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;

		case PROP_DEFAULT_TASK_LIST:
			e_source_registry_set_default_task_list (
				E_SOURCE_REGISTRY (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_registry_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_ADDRESS_BOOK:
			g_value_take_object (
				value,
				e_source_registry_ref_default_address_book (
				E_SOURCE_REGISTRY (object)));
			return;

		case PROP_DEFAULT_CALENDAR:
			g_value_take_object (
				value,
				e_source_registry_ref_default_calendar (
				E_SOURCE_REGISTRY (object)));
			return;

		case PROP_DEFAULT_MAIL_ACCOUNT:
			g_value_take_object (
				value,
				e_source_registry_ref_default_mail_account (
				E_SOURCE_REGISTRY (object)));
			return;

		case PROP_DEFAULT_MAIL_IDENTITY:
			g_value_take_object (
				value,
				e_source_registry_ref_default_mail_identity (
				E_SOURCE_REGISTRY (object)));
			return;

		case PROP_DEFAULT_MEMO_LIST:
			g_value_take_object (
				value,
				e_source_registry_ref_default_memo_list (
				E_SOURCE_REGISTRY (object)));
			return;

		case PROP_DEFAULT_TASK_LIST:
			g_value_take_object (
				value,
				e_source_registry_ref_default_task_list (
				E_SOURCE_REGISTRY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_registry_dispose (GObject *object)
{
	ESourceRegistryPrivate *priv;

	priv = E_SOURCE_REGISTRY_GET_PRIVATE (object);

	/* Terminate the manager thread first. */
	if (priv->manager_thread != NULL) {
		g_main_loop_quit (priv->thread_closure->main_loop);
		g_thread_join (priv->manager_thread);
		thread_closure_free (priv->thread_closure);
		priv->manager_thread = NULL;
		priv->thread_closure = NULL;
	}

	if (priv->main_context != NULL) {
		g_main_context_unref (priv->main_context);
		priv->main_context = NULL;
	}

	if (priv->dbus_object_manager != NULL) {
		g_object_unref (priv->dbus_object_manager);
		priv->dbus_object_manager = NULL;
	}

	if (priv->dbus_source_manager != NULL) {
		g_object_unref (priv->dbus_source_manager);
		priv->dbus_source_manager = NULL;
	}

	g_hash_table_remove_all (priv->object_path_table);

	g_hash_table_remove_all (priv->sources);

	if (priv->settings != NULL) {
		g_signal_handlers_disconnect_by_data (priv->settings, object);
		g_object_unref (priv->settings);
		priv->settings = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_registry_parent_class)->dispose (object);
}

static void
source_registry_finalize (GObject *object)
{
	ESourceRegistryPrivate *priv;

	priv = E_SOURCE_REGISTRY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->object_path_table);
	g_mutex_clear (&priv->object_path_table_lock);

	g_hash_table_destroy (priv->service_restart_table);
	g_mutex_clear (&priv->service_restart_table_lock);

	g_hash_table_destroy (priv->sources);
	g_mutex_clear (&priv->sources_lock);

	g_clear_error (&priv->init_error);
	g_mutex_clear (&priv->init_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_registry_parent_class)->finalize (object);
}

static gboolean
source_registry_initable_init (GInitable *initable,
                               GCancellable *cancellable,
                               GError **error)
{
	ESourceRegistry *registry;
	ThreadClosure *closure;
	GError *local_error = NULL;

	registry = E_SOURCE_REGISTRY (initable);

	g_mutex_lock (&registry->priv->init_lock);

	if (registry->priv->initialized)
		goto exit;

	closure = g_slice_new0 (ThreadClosure);
	closure->registry = registry;  /* do not reference */
	closure->main_context = g_main_context_new ();
	/* It's important to pass 'is_running=FALSE' here because
	 * we wait for the main loop to start running as a way of
	 * synchronizing with the manager thread. */
	closure->main_loop = g_main_loop_new (closure->main_context, FALSE);
	g_cond_init (&closure->main_loop_cond);
	g_mutex_init (&closure->main_loop_mutex);

	registry->priv->thread_closure = closure;

	registry->priv->manager_thread = g_thread_new (
		NULL,
		source_registry_object_manager_thread,
		closure);

	/* Wait for notification that the manager
	 * thread's main loop has been started. */
	g_mutex_lock (&closure->main_loop_mutex);
	while (!g_main_loop_is_running (closure->main_loop))
		g_cond_wait (
			&closure->main_loop_cond,
			&closure->main_loop_mutex);
	g_mutex_unlock (&closure->main_loop_mutex);

	/* Check for error in the manager thread. */
	if (closure->error != NULL) {
		g_dbus_error_strip_remote_error (closure->error);
		g_propagate_error (&registry->priv->init_error, closure->error);
		closure->error = NULL;
		goto exit;
	}

	/* The registry should now be populated with sources.
	 *
	 * XXX Actually, not necessarily if the registry service was
	 *     just now activated.  There may yet be a small window
	 *     while the registry service starts up before it exports
	 *     any sources, even built-in sources.  This COULD create
	 *     problems if any logic that depends on those built-in
	 *     sources executes during this time window, but so far
	 *     we haven't seen any cases of that.
	 *
	 *     Attempts in the past to stop and wait for sources to
	 *     show up have proven problematic.  See for example:
	 *     https://bugzilla.gnome.org/678378
	 *
	 *     Leave the runtime check disabled for the moment.
	 *     I have a feeling I'll be revisiting this again.
	 */
	/*g_warn_if_fail (g_hash_table_size (registry->priv->sources) > 0);*/

	/* The EDBusSourceManagerProxy is just another D-Bus interface
	 * that resides at the same object path.  It's unrelated to the
	 * GDBusObjectManagerClient and doesn't need its own thread. */
	registry->priv->dbus_source_manager =
		e_dbus_source_manager_proxy_new_for_bus_sync (
			G_BUS_TYPE_SESSION,
			G_DBUS_PROXY_FLAGS_NONE,
			SOURCES_DBUS_SERVICE_NAME,
			DBUS_OBJECT_PATH,
			cancellable, &local_error);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (&registry->priv->init_error, local_error);
		goto exit;
	}

	/* Allow authentication prompts for all exported data sources
	 * when a new EDBusSourceManagerProxy is created.  The thought
	 * being, if you cancel an authentication prompt you will not
	 * be bothered again until you start (or restart) a new E-D-S
	 * client app.  Failure here is non-fatal, ignore errors. */
	e_dbus_source_manager_call_allow_auth_prompt_all_sync (
		registry->priv->dbus_source_manager, cancellable, NULL);

exit:
	registry->priv->initialized = TRUE;
	g_mutex_unlock (&registry->priv->init_lock);

	if (registry->priv->init_error != NULL) {
		GError *init_error_copy;

		/* Return a copy of the same error to
		 * all pending initialization requests. */
		init_error_copy = g_error_copy (registry->priv->init_error);
		g_propagate_error (error, init_error_copy);

		return FALSE;
	}

	return TRUE;
}

static void
e_source_registry_class_init (ESourceRegistryClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESourceRegistryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_registry_set_property;
	object_class->get_property = source_registry_get_property;
	object_class->dispose = source_registry_dispose;
	object_class->finalize = source_registry_finalize;

	/* The property names correspond to the key names in the
	 * "org.gnome.Evolution.DefaultSources" GSettings schema. */

	/**
	 * ESourceRegistry:default-address-book:
	 *
	 * The default address book #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_ADDRESS_BOOK,
		g_param_spec_object (
			"default-address-book",
			"Default Address Book",
			"The default address book ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry:default-calendar:
	 *
	 * The default calendar #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_CALENDAR,
		g_param_spec_object (
			"default-calendar",
			"Default Calendar",
			"The default calendar ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry:default-mail-account:
	 *
	 * The default mail account #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_MAIL_ACCOUNT,
		g_param_spec_object (
			"default-mail-account",
			"Default Mail Account",
			"The default mail account ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry:default-mail-identity:
	 *
	 * The default mail identity #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_MAIL_IDENTITY,
		g_param_spec_object (
			"default-mail-identity",
			"Default Mail Identity",
			"The default mail identity ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry:default-memo-list:
	 *
	 * The default memo list #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_MEMO_LIST,
		g_param_spec_object (
			"default-memo-list",
			"Default Memo List",
			"The default memo list ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry:default-task-list:
	 *
	 * The default task list #ESource.
	 **/
	g_object_class_install_property (
		object_class,
		PROP_DEFAULT_TASK_LIST,
		g_param_spec_object (
			"default-task-list",
			"Default Task List",
			"The default task list ESource",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ESourceRegistry::source-added:
	 * @registry: the #ESourceRegistry which emitted the signal
	 * @source: the newly-added #ESource
	 *
	 * Emitted when an #ESource is added to @registry.
	 **/
	signals[SOURCE_ADDED] = g_signal_new (
		"source-added",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryClass, source_added),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	/**
	 * ESourceRegistry::source-changed:
	 * @registry: the #ESourceRegistry which emitted the signal
	 * @source: the #ESource that changed
	 *
	 * Emitted when an #ESource registered with @registry emits
	 * its #ESource::changed signal.
	 **/
	signals[SOURCE_CHANGED] = g_signal_new (
		"source-changed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryClass, source_changed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	/**
	 * ESourceRegistry::source-removed:
	 * @registry: the #ESourceRegistry which emitted the signal
	 * @source: the #ESource that got removed
	 *
	 * Emitted when an #ESource is removed from @registry.
	 **/
	signals[SOURCE_REMOVED] = g_signal_new (
		"source-removed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryClass, source_removed),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	/**
	 * ESourceRegistry::source-enabled:
	 * @registry: the #ESourceRegistry which emitted the signal
	 * @source: the #ESource that got enabled
	 *
	 * Emitted when an #ESource #ESource:enabled property becomes %TRUE.
	 **/
	signals[SOURCE_ENABLED] = g_signal_new (
		"source-enabled",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryClass, source_enabled),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);

	/**
	 * ESourceRegistry::source-disabled:
	 * @registry: the #ESourceRegistry which emitted the signal
	 * @source: the #ESource that got disabled
	 *
	 * Emitted when an #ESource #ESource:enabled property becomes %FALSE.
	 **/
	signals[SOURCE_DISABLED] = g_signal_new (
		"source-disabled",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ESourceRegistryClass, source_disabled),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		E_TYPE_SOURCE);
}

static void
e_source_registry_initable_init (GInitableIface *iface)
{
	iface->init = source_registry_initable_init;
}

static void
e_source_registry_init (ESourceRegistry *registry)
{
	registry->priv = E_SOURCE_REGISTRY_GET_PRIVATE (registry);

	/* This is so the object manager thread can schedule signal
	 * emissions on the thread-default context for this thread. */
	registry->priv->main_context = g_main_context_ref_thread_default ();

	/* D-Bus object path -> ESource */
	registry->priv->object_path_table =
		g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) g_object_unref);

	g_mutex_init (&registry->priv->object_path_table_lock);

	/* Set of UID strings */
	registry->priv->service_restart_table =
		g_hash_table_new_full (
			(GHashFunc) g_str_hash,
			(GEqualFunc) g_str_equal,
			(GDestroyNotify) g_free,
			(GDestroyNotify) NULL);

	g_mutex_init (&registry->priv->service_restart_table_lock);

	/* UID string -> ESource */
	registry->priv->sources = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) source_registry_unref_source);

	g_mutex_init (&registry->priv->sources_lock);

	registry->priv->settings = g_settings_new (GSETTINGS_SCHEMA);

	g_signal_connect (
		registry->priv->settings, "changed",
		G_CALLBACK (source_registry_settings_changed_cb), registry);

	g_mutex_init (&registry->priv->init_lock);
}

/**
 * e_source_registry_new_sync:
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #ESourceRegistry front-end for the registry D-Bus service.
 * If an error occurs in connecting to the D-Bus service, the function sets
 * @error and returns %NULL.
 *
 * Since 3.12 a singleton will be returned.  No strong reference is kept
 * internally, so it is the caller's responsibility to keep one.
 *
 * Returns: a new #ESourceRegistry, or %NULL
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_source_registry_new_sync (GCancellable *cancellable,
                            GError **error)
{
	ESourceRegistry *registry;

	/* XXX Work around http://bugzilla.gnome.org/show_bug.cgi?id=683519
	 *     until GObject's type initialization deadlock issue is fixed.
	 *     Apparently only the synchronous instantiation is affected. */
	g_type_ensure (G_TYPE_DBUS_CONNECTION);

	registry = source_registry_dup_uninitialized_singleton ();

	if (!g_initable_init (G_INITABLE (registry), cancellable, error))
		g_clear_object (&registry);

	return registry;
}

/* Helper for e_source_registry_new() */
static void
source_registry_init_cb (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
	GTask *task = user_data;
	GError *local_error = NULL;

	g_async_initable_init_finish (
		G_ASYNC_INITABLE (source_object), result, &local_error);

	if (local_error == NULL) {
		g_task_return_pointer (
			task, g_object_ref (source_object),
			(GDestroyNotify) g_object_unref);
	} else {
		g_task_return_error (task, local_error);
	}

	g_object_unref (task);
}

/**
 * e_source_registry_new:
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously creates a new #ESourceRegistry front-end for the registry
 * D-Bus service.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_registry_new_finish() to get the result of the operation.
 *
 * Since 3.12 a singleton will be returned.  No strong reference is kept
 * internally, so it is the caller's responsibility to keep one.
 *
 * Since: 3.6
 **/
void
e_source_registry_new (GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	ESourceRegistry *registry;
	GTask *task;

	task = g_task_new (NULL, cancellable, callback, user_data);

	registry = source_registry_dup_uninitialized_singleton ();

	g_async_initable_init_async (
		G_ASYNC_INITABLE (registry),
		G_PRIORITY_DEFAULT, cancellable,
		source_registry_init_cb, task);

	g_object_unref (registry);
}

/**
 * e_source_registry_new_finish:
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_registry_new_finish().
 * If an error occurs in connecting to the D-Bus service, the function
 * sets @error and returns %NULL.
 *
 * Returns: a new #ESourceRegistry, or %NULL
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_source_registry_new_finish (GAsyncResult *result,
                              GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, NULL), NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

/* Helper for e_source_registry_authenticate() */
static void
source_registry_authenticate_thread (GSimpleAsyncResult *simple,
                                     GObject *object,
                                     GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_source_registry_authenticate_sync (
		E_SOURCE_REGISTRY (object),
		async_context->source,
		async_context->auth,
		cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

/* Helper for e_source_registry_authenticate_sync() */
static gboolean
source_registry_authenticate_respond_cb (AuthContext *auth_context)
{
	ESourceAuthenticationResult auth_result;
	GError *non_fatal_error = NULL;

	g_return_val_if_fail (auth_context->authenticating, FALSE);

	auth_result = auth_context->auth_result;

	/* Allow the next authentication attempt to proceed. */
	auth_context->authenticating = FALSE;

	/* Send the server a status update based on the authentication
	 * result.  Note, we don't really care if the D-Bus message gets
	 * through to the server at this point.  If it doesn't, the auth
	 * session will either time out on its own or the authentication
	 * dialog will eventually be dismissed by the user. */

	/* If we were cancelled from our side, we have a bit of a dilemma.
	 * We need to tell the server to cancel the authentication session,
	 * but that involves making a synchronous D-Bus call, which we are
	 * not supposed to do if we know we've been cancelled.  But if we
	 * don't tell the server, the authentication session will be left
	 * to timeout on its own (which may take minutes), and meanwhile
	 * all other authentication requests are blocked.  So choose the
	 * lesser evil and make the synchronous call but without passing
	 * the already-cancelled GCancellable. */
	if (g_cancellable_is_cancelled (auth_context->cancellable)) {
		e_dbus_authenticator_call_cancel_sync (
			auth_context->dbus_auth,
			NULL, &non_fatal_error);
		g_main_loop_quit (auth_context->main_loop);

	/* If an error occurred while attempting to authenticate,
	 * tell the server to cancel the authentication session. */
	} else if (auth_result == E_SOURCE_AUTHENTICATION_ERROR) {
		e_dbus_authenticator_call_cancel_sync (
			auth_context->dbus_auth,
			auth_context->cancellable,
			&non_fatal_error);
		g_main_loop_quit (auth_context->main_loop);

	/* If the password was accepted, let the server know so it
	 * can close any authentication dialogs and save the user
	 * provided password to the keyring. */
	} else if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		e_dbus_authenticator_call_accepted_sync (
			auth_context->dbus_auth,
			auth_context->cancellable,
			&non_fatal_error);
		g_main_loop_quit (auth_context->main_loop);

	/* If the password was rejected, let the server know so it can
	 * indicate failure and request a different password, and then
	 * wait for the next "response" signal. */
	} else {
		e_dbus_authenticator_call_rejected_sync (
			auth_context->dbus_auth,
			auth_context->cancellable,
			&non_fatal_error);
	}

	/* Leave breadcrumbs if something went wrong,
	 * but don't fail the whole operation over it. */
	if (non_fatal_error != NULL) {
		g_dbus_error_strip_remote_error (non_fatal_error);
		g_warning ("%s: %s", G_STRFUNC, non_fatal_error->message);
		g_error_free (non_fatal_error);
	}

	return FALSE;
}

/* Helper for e_source_registry_authenticate_sync() */
static void
source_registry_authenticate_authenticate_cb (EDBusAuthenticator *dbus_auth,
                                              const gchar *encrypted_secret,
                                              AuthContext *auth_context)
{
	GSource *idle_source;
	GMainContext *main_context;
	GString *password;
	gboolean valid_secret;

	/* We should only get one secret at a time. */
	g_return_if_fail (!auth_context->authenticating);

	valid_secret = gcr_secret_exchange_receive (
		auth_context->secret_exchange, encrypted_secret);
	g_return_if_fail (valid_secret);

	auth_context->authenticating = TRUE;

	/* This avoids revealing the password in a stack trace. */
	password = g_string_new (
		gcr_secret_exchange_get_secret (
		auth_context->secret_exchange, NULL));

	/* Try authenticating with the given password.  We have to
	 * call this synchronously because some authenticators use
	 * mutexes to serialize I/O operations and are not prepared
	 * to make authentication attempts from a different thread.
	 *
	 * Unfortunately this means we won't notice server-side
	 * dismissals while the main loop is blocked.  We respond
	 * to the server from a low-priority idle callback so that
	 * any pending "dismissed" signals get handled first. */

	auth_context->auth_result =
		e_source_authenticator_try_password_sync (
			auth_context->auth, password,
			auth_context->cancellable,
			auth_context->error);

	idle_source = g_idle_source_new ();
	main_context = g_main_context_get_thread_default ();
	g_source_set_callback (
		idle_source, (GSourceFunc)
		source_registry_authenticate_respond_cb,
		auth_context, NULL);
	g_source_attach (idle_source, main_context);
	g_source_unref (idle_source);

	g_string_free (password, TRUE);
}

/* Helper for e_source_registry_authenticate_sync() */
static void
source_registry_authenticate_dismissed_cb (EDBusAuthenticator *dbus_auth,
                                           AuthContext *auth_context)
{
	/* Be careful not to overwrite an existing error in case this
	 * is called after e_source_authenticator_try_password_sync()
	 * but prior to the idle callback. */
	if (auth_context->auth_result != E_SOURCE_AUTHENTICATION_ERROR) {
		/* XXX Use a separate error code for dismissals? */
		g_set_error_literal (
			auth_context->error,
			G_IO_ERROR, G_IO_ERROR_CANCELLED,
			_("The user declined to authenticate"));
		auth_context->auth_result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_main_loop_quit (auth_context->main_loop);
}

/* Helper for e_source_registry_authenticate_sync() */
static void
source_registry_authenticate_server_error_cb (EDBusAuthenticator *dbus_auth,
                                              const gchar *name,
                                              const gchar *message,
                                              AuthContext *auth_context)
{
	/* Be careful not to overwrite an existing error */
	if (auth_context->auth_result != E_SOURCE_AUTHENTICATION_ERROR) {
		GError *error;

		error = g_dbus_error_new_for_dbus_error (name, message);
		g_propagate_error (auth_context->error, error);

		auth_context->auth_result = E_SOURCE_AUTHENTICATION_ERROR;
	}

	g_main_loop_quit (auth_context->main_loop);
}

/* Helper for e_source_registry_authenticate_sync() */
static gboolean
source_registry_call_authenticate_for_source (ESourceRegistry *registry,
                                              ESourceAuthenticator *auth,
                                              ESource *source,
                                              gchar **out_object_path,
                                              GCancellable *cancellable,
                                              GError **error)
{
	ESource *collection;
	const gchar *uid;
	gchar *prompt_title = NULL;
	gchar *prompt_message = NULL;
	gchar *prompt_description = NULL;
	GError *local_error = NULL;

	g_object_ref (source);

	/* If the source is a member of a collection, we want to store
	 * the password under the UID of the "collection" source so it
	 * will apply to the entire collection.
	 *
	 * XXX This assumes all sources in a collection share a single
	 *     password.  If that turns out not to be true in all cases
	 *     we could maybe add a "SharedPassword: true/false" key to
	 *     [Collection] and apply it here.
	 *
	 *     Addendum: Assumption proven wrong.  GOA's generic IMAP/SMTP
	 *               provider uses a plain ECollectionBackend (backend
	 *               name "none") with separately stored passwords for
	 *               IMAP vs SMTP.  Just handle this case directly for
	 *               now, but don't rule out the "SharedPassword" idea.
	 */
	collection = e_source_registry_find_extension (
		registry, source, E_SOURCE_EXTENSION_COLLECTION);
	if (collection != NULL) {
		ESourceBackend *extension;
		gchar *backend_name;

		extension = e_source_get_extension (
			collection, E_SOURCE_EXTENSION_COLLECTION);
		backend_name = e_source_backend_dup_backend_name (extension);

		if (g_strcmp0 (backend_name, "none") != 0) {
			g_object_unref (source);
			source = g_object_ref (collection);
		}

		g_free (backend_name);

		g_object_unref (collection);
	}

	uid = e_source_get_uid (source);

	e_source_authenticator_get_prompt_strings (
		auth, source,
		&prompt_title,
		&prompt_message,
		&prompt_description);

	e_dbus_source_manager_call_authenticate_sync (
		registry->priv->dbus_source_manager, uid,
		prompt_title, prompt_message, prompt_description,
		out_object_path, cancellable, &local_error);

	g_free (prompt_title);
	g_free (prompt_message);
	g_free (prompt_description);

	g_object_unref (source);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/**
 * e_source_registry_authenticate_sync:
 * @registry: an #ESourceRegistry
 * @source: an #ESource
 * @auth: an #ESourceAuthenticator
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Authenticates @source, using @auth to handle the authentication
 * attempts.  The operation loops until authentication is successful or
 * the user aborts further authentication attempts.  If an error occurs,
 * the function will set @error and return %FALSE.
 *
 * Note that @source need not have a #GDBusObject, which means this
 * function can test authentication on a scratch #ESource.
 *
 * Only backend implementations and data source editors should call this
 * function.  The intent is for basic client applications to not have to
 * deal with authentication at all.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_authenticate_sync (ESourceRegistry *registry,
                                     ESource *source,
                                     ESourceAuthenticator *auth,
                                     GCancellable *cancellable,
                                     GError **error)
{
	AuthContext *auth_context;
	GMainContext *main_context;
	EDBusAuthenticator *dbus_auth;
	gboolean without_password;
	gchar *encryption_key;
	gchar *object_path = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth), FALSE);

	/* This extracts authentication prompt details for the ESource
	 * before initiating an authentication session with the server,
	 * so split it out of the main algorithm for clarity's sake. */
	source_registry_call_authenticate_for_source (
		registry, auth, source, &object_path,
		cancellable, &local_error);

	if (local_error != NULL) {
		g_warn_if_fail (object_path == NULL);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	g_return_val_if_fail (object_path != NULL, FALSE);

	main_context = g_main_context_new ();
	g_main_context_push_thread_default (main_context);

	dbus_auth = e_dbus_authenticator_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		SOURCES_DBUS_SERVICE_NAME,
		object_path, cancellable, &local_error);

	g_free (object_path);

	/* Sanity check. */
	g_return_val_if_fail (
		((dbus_auth != NULL) && (local_error == NULL)) ||
		((dbus_auth == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL)
		goto exit;

	without_password = e_source_authenticator_get_without_password (auth);
	e_dbus_authenticator_set_without_password (dbus_auth, without_password);

	auth_context = g_slice_new0 (AuthContext);
	auth_context->auth = g_object_ref (auth);
	auth_context->dbus_auth = dbus_auth;  /* takes ownership */
	auth_context->main_loop = g_main_loop_new (main_context, FALSE);
	auth_context->error = &local_error;

	/* This just needs to be something other than
	 * E_SOURCE_AUTHENTICATION_ERROR so we don't trip
	 * up source_registry_authenticate_dismissed_cb(). */
	auth_context->auth_result = E_SOURCE_AUTHENTICATION_REJECTED;

	if (G_IS_CANCELLABLE (cancellable))
		auth_context->cancellable = g_object_ref (cancellable);

	auth_context->secret_exchange =
		gcr_secret_exchange_new (GCR_SECRET_EXCHANGE_PROTOCOL_1);

	g_signal_connect (
		dbus_auth, "authenticate",
		G_CALLBACK (source_registry_authenticate_authenticate_cb),
		auth_context);

	g_signal_connect (
		dbus_auth, "dismissed",
		G_CALLBACK (source_registry_authenticate_dismissed_cb),
		auth_context);

	g_signal_connect (
		dbus_auth, "server-error",
		G_CALLBACK (source_registry_authenticate_server_error_cb),
		auth_context);

	encryption_key = gcr_secret_exchange_begin (
		auth_context->secret_exchange);

	/* Signal the D-Bus server that we're ready to begin the
	 * authentication session.  This must happen AFTER we've
	 * connected to the response signal since the server may
	 * already have a response ready and waiting for us. */
	e_dbus_authenticator_call_ready_sync (
		dbus_auth, encryption_key, cancellable, &local_error);

	g_free (encryption_key);

	if (local_error == NULL)
		g_main_loop_run (auth_context->main_loop);

	auth_context_free (auth_context);

exit:
	g_main_context_pop_thread_default (main_context);

	/* Make sure the main_context doesn't have pending operations;
	 * workarounds https://bugzilla.gnome.org/show_bug.cgi?id=690126 */
	while (g_main_context_pending (main_context))
		g_main_context_iteration (main_context, FALSE);

	g_main_context_unref (main_context);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/**
 * e_source_registry_authenticate:
 * @registry: an #ESourceRegistry
 * @source: an #ESource
 * @auth: an #ESourceAuthenticator
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously authenticates @source, using @auth to handle the
 * authentication attempts.  The operation loops until authentication
 * is successful or the user aborts further authentication attempts.
 *
 * Note that @source need not have a #GDBusObject, which means this
 * function can test authentication on a scratch #ESource.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_registry_authenticate_finish() to get the result of the
 * operation.
 *
 * Only backend implementations and data source editors should call this
 * function.  The intent is for basic client applications to not have to
 * deal with authentication at all.
 *
 * Since: 3.6
 **/
void
e_source_registry_authenticate (ESourceRegistry *registry,
                                ESource *source,
                                ESourceAuthenticator *auth,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATOR (auth));

	async_context = g_slice_new0 (AsyncContext);
	async_context->source = g_object_ref (source);
	async_context->auth = g_object_ref (auth);

	simple = g_simple_async_result_new (
		G_OBJECT (registry), callback, user_data,
		e_source_registry_authenticate);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_registry_authenticate_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_source_registry_authenticate_finish:
 * @registry: an #ESourceRegistry
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_registry_authenticate().
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_authenticate_finish (ESourceRegistry *registry,
                                       GAsyncResult *result,
                                       GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (registry),
		e_source_registry_authenticate), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/* Helper for e_source_registry_commit_source() */
static void
source_registry_commit_source_thread (GSimpleAsyncResult *simple,
                                      GObject *object,
                                      GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_source_registry_commit_source_sync (
		E_SOURCE_REGISTRY (object),
		async_context->source,
		cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

/**
 * e_source_registry_commit_source_sync:
 * @registry: an #ESourceRegistry
 * @source: an #ESource with changes to commit
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for #GError, or %NULL
 *
 * This is a convenience function intended for use with graphical
 * #ESource editors.  Call this function when the user is finished
 * making changes to @source.
 *
 * If @source has a #GDBusObject, its contents are submitted to the D-Bus
 * service through e_source_write_sync().
 *
 * If @source does NOT have a #GDBusObject (implying it's a scratch
 * #ESource), its contents are submitted to the D-Bus service through
 * either e_source_remote_create_sync() if @source is to be a collection
 * member, or e_source_registry_create_sources_sync() if @source to be an
 * independent data source.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_commit_source_sync (ESourceRegistry *registry,
                                      ESource *source,
                                      GCancellable *cancellable,
                                      GError **error)
{
	GDBusObject *dbus_object;
	ESource *collection_source;
	gboolean collection_member;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	dbus_object = e_source_ref_dbus_object (source);

	collection_source = e_source_registry_find_extension (
		registry, source, E_SOURCE_EXTENSION_COLLECTION);

	collection_member =
		(collection_source != NULL) &&
		(collection_source != source);

	if (dbus_object != NULL) {
		success = e_source_write_sync (source, cancellable, error);
		g_object_unref (dbus_object);

	} else if (collection_member) {
		success = e_source_remote_create_sync (
			collection_source, source, cancellable, error);

	} else {
		GList *list = g_list_prepend (NULL, source);
		success = e_source_registry_create_sources_sync (
			registry, list, cancellable, error);
		g_list_free (list);
	}

	if (collection_source != NULL)
		g_object_unref (collection_source);

	return success;
}

/**
 * e_source_registry_commit_source:
 * @registry: an #ESourceRegistry
 * @source: an #ESource with changes to commit
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * See e_source_registry_commit_source_sync() for details.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_registry_commit_source_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
e_source_registry_commit_source (ESourceRegistry *registry,
                                 ESource *source,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (E_IS_SOURCE (source));

	async_context = g_slice_new0 (AsyncContext);
	async_context->source = g_object_ref (source);

	simple = g_simple_async_result_new (
		G_OBJECT (registry), callback, user_data,
		e_source_registry_commit_source);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_registry_commit_source_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_source_registry_commit_source_finish:
 * @registry: an #ESourceRegistry
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_registry_commit_source().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_commit_source_finish (ESourceRegistry *registry,
                                        GAsyncResult *result,
                                        GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (registry),
		e_source_registry_commit_source), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/* Helper for e_source_registry_create_sources() */
static void
source_registry_create_sources_thread (GSimpleAsyncResult *simple,
                                       GObject *object,
                                       GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *local_error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	e_source_registry_create_sources_sync (
		E_SOURCE_REGISTRY (object),
		async_context->list_of_sources,
		cancellable, &local_error);

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);
}

/* Helper for e_source_registry_create_sources_sync() */
static gboolean
source_registry_create_sources_main_loop_quit_cb (gpointer user_data)
{
	GMainLoop *main_loop = user_data;

	g_main_loop_quit (main_loop);

	return FALSE;
}

/* Helper for e_source_registry_create_sources_sync() */
static void
source_registry_create_sources_object_added_cb (GDBusObjectManager *object_manager,
                                                GDBusObject *dbus_object,
                                                CreateContext *create_context)
{
	gchar *uid;

	uid = source_registry_dbus_object_dup_uid (dbus_object);

	if (uid != NULL) {
		g_hash_table_remove (create_context->pending_uids, uid);
		g_free (uid);
	}

	/* The hash table will be empty when all of the expected
	 * GDBusObjects have been added to the GDBusObjectManager. */
	if (g_hash_table_size (create_context->pending_uids) == 0) {
		GSource *idle_source;

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			source_registry_create_sources_main_loop_quit_cb,
			g_main_loop_ref (create_context->main_loop),
			(GDestroyNotify) g_main_loop_unref);
		g_source_attach (idle_source, create_context->main_context);
		g_source_unref (idle_source);
	}
}

/**
 * e_source_registry_create_sources_sync:
 * @registry: an #ESourceRegistry
 * @list_of_sources: (element-type ESource): a list of #ESource instances with
 * no #GDBusObject
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Requests the D-Bus service create new key files for each #ESource in
 * @list_of_sources.  Each list element must be a scratch #ESource with
 * no #GDBusObject.
 *
 * If an error occurs, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_create_sources_sync (ESourceRegistry *registry,
                                       GList *list_of_sources,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CreateContext *create_context;
	GVariantBuilder builder;
	GVariant *variant;
	GList *link;
	gulong object_added_id;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);

	/* Verify the list elements are all ESources. */
	for (link = list_of_sources; link != NULL; link = g_list_next (link))
		g_return_val_if_fail (E_IS_SOURCE (link->data), FALSE);

	create_context = create_context_new ();
	g_main_context_push_thread_default (create_context->main_context);

	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

	for (link = list_of_sources; link != NULL; link = g_list_next (link)) {
		ESource *source;
		gchar *source_data;
		gchar *uid;

		source = E_SOURCE (link->data);
		uid = e_source_dup_uid (source);

		/* Takes ownership of the UID string. */
		g_hash_table_add (create_context->pending_uids, uid);

		source_data = e_source_to_string (source, NULL);
		g_variant_builder_add (&builder, "{ss}", uid, source_data);
		g_free (source_data);
	}

	variant = g_variant_builder_end (&builder);

	/* Use G_CONNECT_AFTER so source_registry_object_added_cb()
	 * runs first and actually adds the ESource to the internal
	 * hash table before we go quitting our main loop. */
	object_added_id = g_signal_connect_after (
		registry->priv->dbus_object_manager, "object-added",
		G_CALLBACK (source_registry_create_sources_object_added_cb),
		create_context);

	/* This function sinks the floating GVariant reference. */
	e_dbus_source_manager_call_create_sources_sync (
		registry->priv->dbus_source_manager,
		variant, cancellable, &local_error);

	g_variant_builder_clear (&builder);

	/* Wait for an "object-added" signal for each created ESource.
	 * But also set a short timeout to avoid getting stuck here in
	 * case the registry service adds sources to its orphan table,
	 * which prevents them from being exported over D-Bus. */
	if (local_error == NULL) {
		GSource *timeout_source;

		timeout_source = g_timeout_source_new_seconds (2);
		g_source_set_callback (
			timeout_source,
			source_registry_create_sources_main_loop_quit_cb,
			g_main_loop_ref (create_context->main_loop),
			(GDestroyNotify) g_main_loop_unref);
		g_source_attach (timeout_source, create_context->main_context);
		g_source_unref (timeout_source);

		g_main_loop_run (create_context->main_loop);
	}

	g_signal_handler_disconnect (
		registry->priv->dbus_object_manager, object_added_id);

	g_main_context_pop_thread_default (create_context->main_context);
	create_context_free (create_context);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/**
 * e_source_registry_create_sources:
 * @registry: an #ESourceRegistry
 * @list_of_sources: (element-type ESource): a list of #ESource instances with
 * no #GDBusObject
 * @cancellable: (allow-none): optional #GCancellable object, or %NULL
 * @callback: (scope async): a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: (closure): data to pass to the callback function
 *
 * Asynchronously requests the D-Bus service create new key files for each
 * #ESource in @list_of_sources.  Each list element must be a scratch
 * #ESource with no #GDBusObject.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_source_registry_create_sources_finish() to get the result of the
 * operation.
 *
 * Since: 3.6
 **/
void
e_source_registry_create_sources (ESourceRegistry *registry,
                                  GList *list_of_sources,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GList *link;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	/* Verify the list elements are all ESources. */
	for (link = list_of_sources; link != NULL; link = g_list_next (link))
		g_return_if_fail (E_IS_SOURCE (link->data));

	async_context = g_slice_new0 (AsyncContext);
	async_context->list_of_sources = g_list_copy (list_of_sources);

	g_list_foreach (
		async_context->list_of_sources,
		(GFunc) g_object_ref, NULL);

	simple = g_simple_async_result_new (
		G_OBJECT (registry), callback, user_data,
		e_source_registry_create_sources);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, source_registry_create_sources_thread,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);
}

/**
 * e_source_registry_create_sources_finish:
 * @registry: an #ESourceRegistry
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_source_registry_create_sources().
 *
 * If an error occurred, the function will set @error and return %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_source_registry_create_sources_finish (ESourceRegistry *registry,
                                         GAsyncResult *result,
                                         GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (registry),
		e_source_registry_create_sources), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

/**
 * e_source_registry_ref_source:
 * @registry: an #ESourceRegistry
 * @uid: a unique identifier string
 *
 * Looks up an #ESource in @registry by its unique identifier string.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): an #ESource, or %NULL if no match was found
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_source (ESourceRegistry *registry,
                              const gchar *uid)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	return source_registry_sources_lookup (registry, uid);
}

/**
 * e_source_registry_list_sources:
 * @registry: an #ESourceRegistry
 * @extension_name: (allow-none): an extension name, or %NULL
 *
 * Returns a list of registered sources, sorted by display name.  If
 * @extension_name is given, restrict the list to sources having that
 * extension name.
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned list itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: (element-type ESource) (transfer full): a sorted list of sources
 *
 * Since: 3.6
 **/
GList *
e_source_registry_list_sources (ESourceRegistry *registry,
                                const gchar *extension_name)
{
	GList *list, *link;
	GQueue trash = G_QUEUE_INIT;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	list = g_list_sort (
		source_registry_sources_get_values (registry),
		(GCompareFunc) e_source_compare_by_display_name);

	if (extension_name == NULL)
		return list;

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);

		if (!e_source_has_extension (source, extension_name)) {
			g_queue_push_tail (&trash, link);
			g_object_unref (source);
		}
	}

	/* We do want pop_head() here, not pop_head_link(). */
	while ((link = g_queue_pop_head (&trash)) != NULL)
		list = g_list_delete_link (list, link);

	return list;
}

/**
 * e_source_registry_list_enabled:
 * @registry: an #ESourceRegistry
 * @extension_name: (allow-none): an extension name, or %NULL
 *
 * Similar to e_source_registry_list_sources(), but returns only enabled
 * sources according to e_source_registry_check_enabled().
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned list itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: (element-type ESource) (transfer full): a sorted list of sources
 *
 * Since: 3.10
 **/
GList *
e_source_registry_list_enabled (ESourceRegistry *registry,
                                const gchar *extension_name)
{
	GList *list, *link;
	GQueue trash = G_QUEUE_INIT;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	list = e_source_registry_list_sources (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);

		if (!e_source_registry_check_enabled (registry, source)) {
			g_queue_push_tail (&trash, link);
			g_object_unref (source);
		}
	}

	/* We do want pop_head() here, not pop_head_link(). */
	while ((link = g_queue_pop_head (&trash)) != NULL)
		list = g_list_delete_link (list, link);

	return list;
}

/**
 * e_source_registry_find_extension:
 * @registry: an #ESourceRegistry
 * @source: an #ESource
 * @extension_name: the extension name to find
 *
 * Examines @source and its ancestors and returns the "deepest" #ESource
 * having an #ESourceExtension with the given @extension_name.  If neither
 * @source nor any of its ancestors have such an extension, the function
 * returns %NULL.
 *
 * This function is useful in cases when an #ESourceExtension is meant to
 * apply to both the #ESource it belongs to and the #ESource's descendants.
 *
 * A common example is the #ESourceCollection extension, where descendants
 * of an #ESource having an #ESourceCollection extension are implied to be
 * members of that collection.  In that example, this function can be used
 * to test whether @source is a member of a collection.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Note the function returns the #ESource containing the #ESourceExtension
 * instead of the #ESourceExtension itself because extension instances are
 * not to be referenced directly (see e_source_get_extension()).
 *
 * Returns: (transfer full): an #ESource, or %NULL if no match was found
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_find_extension (ESourceRegistry *registry,
                                  ESource *source,
                                  const gchar *extension_name)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	g_object_ref (source);

	while (!e_source_has_extension (source, extension_name)) {
		gchar *uid;

		uid = e_source_dup_parent (source);

		g_object_unref (source);
		source = NULL;

		if (uid != NULL) {
			source = e_source_registry_ref_source (registry, uid);
			g_free (uid);
		}

		if (source == NULL)
			break;
	}

	return source;
}

/**
 * e_source_registry_check_enabled:
 * @registry: an #ESourceRegistry
 * @source: an #ESource
 *
 * Determines whether @source is "effectively" enabled by examining its
 * own #ESource:enabled property as well as those of its ancestors in the
 * #ESource hierarchy.  If all examined #ESource:enabled properties are
 * %TRUE, then the function returns %TRUE.  If any are %FALSE, then the
 * function returns %FALSE.
 *
 * Use this function instead of e_source_get_enabled() to determine
 * things like whether to display an #ESource in a user interface or
 * whether to act on the data set described by the #ESource.
 *
 * Returns: whether @source is "effectively" enabled
 *
 * Since: 3.8
 **/
gboolean
e_source_registry_check_enabled (ESourceRegistry *registry,
                                 ESource *source)
{
	gboolean enabled;
	gchar *parent_uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	enabled = e_source_get_enabled (source);
	parent_uid = e_source_dup_parent (source);

	while (enabled && parent_uid != NULL) {
		ESource *parent;

		parent = e_source_registry_ref_source (registry, parent_uid);

		g_free (parent_uid);
		parent_uid = NULL;

		if (parent != NULL) {
			enabled = e_source_get_enabled (parent);
			parent_uid = e_source_dup_parent (parent);
			g_object_unref (parent);
		}
	}

	g_free (parent_uid);

	return enabled;
}

/* Helper for e_source_registry_build_display_tree() */
static gint
source_registry_compare_nodes (GNode *node_a,
                               GNode *node_b)
{
	ESource *source_a = E_SOURCE (node_a->data);
	ESource *source_b = E_SOURCE (node_b->data);
	const gchar *uid_a, *uid_b;

	uid_a = e_source_get_uid (source_a);
	uid_b = e_source_get_uid (source_b);

	/* Sanity check, with runtime warnings. */
	if (uid_a == NULL) {
		g_warn_if_reached ();
		uid_a = "";
	}
	if (uid_b == NULL) {
		g_warn_if_reached ();
		uid_b = "";
	}

	/* The built-in "local-stub" source comes first at depth 1. */

	if (g_strcmp0 (uid_a, "local-stub") == 0)
		return -1;

	if (g_strcmp0 (uid_b, "local-stub") == 0)
		return 1;

	/* The built-in "system-*" sources come first at depth 2. */

	if (g_str_has_prefix (uid_a, "system-"))
		return -1;

	if (g_str_has_prefix (uid_b, "system-"))
		return 1;

	return e_source_compare_by_display_name (source_a, source_b);
}

/* Helper for e_source_registry_build_display_tree() */
static gboolean
source_registry_prune_nodes (GNode *node,
                             const gchar *extension_name)
{
	GQueue queue = G_QUEUE_INIT;
	GNode *child_node;

	/* Unlink all the child nodes and place them in a queue. */
	while ((child_node = g_node_first_child (node)) != NULL) {
		g_node_unlink (child_node);
		g_queue_push_tail (&queue, child_node);
	}

	/* Sort the queue by source name. */
	g_queue_sort (
		&queue, (GCompareDataFunc)
		source_registry_compare_nodes, NULL);

	/* Pop nodes off the head of the queue until the queue is empty.
	 * If the node has either its own children or the given extension
	 * name, put it back under the parent node (preserving the sorted
	 * order).  Otherwise delete the node and its descendants. */
	while ((child_node = g_queue_pop_head (&queue)) != NULL) {
		ESource *child = E_SOURCE (child_node->data);
		gboolean append_child_node = FALSE;

		if (extension_name == NULL)
			append_child_node = e_source_get_enabled (child);

		else if (e_source_has_extension (child, extension_name))
			append_child_node = e_source_get_enabled (child);

		else if (g_node_first_child (child_node) != NULL)
			append_child_node = e_source_get_enabled (child);

		if (append_child_node)
			g_node_append (node, child_node);
		else
			e_source_registry_free_display_tree (child_node);
	}

	return FALSE;
}

/**
 * e_source_registry_build_display_tree:
 * @registry: an #ESourceRegistry
 * @extension_name: (allow-none): an extension name, or %NULL
 *
 * Returns a single #GNode tree of registered sources that can be used to
 * populate a #GtkTreeModel.  (The root #GNode is just an empty placeholder.)
 *
 * Similar to e_source_registry_list_sources(), an @extension_name can be
 * given to restrict the tree to sources having that extension name.  Parents
 * of matched sources are included in the tree regardless of whether they have
 * an extension named @extension_name.
 *
 * Disabled leaf nodes are automatically excluded from the #GNode tree.
 *
 * The sources returned in the tree are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned tree itself with g_node_destroy().
 * For convenience, e_source_registry_free_display_tree() does all
 * that in one step.
 *
 * Returns: (element-type ESource) (transfer full): a tree of sources,
 *          arranged for display
 *
 * Since: 3.6
 **/
GNode *
e_source_registry_build_display_tree (ESourceRegistry *registry,
                                      const gchar *extension_name)
{
	GNode *root;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	/* Assemble all data sources into a tree. */
	root = source_registry_sources_build_tree (registry);

	/* Prune unwanted nodes from the copied source trees.
	 * This must be done in "post" order (children first)
	 * since it reorders and deletes child nodes. */
	g_node_traverse (
		root, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) source_registry_prune_nodes,
		(gpointer) extension_name);

	return root;
}

/* Helper for e_source_registry_free_display_tree() */
static void
source_registry_unref_nodes (GNode *node)
{
	while (node != NULL) {
		if (node->children != NULL)
			source_registry_unref_nodes (node->children);
		if (node->data != NULL)
			g_object_unref (node->data);
		node = node->next;
	}
}

/**
 * e_source_registry_free_display_tree:
 * @display_tree: a tree of sources, arranged for display
 *
 * Convenience function to free a #GNode tree of registered
 * sources created by e_source_registry_build_display_tree().
 *
 * Since: 3.6
 **/
void
e_source_registry_free_display_tree (GNode *display_tree)
{
	g_return_if_fail (display_tree != NULL);

	/* XXX This would be easier if GLib had something like
	 *     g_node_destroy_full() which took a GDestroyNotify.
	 *     Then the tree would not have to be traversed twice. */

	source_registry_unref_nodes (display_tree);
	g_node_destroy (display_tree);
}

/**
 * e_source_registry_dup_unique_display_name:
 * @registry: an #ESourceRegistry
 * @source: an #ESource
 * @extension_name: (allow-none): an extension name, or %NULL
 *
 * Compares @source's #ESource:display-name against other sources having
 * an #ESourceExtension named @extension_name, if given, or else against
 * all other sources in the @registry.
 *
 * If @sources's #ESource:display-name is unique among these other sources,
 * the function will return the #ESource:display-name verbatim.  Otherwise
 * the function will construct a string that includes the @sources's own
 * #ESource:display-name as well as those of its ancestors.
 *
 * The function's return value is intended to be used in messages shown to
 * the user to help clarify which source is being referred to.  It assumes
 * @source's #ESource:display-name is at least unique among its siblings.
 *
 * Free the returned string with g_free() when finished with it.
 *
 * Returns: a unique display name for @source
 *
 * Since: 3.8
 **/
gchar *
e_source_registry_dup_unique_display_name (ESourceRegistry *registry,
                                           ESource *source,
                                           const gchar *extension_name)
{
	GString *buffer;
	GList *list, *link;
	gchar *display_name;
	gboolean need_clarification = FALSE;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	list = e_source_registry_list_sources (registry, extension_name);

	/* Remove the input source from the list, if present. */
	link = g_list_find (list, source);
	if (link != NULL) {
		g_object_unref (link->data);
		list = g_list_delete_link (list, link);
	}

	/* Now find another source with a matching display name. */
	link = g_list_find_custom (
		list, source, (GCompareFunc)
		e_source_compare_by_display_name);

	need_clarification = (link != NULL);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	list = NULL;

	display_name = e_source_dup_display_name (source);
	buffer = g_string_new (display_name);
	g_free (display_name);

	if (need_clarification) {
		/* Build a list of ancestor sources. */

		g_object_ref (source);

		while (source != NULL) {
			gchar *parent_uid;

			parent_uid = e_source_dup_parent (source);

			g_object_unref (source);
			source = NULL;

			if (parent_uid != NULL) {
				source = e_source_registry_ref_source (
					registry, parent_uid);
				g_free (parent_uid);
			}

			if (source != NULL) {
				g_object_ref (source);
				list = g_list_prepend (list, source);
			}
		}

		/* Display the ancestor names from the most distant
		 * ancestor to the input source's immediate parent. */

		if (list != NULL)
			g_string_append (buffer, " (");

		for (link = list; link != NULL; link = g_list_next (link)) {
			if (link != list)
				g_string_append (buffer, " / ");

			source = E_SOURCE (link->data);
			display_name = e_source_dup_display_name (source);
			g_string_append (buffer, display_name);
			g_free (display_name);
		}

		if (list != NULL)
			g_string_append (buffer, ")");

		g_list_free_full (list, (GDestroyNotify) g_object_unref);
	}

	return g_string_free (buffer, FALSE);
}

/* Helper for e_source_registry_debug_dump() */
static gboolean
source_registry_debug_dump_cb (GNode *node)
{
	guint ii, depth;

	/* Root node is an empty placeholder. */
	if (G_NODE_IS_ROOT (node))
		return FALSE;

	depth = g_node_depth (node);
	for (ii = 2; ii < depth; ii++)
		g_print ("    ");

	if (E_IS_SOURCE (node->data)) {
		ESource *source = E_SOURCE (node->data);
		g_print ("\"%s\" ", e_source_get_display_name (source));
		g_print ("(%s)", e_source_get_uid (source));
	}

	g_print ("\n");

	return FALSE;
}

/**
 * e_source_registry_debug_dump:
 * @registry: an #ESourceRegistry
 * @extension_name: (allow-none): an extension name, or %NULL
 *
 * Handy debugging function that uses e_source_registry_build_display_tree()
 * to print a tree of registered sources to standard output.
 *
 * Since: 3.6
 **/
void
e_source_registry_debug_dump (ESourceRegistry *registry,
                              const gchar *extension_name)
{
	GNode *root;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	root = e_source_registry_build_display_tree (registry, extension_name);

	g_node_traverse (
		root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) source_registry_debug_dump_cb, NULL);

	e_source_registry_free_display_tree (root);
}

/**
 * e_source_registry_ref_builtin_address_book:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in address book #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in address book #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_builtin_address_book (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_ADDRESS_BOOK_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_builtin_calendar:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in calendar #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in calendar #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_builtin_calendar (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_CALENDAR_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_builtin_mail_account:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in mail account #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in mail account #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_builtin_mail_account (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_MAIL_ACCOUNT_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_builtin_memo_list:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in memo list #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in memo list #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_builtin_memo_list (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_MEMO_LIST_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_builtin_proxy:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in proxy profile #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in proxy profile #ESource
 *
 * Since: 3.12
 **/
ESource *
e_source_registry_ref_builtin_proxy (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_PROXY_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_builtin_task_list:
 * @registry: an #ESourceRegistry
 *
 * Returns the built-in task list #ESource.
 *
 * This #ESource is always present and makes for a safe fallback.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the built-in task list #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_builtin_task_list (ESourceRegistry *registry)
{
	ESource *source;
	const gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	uid = E_SOURCE_BUILTIN_TASK_LIST_UID;
	source = e_source_registry_ref_source (registry, uid);
	g_return_val_if_fail (source != NULL, NULL);

	return source;
}

/**
 * e_source_registry_ref_default_address_book:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_address_book() either in this session
 * or a previous session, or else falls back to the built-in address book.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default address book #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_address_book (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_ADDRESS_BOOK_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	/* The built-in source is always present. */
	if (source == NULL)
		source = e_source_registry_ref_builtin_address_book (registry);

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source;
}

/**
 * e_source_registry_set_default_address_book:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): an address book #ESource, or %NULL
 *
 * Sets @default_source as the default address book.  If @default_source
 * is %NULL, the default address book is reset to the built-in address book.
 * This setting will persist across sessions until changed.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_address_book (ESourceRegistry *registry,
                                            ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = E_SOURCE_BUILTIN_ADDRESS_BOOK_UID;
	}

	key = E_SETTINGS_DEFAULT_ADDRESS_BOOK_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/**
 * e_source_registry_ref_default_calendar:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_calendar() either in this session
 * or a previous session, or else falls back to the built-in calendar.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default calendar #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_calendar (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_CALENDAR_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	/* The built-in source is always present. */
	if (source == NULL)
		source = e_source_registry_ref_builtin_calendar (registry);

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source;
}

/**
 * e_source_registry_set_default_calendar:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): a calendar #ESource, or %NULL
 *
 * Sets @default_source as the default calendar.  If @default_source
 * is %NULL, the default calendar is reset to the built-in calendar.
 * This setting will persist across sessions until changed.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_calendar (ESourceRegistry *registry,
                                        ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = E_SOURCE_BUILTIN_CALENDAR_UID;
	}

	key = E_SETTINGS_DEFAULT_CALENDAR_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/**
 * e_source_registry_ref_default_mail_account:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_mail_account() either in this session
 * or a previous session, or else falls back to the built-in mail account.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default mail account #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_mail_account (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_MAIL_ACCOUNT_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	/* The built-in source is always present. */
	if (source == NULL)
		source = e_source_registry_ref_builtin_mail_account (registry);

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source;
}

/**
 * e_source_registry_set_default_mail_account:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): a mail account #ESource, or %NULL
 *
 * Sets @default_source as the default mail account.  If @default_source
 * is %NULL, the default mail account is reset to the built-in mail account.
 * This setting will persist across sessions until changed.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_mail_account (ESourceRegistry *registry,
                                            ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = E_SOURCE_BUILTIN_MAIL_ACCOUNT_UID;
	}

	key = E_SETTINGS_DEFAULT_MAIL_ACCOUNT_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/* Helper for e_source_registry_ref_default_mail_identity() */
static ESource *
source_registry_ref_any_mail_identity (ESourceRegistry *registry)
{
	ESource *source;
	GList *list, *link;
	const gchar *extension_name;
	gchar *uid = NULL;

	/* First fallback: Return the mail identity named
	 *                 by the default mail account. */

	source = e_source_registry_ref_default_mail_account (registry);

	/* This should never be NULL, but just to be safe. */
	if (source != NULL) {
		ESourceMailAccount *extension;

		extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
		extension = e_source_get_extension (source, extension_name);
		uid = e_source_mail_account_dup_identity_uid (extension);

		g_object_unref (source);
		source = NULL;
	}

	if (uid != NULL) {
		source = e_source_registry_ref_source (registry, uid);
		g_free (uid);
	}

	if (source != NULL)
		return source;

	/* Second fallback: Pick any available mail identity,
	 *                  preferring enabled identities. */

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	list = e_source_registry_list_sources (registry, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *candidate = E_SOURCE (link->data);

		if (e_source_registry_check_enabled (registry, candidate)) {
			source = g_object_ref (candidate);
			break;
		}
	}

	if (source == NULL && list != NULL)
		source = g_object_ref (list->data);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return source;
}

/**
 * e_source_registry_ref_default_mail_identity:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_mail_identity() either in this session
 * or a previous session, or else falls back to the mail identity named
 * by the default mail account.  If even that fails it returns any mail
 * identity from @registry, or %NULL if there are none.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default mail identity #ESource, or %NULL
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_mail_identity (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_MAIL_IDENTITY_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	if (source == NULL)
		source = source_registry_ref_any_mail_identity (registry);

	return source;
}

/**
 * e_source_registry_set_default_mail_identity:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): a mail identity #ESource, or %NULL
 *
 * Sets @default_source as the default mail identity.  If @default_source
 * is %NULL, the next request for the default mail identity will use the
 * fallbacks described in e_source_registry_ref_default_mail_identity().
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_mail_identity (ESourceRegistry *registry,
                                             ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = "";  /* no built-in mail identity */
	}

	key = E_SETTINGS_DEFAULT_MAIL_IDENTITY_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/**
 * e_source_registry_ref_default_memo_list:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_memo_list() either in this session
 * or a previous session, or else falls back to the built-in memo list.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default memo list #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_memo_list (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_MEMO_LIST_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	/* The built-in source is always present. */
	if (source == NULL)
		source = e_source_registry_ref_builtin_memo_list (registry);

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source;
}

/**
 * e_source_registry_set_default_memo_list:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): a memo list #ESource, or %NULL
 *
 * Sets @default_source as the default memo list.  If @default_source
 * is %NULL, the default memo list is reset to the built-in memo list.
 * This setting will persist across sessions until changed.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_memo_list (ESourceRegistry *registry,
                                         ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = E_SOURCE_BUILTIN_MEMO_LIST_UID;
	}

	key = E_SETTINGS_DEFAULT_MEMO_LIST_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/**
 * e_source_registry_ref_default_task_list:
 * @registry: an #ESourceRegistry
 *
 * Returns the #ESource most recently passed to
 * e_source_registry_set_default_task_list() either in this session
 * or a previous session, or else falls back to the built-in task list.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default task list #ESource
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_task_list (ESourceRegistry *registry)
{
	const gchar *key;
	ESource *source;
	gchar *uid;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	key = E_SETTINGS_DEFAULT_TASK_LIST_KEY;
	uid = g_settings_get_string (registry->priv->settings, key);
	source = e_source_registry_ref_source (registry, uid);
	g_free (uid);

	/* The built-in source is always present. */
	if (source == NULL)
		source = e_source_registry_ref_builtin_task_list (registry);

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return source;
}

/**
 * e_source_registry_set_default_task_list:
 * @registry: an #ESourceRegistry
 * @default_source: (allow-none): a task list #ESource, or %NULL
 *
 * Sets @default_source as the default task list.  If @default_source
 * is %NULL, the default task list is reset to the built-in task list.
 * This setting will persist across sessions until changed.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_task_list (ESourceRegistry *registry,
                                         ESource *default_source)
{
	const gchar *key;
	const gchar *uid;

	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	if (default_source != NULL) {
		g_return_if_fail (E_IS_SOURCE (default_source));
		uid = e_source_get_uid (default_source);
	} else {
		uid = E_SOURCE_BUILTIN_TASK_LIST_UID;
	}

	key = E_SETTINGS_DEFAULT_TASK_LIST_KEY;
	g_settings_set_string (registry->priv->settings, key, uid);

	/* The GSettings::changed signal will trigger a "notify" signal
	 * from the registry, so no need to call g_object_notify() here. */
}

/**
 * e_source_registry_ref_default_for_extension_name:
 * @registry: an #ESourceRegistry
 * @extension_name: an extension_name
 *
 * This is a convenience function to return a default #ESource based on
 * @extension_name.  This only works with a subset of extension names.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_ADDRESS_BOOK, the function
 * returns the current default address book, or else falls back to the
 * built-in address book.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_CALENDAR, the function returns
 * the current default calendar, or else falls back to the built-in calendar.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MAIL_ACCOUNT, the function
 * returns the current default mail account, or else falls back to the
 * built-in mail account.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MAIL_IDENTITY, the function
 * returns the current default mail identity, or else falls back to the
 * mail identity named by the current default mail account.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MEMO_LIST, the function returns
 * the current default memo list, or else falls back to the built-in memo list.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_TASK_LIST, the function returns
 * the current default task list, or else falls back to the built-in task list.
 *
 * For all other values of @extension_name, the function returns %NULL.
 *
 * The returned #ESource is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): the default #ESource based on @extension_name
 *
 * Since: 3.6
 **/
ESource *
e_source_registry_ref_default_for_extension_name (ESourceRegistry *registry,
                                                  const gchar *extension_name)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);
	g_return_val_if_fail (extension_name != NULL, NULL);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK) == 0)
		return e_source_registry_ref_default_address_book (registry);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0)
		return e_source_registry_ref_default_calendar (registry);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MAIL_ACCOUNT) == 0)
		return e_source_registry_ref_default_mail_account (registry);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MAIL_IDENTITY) == 0)
		return e_source_registry_ref_default_mail_identity (registry);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MEMO_LIST) == 0)
		return e_source_registry_ref_default_memo_list (registry);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0)
		return e_source_registry_ref_default_task_list (registry);

	return NULL;
}

/**
 * e_source_registry_set_default_for_extension_name:
 * @registry: an #ESourceRegistry
 * @extension_name: an extension name
 * @default_source: (allow-none): an #ESource, or %NULL
 *
 * This is a convenience function to set a default #ESource based on
 * @extension_name.  This only works with a subset of extension names.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_ADDRESS_BOOK, the function
 * sets @default_source as the default address book.  If @default_source
 * is %NULL, the default address book is reset to the built-in address book.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_CALENDAR, the function sets
 * @default_source as the default calendar.  If @default_source is %NULL,
 * the default calendar is reset to the built-in calendar.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MAIL_ACCOUNT, the function
 * sets @default_source as the default mail account.  If @default_source
 * is %NULL, the default mail account is reset to the built-in mail account.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MAIL_IDENTITY, the function
 * sets @default_source as the default mail identity.  If @default_source
 * is %NULL, the next request for the default mail identity will return
 * the mail identity named by the default mail account.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_MEMO_LIST, the function sets
 * @default_source as the default memo list.  If @default_source is %NULL,
 * the default memo list is reset to the built-in memo list.
 *
 * If @extension_name is #E_SOURCE_EXTENSION_TASK_LIST, the function sets
 * @default_source as the default task list.  If @default_source is %NULL,
 * the default task list is reset to the built-in task list.
 *
 * For all other values of @extension_name, the function does nothing.
 *
 * Since: 3.6
 **/
void
e_source_registry_set_default_for_extension_name (ESourceRegistry *registry,
                                                  const gchar *extension_name,
                                                  ESource *default_source)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (extension_name != NULL);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_ADDRESS_BOOK) == 0)
		e_source_registry_set_default_address_book (
			registry, default_source);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_CALENDAR) == 0)
		e_source_registry_set_default_calendar (
			registry, default_source);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MAIL_ACCOUNT) == 0)
		e_source_registry_set_default_mail_account (
			registry, default_source);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MAIL_IDENTITY) == 0)
		e_source_registry_set_default_mail_identity (
			registry, default_source);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_MEMO_LIST) == 0)
		e_source_registry_set_default_memo_list (
			registry, default_source);

	if (strcmp (extension_name, E_SOURCE_EXTENSION_TASK_LIST) == 0)
		e_source_registry_set_default_task_list (
			registry, default_source);
}


/*
 * e-collection-backend.c
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
 * SECTION: e-collection-backend
 * @include: libebackend/libebackend.h
 * @short_description: An abstract base class for a data source
 *                     collection backend
 *
 * #ECollectionBackend is an abstract base class for backends which
 * manage a collection of data sources which collectively represent the
 * resources on a remote server.  The resources can include any number
 * of private and shared email stores, calendars and address books.
 *
 * The backend's job is to synchronize local representations of remote
 * resources by adding and removing #EServerSideSource instances in an
 * #ESourceRegistryServer.  If possible the backend should also listen
 * for notifications of newly-added or deleted resources on the remote
 * server or else poll the remote server at regular intervals and then
 * update the data source collection accordingly.
 *
 * As most remote servers require authentication, the backend may also
 * wish to implement the #ESourceAuthenticator interface so it can submit
 * its own #EAuthenticationSession instances to the #ESourceRegistryServer.
 **/

#include "e-collection-backend.h"

#include <libedataserver/libedataserver.h>

#include <libebackend/e-server-side-source.h>
#include <libebackend/e-source-registry-server.h>

#define E_COLLECTION_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_COLLECTION_BACKEND, ECollectionBackendPrivate))

struct _ECollectionBackendPrivate {
	GWeakRef server;
	GQueue children;

	/* Remembers memory-only data source UIDs
	 * based on a server-assigned resource ID. */
	gchar *collection_filename;
	GKeyFile *collection_key_file;
	guint save_collection_idle_id;

	gulong source_added_handler_id;
	gulong source_removed_handler_id;
};

enum {
	PROP_0,
	PROP_SERVER
};

enum {
	CHILD_ADDED,
	CHILD_REMOVED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_ABSTRACT_TYPE (
	ECollectionBackend,
	e_collection_backend,
	E_TYPE_BACKEND)

static void
collection_backend_load_collection_file (ECollectionBackend *backend)
{
	ESource *source;
	const gchar *uid;
	const gchar *cache_dir;
	gchar *dirname;
	gchar *basename;
	gchar *filename;
	GError *error = NULL;

	cache_dir = e_get_user_cache_dir ();
	source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_uid (source);
	dirname = g_build_filename (cache_dir, "sources", NULL);
	basename = g_strconcat (uid, ".collection", NULL);
	filename = g_build_filename (dirname, basename, NULL);
	backend->priv->collection_filename = filename;  /* takes ownership */
	g_mkdir_with_parents (dirname, 0700);
	g_free (basename);
	g_free (dirname);

	g_key_file_load_from_file (
		backend->priv->collection_key_file,
		backend->priv->collection_filename,
		G_KEY_FILE_KEEP_COMMENTS, &error);

	/* Disregard "file not found" errors. */
	if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_error_free (error);
	} else if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}
}

static void
collection_backend_save_collection_file (ECollectionBackend *backend)
{
	gchar *contents;
	gsize length;
	GError *error = NULL;

	contents = g_key_file_to_data (
		backend->priv->collection_key_file, &length, NULL);

	g_file_set_contents (
		backend->priv->collection_filename,
		contents, (gssize) length, &error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	g_free (contents);
}

static gboolean
collection_backend_save_idle_cb (gpointer user_data)
{
	ECollectionBackend *backend;

	backend = E_COLLECTION_BACKEND (user_data);
	collection_backend_save_collection_file (backend);
	backend->priv->save_collection_idle_id = 0;

	return FALSE;
}

static ESource *
collection_backend_register_resource (ECollectionBackend *backend,
                                      const gchar *resource_id,
                                      GError **error)
{
	ESourceRegistryServer *server;
	ESource *source;
	gchar *group_name;
	gchar *uid;

	server = e_collection_backend_ref_server (backend);
	group_name = g_strdup_printf ("Resource %s", resource_id);

	uid = g_key_file_get_string (
		backend->priv->collection_key_file,
		group_name, "SourceUid", NULL);

	/* Verify the UID is not already in use.
	 * If it is, we'll have to pick a new one. */
	if (uid != NULL) {
		source = e_source_registry_server_ref_source (server, uid);
		if (source != NULL) {
			g_object_unref (source);
			g_free (uid);
			uid = NULL;
		}
	}

	if (uid == NULL)
		uid = e_uid_new ();

	g_key_file_set_string (
		backend->priv->collection_key_file,
		group_name, "SourceUid", uid);

	if (backend->priv->save_collection_idle_id == 0) {
		guint idle_id;

		idle_id = g_idle_add_full (
			G_PRIORITY_DEFAULT_IDLE,
			collection_backend_save_idle_cb,
			g_object_ref (backend),
			(GDestroyNotify) g_object_unref);
		backend->priv->save_collection_idle_id = idle_id;
	}

	source = e_server_side_source_new_memory_only (server, uid, error);

	g_free (uid);
	g_free (group_name);
	g_object_unref (server);

	return source;
}

static gboolean
collection_backend_child_is_calendar (ESource *child_source)
{
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	return FALSE;
}

static gboolean
collection_backend_child_is_contacts (ESource *child_source)
{
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	return FALSE;
}

static gboolean
collection_backend_child_is_mail (ESource *child_source)
{
	const gchar *extension_name;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	if (e_source_has_extension (child_source, extension_name))
		return TRUE;

	return FALSE;
}

static void
collection_backend_bind_child_enabled (ECollectionBackend *backend,
                                       ESource *child_source)
{
	ESource *collection_source;
	ESourceCollection *extension;
	const gchar *extension_name;

	/* See if the child source's "enabled" property can be
	 * bound to any ESourceCollection "enabled" properties. */

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	collection_source = e_backend_get_source (E_BACKEND (backend));
	extension = e_source_get_extension (collection_source, extension_name);

	if (collection_backend_child_is_calendar (child_source)) {
		g_object_bind_property (
			extension, "calendar-enabled",
			child_source, "enabled",
			G_BINDING_SYNC_CREATE);
		return;
	}

	if (collection_backend_child_is_contacts (child_source)) {
		g_object_bind_property (
			extension, "contacts-enabled",
			child_source, "enabled",
			G_BINDING_SYNC_CREATE);
		return;
	}

	if (collection_backend_child_is_mail (child_source)) {
		g_object_bind_property (
			extension, "mail-enabled",
			child_source, "enabled",
			G_BINDING_SYNC_CREATE);
		return;
	}
}

static void
collection_backend_source_added_cb (ESourceRegistryServer *server,
                                    ESource *source,
                                    ECollectionBackend *backend)
{
	ESource *collection_source;
	ESource *parent_source;
	const gchar *uid;

	/* If the newly-added source is our own child, emit "child-added". */

	collection_source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_parent (source);
	if (uid == NULL)
		return;

	parent_source = e_source_registry_server_ref_source (server, uid);
	g_return_if_fail (parent_source != NULL);

	if (e_source_equal (collection_source, parent_source))
		g_signal_emit (backend, signals[CHILD_ADDED], 0, source);

	g_object_unref (parent_source);
}

static void
collection_backend_source_removed_cb (ESourceRegistryServer *server,
                                      ESource *source,
                                      ECollectionBackend *backend)
{
	ESource *collection_source;
	ESource *parent_source;
	const gchar *uid;

	/* If the removed source was our own child, emit "child-removed".
	 * Note that the source is already unlinked from the GNode tree. */

	collection_source = e_backend_get_source (E_BACKEND (backend));

	uid = e_source_get_parent (source);
	if (uid == NULL)
		return;

	parent_source = e_source_registry_server_ref_source (server, uid);
	g_return_if_fail (parent_source != NULL);

	if (e_source_equal (collection_source, parent_source))
		g_signal_emit (backend, signals[CHILD_REMOVED], 0, source);

	g_object_unref (parent_source);
}

static gboolean
collection_backend_populate_idle_cb (gpointer user_data)
{
	ECollectionBackend *backend;
	ECollectionBackendClass *class;

	backend = E_COLLECTION_BACKEND (user_data);

	class = E_COLLECTION_BACKEND_GET_CLASS (backend);
	g_return_val_if_fail (class->populate != NULL, FALSE);

	class->populate (backend);

	return FALSE;
}

static void
collection_backend_set_server (ECollectionBackend *backend,
                               ESourceRegistryServer *server)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));

	g_weak_ref_set (&backend->priv->server, server);
}

static void
collection_backend_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SERVER:
			collection_backend_set_server (
				E_COLLECTION_BACKEND (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
collection_backend_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SERVER:
			g_value_take_object (
				value,
				e_collection_backend_ref_server (
				E_COLLECTION_BACKEND (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
collection_backend_dispose (GObject *object)
{
	ECollectionBackendPrivate *priv;
	ESourceRegistryServer *server;

	priv = E_COLLECTION_BACKEND_GET_PRIVATE (object);

	server = g_weak_ref_get (&priv->server);
	if (server != NULL) {
		g_signal_handler_disconnect (
			server, priv->source_added_handler_id);
		g_signal_handler_disconnect (
			server, priv->source_removed_handler_id);
		g_weak_ref_set (&priv->server, NULL);
		g_object_unref (server);
	}

	while (!g_queue_is_empty (&priv->children))
		g_object_unref (g_queue_pop_head (&priv->children));

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_collection_backend_parent_class)->dispose (object);
}

static void
collection_backend_finalize (GObject *object)
{
	ECollectionBackendPrivate *priv;

	priv = E_COLLECTION_BACKEND_GET_PRIVATE (object);

	g_free (priv->collection_filename);
	g_key_file_free (priv->collection_key_file);

	/* The idle source ID should be zero since the idle
	 * source itself holds a reference on the backend. */
	g_warn_if_fail (priv->save_collection_idle_id == 0);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_collection_backend_parent_class)->finalize (object);
}

static void
collection_backend_constructed (GObject *object)
{
	ECollectionBackend *backend;
	ESourceRegistryServer *server;
	ESource *source;
	GNode *node;
	gulong handler_id;

	backend = E_COLLECTION_BACKEND (object);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_collection_backend_parent_class)->
		constructed (object);

	collection_backend_load_collection_file (backend);

	/* Emit "child-added" signals for the children we already have. */

	source = e_backend_get_source (E_BACKEND (backend));
	node = e_server_side_source_get_node (E_SERVER_SIDE_SOURCE (source));
	node = g_node_first_child (node);

	while (node != NULL) {
		ESource *child = E_SOURCE (node->data);
		g_signal_emit (backend, signals[CHILD_ADDED], 0, child);
		node = g_node_next_sibling (node);
	}

	/* Listen for "source-added" and "source-removed" signals
	 * from the server, which may trigger our own "child-added"
	 * and "child-removed" signals. */

	server = e_collection_backend_ref_server (backend);

	handler_id = g_signal_connect (
		server, "source-added",
		G_CALLBACK (collection_backend_source_added_cb), backend);

	backend->priv->source_added_handler_id = handler_id;

	handler_id = g_signal_connect (
		server, "source-removed",
		G_CALLBACK (collection_backend_source_removed_cb), backend);

	backend->priv->source_removed_handler_id = handler_id;

	g_object_unref (server);

	/* Populate the newly-added collection from an idle callback
	 * so persistent child sources have a chance to be added first. */

	g_idle_add_full (
		G_PRIORITY_LOW,
		collection_backend_populate_idle_cb,
		g_object_ref (backend),
		(GDestroyNotify) g_object_unref);
}

static void
collection_backend_populate (ECollectionBackend *backend)
{
	/* Placeholder so subclasses can safely chain up. */
}

static void
collection_backend_child_added (ECollectionBackend *backend,
                                ESource *child_source)
{
	collection_backend_bind_child_enabled (backend, child_source);

	g_queue_push_tail (
		&backend->priv->children,
		g_object_ref (child_source));

	/* Collection children are not removable. */
	e_server_side_source_set_removable (
		E_SERVER_SIDE_SOURCE (child_source), FALSE);
}

static void
collection_backend_child_removed (ECollectionBackend *backend,
                                  ESource *child_source)
{
	if (g_queue_remove (&backend->priv->children, child_source))
		g_object_unref (child_source);
}

static void
e_collection_backend_class_init (ECollectionBackendClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ECollectionBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = collection_backend_set_property;
	object_class->get_property = collection_backend_get_property;
	object_class->dispose = collection_backend_dispose;
	object_class->finalize = collection_backend_finalize;
	object_class->constructed = collection_backend_constructed;

	class->populate = collection_backend_populate;
	class->child_added = collection_backend_child_added;
	class->child_removed = collection_backend_child_removed;

	g_object_class_install_property (
		object_class,
		PROP_SERVER,
		g_param_spec_object (
			"server",
			"Server",
			"The server to which the backend belongs",
			E_TYPE_SOURCE_REGISTRY_SERVER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/**
	 * ECollectionBackend::child-added:
	 * @backend: the #ECollectionBackend which emitted the signal
	 * @child_source: the newly-added child #EServerSideSource
	 *
	 * Emitted when an #EServerSideSource is added to @backend's
	 * #ECollectionBackend:server as a child of @backend's collection
	 * #EBackend:source.
	 *
	 * You can think of this as a filtered version of
	 * #ESourceRegistryServer's #ESourceRegistryServer::source-added
	 * signal which only lets through sources relevant to @backend.
	 **/
	signals[CHILD_ADDED] = g_signal_new (
		"child-added",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ECollectionBackendClass, child_added),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SERVER_SIDE_SOURCE);

	/**
	 * ECollectionBackend::child-removed:
	 * @backend: the #ECollectionBackend which emitted the signal
	 * @child_source: the child #EServerSideSource that got removed
	 *
	 * Emitted when an #EServerSideSource that is a child of
	 * @backend's collection #EBackend:source is removed from
	 * @backend's #ECollectionBackend:server.
	 *
	 * You can think of this as a filtered version of
	 * #ESourceRegistryServer's #ESourceRegistryServer::source-removed
	 * signal which only lets through sources relevant to @backend.
	 **/
	signals[CHILD_REMOVED] = g_signal_new (
		"child-removed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (ECollectionBackendClass, child_removed),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		E_TYPE_SERVER_SIDE_SOURCE);
}

static void
e_collection_backend_init (ECollectionBackend *backend)
{
	backend->priv = E_COLLECTION_BACKEND_GET_PRIVATE (backend);
	backend->priv->collection_key_file = g_key_file_new ();
}

/**
 * e_collection_backend_new_child:
 * @backend: an #ECollectionBackend
 * @resource_id: a stable and unique resource ID
 *
 * Creates a new memory-only #EServerSideSource as a child of the collection
 * #EBackend:source owned by @backend.  If possible, the #EServerSideSource
 * is assigned a previously used #ESource:uid based on @resource_id so that
 * locally cached data can be reused.
 *
 * The returned data source should be passed to
 * e_source_registry_server_add_source() to export it over D-Bus.
 *
 * Return: a newly-created data source
 *
 * Since: 3.6
 **/
ESource *
e_collection_backend_new_child (ECollectionBackend *backend,
                                const gchar *resource_id)
{
	ESource *collection_source;
	ESource *child_source;
	const gchar *collection_uid;
	GError *error = NULL;

	g_return_val_if_fail (E_IS_COLLECTION_BACKEND (backend), NULL);
	g_return_val_if_fail (resource_id != NULL, NULL);

	/* This being a memory-only data source, creating the instance
	 * should never fail but we'll check for errors just the same.
	 * It's unlikely enough that we don't need a GError parameter. */
	child_source = collection_backend_register_resource (
		backend, resource_id, &error);

	if (error != NULL) {
		g_warn_if_fail (child_source == NULL);
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
		return NULL;
	}

	collection_source = e_backend_get_source (E_BACKEND (backend));
	collection_uid = e_source_get_uid (collection_source);
	e_source_set_parent (child_source, collection_uid);

	g_print ("%s: Pairing %s with resource %s\n",
		e_source_get_display_name (collection_source),
		e_source_get_uid (child_source), resource_id);

	return child_source;
}

/**
 * e_collection_backend_ref_server:
 * @backend: an #ECollectionBackend
 *
 * Returns the #ESourceRegistryServer to which @backend belongs.
 *
 * The returned #ESourceRegistryServer is referenced for thread-safety.
 * Unreference the #ESourceRegistryServer with g_object_unref() when
 * finished with it.
 *
 * Returns: the #ESourceRegisterServer for @backend
 *
 * Since: 3.6
 **/
ESourceRegistryServer *
e_collection_backend_ref_server (ECollectionBackend *backend)
{
	g_return_val_if_fail (E_IS_COLLECTION_BACKEND (backend), NULL);

	return g_weak_ref_get (&backend->priv->server);
}

/**
 * e_collection_backend_list_calendar_sources:
 * @backend: an #ECollectionBackend
 *
 * Returns a list of calendar sources belonging to the data source
 * collection managed by @backend.
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned #GList itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a list of calendar sources
 *
 * Since: 3.6
 **/
GList *
e_collection_backend_list_calendar_sources (ECollectionBackend *backend)
{
	GList *result_list = NULL;
	GList *list, *link;

	g_return_val_if_fail (E_IS_COLLECTION_BACKEND (backend), NULL);

	list = g_queue_peek_head_link (&backend->priv->children);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *child_source = E_SOURCE (link->data);
		if (collection_backend_child_is_calendar (child_source))
			result_list = g_list_prepend (
				result_list, g_object_ref (child_source));
	}

	return g_list_reverse (result_list);
}

/**
 * e_collection_backend_list_contacts_sources:
 * @backend: an #ECollectionBackend
 *
 * Returns a list of address book sources belonging to the data source
 * collection managed by @backend.
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned #GList itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a list of address book sources
 *
 * Since: 3.6
 **/
GList *
e_collection_backend_list_contacts_sources (ECollectionBackend *backend)
{
	GList *result_list = NULL;
	GList *list, *link;

	g_return_val_if_fail (E_IS_COLLECTION_BACKEND (backend), NULL);

	list = g_queue_peek_head_link (&backend->priv->children);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *child_source = E_SOURCE (link->data);
		if (collection_backend_child_is_contacts (child_source))
			result_list = g_list_prepend (
				result_list, g_object_ref (child_source));
	}

	return g_list_reverse (result_list);
}

/**
 * e_collection_backend_list_mail_sources:
 * @backend: an #ECollectionBackend
 *
 * Returns a list of mail sources belonging to the data source collection
 * managed by @backend.
 *
 * The sources returned in the list are referenced for thread-safety.
 * They must each be unreferenced with g_object_unref() when finished
 * with them.  Free the returned #GList itself with g_list_free().
 *
 * An easy way to free the list properly in one step is as follows:
 *
 * |[
 *   g_list_free_full (list, g_object_unref);
 * ]|
 *
 * Returns: a list of mail sources
 *
 * Since: 3.6
 **/
GList *
e_collection_backend_list_mail_sources (ECollectionBackend *backend)
{
	GList *result_list = NULL;
	GList *list, *link;

	g_return_val_if_fail (E_IS_COLLECTION_BACKEND (backend), NULL);

	list = g_queue_peek_head_link (&backend->priv->children);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *child_source = E_SOURCE (link->data);
		if (collection_backend_child_is_mail (child_source))
			result_list = g_list_prepend (
				result_list, g_object_ref (child_source));
	}

	return g_list_reverse (result_list);
}


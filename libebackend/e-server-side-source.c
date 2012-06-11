/*
 * e-server-side-source.c
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
 * SECTION: e-server-side-source
 * @include: libebackend/libebackend.h
 * @short_description: A server-side data source
 *
 * An #EServerSideSource is an #ESource with some additional capabilities
 * exclusive to the registry D-Bus service.
 **/

#include "e-server-side-source.h"

#include <config.h>
#include <glib/gi18n-lib.h>

/* Private D-Bus classes. */
#include <e-dbus-source.h>

#define E_SERVER_SIDE_SOURCE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SERVER_SIDE_SOURCE, EServerSideSourcePrivate))

#define DBUS_OBJECT_PATH	E_SOURCE_REGISTRY_SERVER_OBJECT_PATH "/Source"

#define PRIMARY_GROUP_NAME	"Data Source"

typedef struct _AsyncClosure AsyncClosure;

struct _EServerSideSourcePrivate {
	gpointer server;  /* weak pointer */

	GNode node;
	GFile *file;
	gchar *uid;

	/* For comparison. */
	gchar *file_contents;

	gboolean allow_auth_prompt;
	gchar *write_directory;
};

struct _AsyncClosure {
	GMainLoop *loop;
	GMainContext *context;
	GAsyncResult *result;
};

enum {
	PROP_0,
	PROP_ALLOW_AUTH_PROMPT,
	PROP_FILE,
	PROP_REMOVABLE,
	PROP_SERVER,
	PROP_UID,
	PROP_WRITABLE,
	PROP_WRITE_DIRECTORY
};

static GInitableIface *initable_parent_interface;

/* Forward Declarations */
static void	e_server_side_source_initable_init
						(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EServerSideSource,
	e_server_side_source,
	E_TYPE_SOURCE,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_server_side_source_initable_init))

static AsyncClosure *
async_closure_new (void)
{
	AsyncClosure *closure;

	closure = g_slice_new0 (AsyncClosure);
	closure->context = g_main_context_new ();
	closure->loop = g_main_loop_new (closure->context, FALSE);

	g_main_context_push_thread_default (closure->context);

	return closure;
}

static GAsyncResult *
async_closure_wait (AsyncClosure *closure)
{
	g_main_loop_run (closure->loop);

	return closure->result;
}

static void
async_closure_free (AsyncClosure *closure)
{
	g_main_context_pop_thread_default (closure->context);

	g_main_loop_unref (closure->loop);
	g_main_context_unref (closure->context);

	if (closure->result != NULL)
		g_object_unref (closure->result);

	g_slice_free (AsyncClosure, closure);
}

static void
async_closure_callback (GObject *object,
                        GAsyncResult *result,
                        gpointer user_data)
{
	AsyncClosure *closure = user_data;

	/* Replace any previous result. */
	if (closure->result != NULL)
		g_object_unref (closure->result);
	closure->result = g_object_ref (result);

	g_main_loop_quit (closure->loop);
}

static gboolean
server_side_source_parse_data (GKeyFile *key_file,
                               const gchar *data,
                               gsize length,
                               GError **error)
{
	gboolean success;

	success = g_key_file_load_from_data (
		key_file, data, length, G_KEY_FILE_NONE, error);

	if (!success)
		return FALSE;

	/* Make sure the key file has a [Data Source] group. */
	if (!g_key_file_has_group (key_file, PRIMARY_GROUP_NAME)) {
		g_set_error (
			error, G_KEY_FILE_ERROR,
			G_KEY_FILE_ERROR_GROUP_NOT_FOUND,
			_("Data source is missing a [%s] group"),
			PRIMARY_GROUP_NAME);
		return FALSE;
	}

	return TRUE;
}

static void
server_side_source_print_diff (ESource *source,
                               const gchar *old_data,
                               const gchar *new_data)
{
	gchar **old_strv = NULL;
	gchar **new_strv = NULL;
	guint old_length = 0;
	guint new_length = 0;
	guint ii;

	g_print ("Saving %s\n", e_source_get_uid (source));

	if (old_data != NULL) {
		old_strv = g_strsplit (old_data, "\n", 0);
		old_length = g_strv_length (old_strv);
	}

	if (new_data != NULL) {
		new_strv = g_strsplit (new_data, "\n", 0);
		new_length = g_strv_length (new_strv);
	}

	for (ii = 0; ii < MIN (old_length, new_length); ii++) {
		if (g_strcmp0 (old_strv[ii], new_strv[ii]) != 0) {
			g_print (" - : %s\n", old_strv[ii]);
			g_print (" + : %s\n", new_strv[ii]);
		} else {
			g_print ("   : %s\n", old_strv[ii]);
		}
	}

	for (; ii < old_length; ii++)
		g_print (" - : %s\n", old_strv[ii]);

	for (; ii < new_length; ii++)
		g_print (" + : %s\n", new_strv[ii]);

	g_strfreev (old_strv);
	g_strfreev (new_strv);
}

static gboolean
server_side_source_traverse_cb (GNode *node,
                                GQueue *queue)
{
	g_queue_push_tail (queue, g_object_ref (node->data));

	return FALSE;
}

static gboolean
server_side_source_allow_auth_prompt_cb (EDBusSource *interface,
                                         GDBusMethodInvocation *invocation,
                                         EServerSideSource *source)
{
	e_server_side_source_set_allow_auth_prompt (source, TRUE);

	e_dbus_source_complete_allow_auth_prompt (interface, invocation);

	return TRUE;
}

static gboolean
server_side_source_remove_cb (EDBusSourceRemovable *interface,
                              GDBusMethodInvocation *invocation,
                              EServerSideSource *source)
{
	GError *error = NULL;

	/* Note we don't need to verify the source is removable here
	 * since if it isn't, the remove() method won't be available.
	 * Descendants of the source are removed whether they export
	 * a remove() method or not. */

	e_source_remove_sync (E_SOURCE (source), NULL, &error);

	if (error != NULL)
		g_dbus_method_invocation_take_error (invocation, error);
	else
		e_dbus_source_removable_complete_remove (
			interface, invocation);

	return TRUE;
}

static gboolean
server_side_source_write_cb (EDBusSourceWritable *interface,
                             GDBusMethodInvocation *invocation,
                             const gchar *data,
                             ESource *source)
{
	GKeyFile *key_file;
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	GError *error = NULL;

	/* Note we don't need to verify the source is writable here
	 * since if it isn't, the write() method won't be available. */

	dbus_object = e_source_ref_dbus_object (source);
	dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));

	/* Validate the raw data before making the changes live. */
	key_file = g_key_file_new ();
	server_side_source_parse_data (key_file, data, strlen (data), &error);
	g_key_file_free (key_file);

	/* Q: How does this trigger data being written to disk?
	 *
	 * A: Here's the sequence of events:
	 *
	 *    1) We set the EDBusSource:data property.
	 *    2) ESource picks up the "notify::data" signal and parses
	 *       the raw data, which triggers an ESource:changed signal.
	 *    3) Our changed() method schedules an idle callback.
	 *    4) The idle callback calls e_source_write_sync().
	 *    5) e_source_write_sync() calls e_dbus_source_dup_data()
	 *       and synchronously writes the resulting string to disk.
	 */

	if (error == NULL)
		e_dbus_source_set_data (dbus_source, data);

	if (error != NULL)
		g_dbus_method_invocation_take_error (invocation, error);
	else
		e_dbus_source_writable_complete_write (
			interface, invocation);

	g_object_unref (dbus_source);
	g_object_unref (dbus_object);

	return TRUE;
}

static void
server_side_source_set_file (EServerSideSource *source,
                             GFile *file)
{
	g_return_if_fail (file == NULL || G_IS_FILE (file));
	g_return_if_fail (source->priv->file == NULL);

	if (file != NULL)
		source->priv->file = g_object_ref (file);
}

static void
server_side_source_set_server (EServerSideSource *source,
                               ESourceRegistryServer *server)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (source->priv->server == NULL);

	source->priv->server = server;

	g_object_add_weak_pointer (
		G_OBJECT (server), &source->priv->server);
}

static void
server_side_source_set_uid (EServerSideSource *source,
                            const gchar *uid)
{
	g_return_if_fail (source->priv->uid == NULL);

	/* It's okay for this to be NULL, in fact if we're given a
	 * GFile the UID is derived from its basename anyway.  This
	 * is just for memory-only sources in a collection backend,
	 * which don't have a GFile. */
	source->priv->uid = g_strdup (uid);
}

static void
server_side_source_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ALLOW_AUTH_PROMPT:
			e_server_side_source_set_allow_auth_prompt (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILE:
			server_side_source_set_file (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_object (value));
			return;

		case PROP_REMOVABLE:
			e_server_side_source_set_removable (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_boolean (value));
			return;

		case PROP_SERVER:
			server_side_source_set_server (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_object (value));
			return;

		case PROP_UID:
			server_side_source_set_uid (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_string (value));
			return;

		case PROP_WRITABLE:
			e_server_side_source_set_writable (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_boolean (value));
			return;

		case PROP_WRITE_DIRECTORY:
			e_server_side_source_set_write_directory (
				E_SERVER_SIDE_SOURCE (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
server_side_source_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ALLOW_AUTH_PROMPT:
			g_value_set_boolean (
				value,
				e_server_side_source_get_allow_auth_prompt (
				E_SERVER_SIDE_SOURCE (object)));
			return;

		case PROP_FILE:
			g_value_set_object (
				value,
				e_server_side_source_get_file (
				E_SERVER_SIDE_SOURCE (object)));
			return;

		case PROP_REMOVABLE:
			g_value_set_boolean (
				value,
				e_source_get_removable (
				E_SOURCE (object)));
			return;

		case PROP_SERVER:
			g_value_set_object (
				value,
				e_server_side_source_get_server (
				E_SERVER_SIDE_SOURCE (object)));
			return;

		case PROP_UID:
			g_value_take_string (
				value,
				e_source_dup_uid (
				E_SOURCE (object)));
			return;

		case PROP_WRITABLE:
			g_value_set_boolean (
				value,
				e_source_get_writable (
				E_SOURCE (object)));
			return;

		case PROP_WRITE_DIRECTORY:
			g_value_set_string (
				value,
				e_server_side_source_get_write_directory (
				E_SERVER_SIDE_SOURCE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
server_side_source_dispose (GObject *object)
{
	EServerSideSourcePrivate *priv;

	priv = E_SERVER_SIDE_SOURCE_GET_PRIVATE (object);

	if (priv->server != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->server), &priv->server);
		priv->server = NULL;
	}

	if (priv->file != NULL) {
		g_object_unref (priv->file);
		priv->file = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_server_side_source_parent_class)->dispose (object);
}

static void
server_side_source_finalize (GObject *object)
{
	EServerSideSourcePrivate *priv;

	priv = E_SERVER_SIDE_SOURCE_GET_PRIVATE (object);

	g_node_unlink (&priv->node);

	g_free (priv->uid);
	g_free (priv->file_contents);
	g_free (priv->write_directory);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_server_side_source_parent_class)->finalize (object);
}

static void
server_side_source_changed (ESource *source)
{
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	gchar *old_data;
	gchar *new_data;
	GError *error = NULL;

	dbus_object = e_source_ref_dbus_object (source);
	dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));

	old_data = e_dbus_source_dup_data (dbus_source);
	new_data = e_source_to_string (source, NULL);

	/* Setting the "data" property triggers the ESource::changed,
	 * signal, which invokes this callback, which sets the "data"
	 * property, etc.  This breaks an otherwise infinite loop. */
	if (g_strcmp0 (old_data, new_data) != 0)
		e_dbus_source_set_data (dbus_source, new_data);

	g_free (old_data);
	g_free (new_data);

	g_object_unref (dbus_source);
	g_object_unref (dbus_object);

	/* This writes the "data" property to disk. */
	e_source_write_sync (source, NULL, &error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}
}

static gboolean
server_side_source_remove_sync (ESource *source,
                                GCancellable *cancellable,
                                GError **error)
{
	AsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = async_closure_new ();

	e_source_remove (
		source, cancellable, async_closure_callback, closure);

	result = async_closure_wait (closure);

	success = e_source_remove_finish (source, result, error);

	async_closure_free (closure);

	return success;
}

static void
server_side_source_remove (ESource *source,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	EServerSideSourcePrivate *priv;
	GSimpleAsyncResult *simple;
	ESourceRegistryServer *server;
	GQueue queue = G_QUEUE_INIT;
	GList *list, *link;
	gboolean success = TRUE;
	GError *error = NULL;

	/* XXX Yes we block here.  We do this operation
	 *     synchronously to keep the server code simple. */

	priv = E_SERVER_SIDE_SOURCE_GET_PRIVATE (source);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback, user_data,
		server_side_source_remove);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	/* Collect the source and its descendants into a queue.
	 * Do this before unexporting so we hold references to
	 * all the removed sources. */
	g_node_traverse (
		&priv->node, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) server_side_source_traverse_cb, &queue);

	/* Unexport the object and its descendants. */
	server = E_SOURCE_REGISTRY_SERVER (priv->server);
	e_source_registry_server_remove_source (server, source);

	list = g_queue_peek_head_link (&queue);

	/* Delete the key file for each source in the queue. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		EServerSideSource *child;
		GFile *file;

		child = E_SERVER_SIDE_SOURCE (link->data);
		file = e_server_side_source_get_file (child);

		if (file != NULL)
			success = g_file_delete (file, cancellable, &error);

		if (!success)
			goto exit;
	}

exit:
	while (!g_queue_is_empty (&queue))
		g_object_unref (g_queue_pop_head (&queue));

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete_in_idle (simple);
	g_object_unref (simple);
}

static gboolean
server_side_source_remove_finish (ESource *source,
                                  GAsyncResult *result,
                                  GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source),
		server_side_source_remove), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
server_side_source_write_sync (ESource *source,
                               GCancellable *cancellable,
                               GError **error)
{
	AsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = async_closure_new ();

	e_source_write (
		source, cancellable, async_closure_callback, closure);

	result = async_closure_wait (closure);

	success = e_source_write_finish (source, result, error);

	async_closure_free (closure);

	return success;
}

static void
server_side_source_write (ESource *source,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
	EServerSideSourcePrivate *priv;
	GSimpleAsyncResult *simple;
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	gboolean replace_file;
	const gchar *old_data;
	gchar *new_data;
	GError *error = NULL;

	/* XXX Yes we block here.  We do this operation
	 *     synchronously to keep the server code simple. */

	priv = E_SERVER_SIDE_SOURCE_GET_PRIVATE (source);

	simple = g_simple_async_result_new (
		G_OBJECT (source), callback, user_data,
		server_side_source_write);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	dbus_object = e_source_ref_dbus_object (source);
	dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));

	old_data = priv->file_contents;
	new_data = e_source_to_string (source, NULL);

	/* When writing source data to disk, we always write to the
	 * source's specified "write-directory" even if the key file
	 * was originally read from a different directory.  To avoid
	 * polluting the write directory with key files identical to
	 * the original, we check that the data has actually changed
	 * before writing a copy to disk. */

	replace_file =
		G_IS_FILE (priv->file) &&
		(g_strcmp0 (old_data, new_data) != 0);

	if (replace_file) {
		GFile *file;
		gchar *filename;
		gchar *uid;

		g_warn_if_fail (priv->write_directory != NULL);

		uid = e_server_side_source_uid_from_file (priv->file, NULL);
		g_warn_if_fail (uid != NULL);  /* this should never fail */
		filename = g_build_filename (priv->write_directory, uid, NULL);
		file = g_file_new_for_path (filename);
		g_free (filename);
		g_free (uid);

		if (!g_file_equal (file, priv->file)) {
			g_object_unref (priv->file);
			priv->file = g_object_ref (file);
		}

		server_side_source_print_diff (source, old_data, new_data);

		g_file_replace_contents (
			file, new_data, strlen (new_data), NULL, FALSE,
			G_FILE_CREATE_NONE, NULL, cancellable, &error);

		if (error == NULL) {
			g_free (priv->file_contents);
			priv->file_contents = new_data;
			new_data = NULL;
		}

		g_object_unref (file);
	}

	g_free (new_data);

	g_object_unref (dbus_source);
	g_object_unref (dbus_object);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);

	g_simple_async_result_complete_in_idle (simple);
	g_object_unref (simple);
}

static gboolean
server_side_source_write_finish (ESource *source,
                                 GAsyncResult *result,
                                 GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (source),
		server_side_source_write), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static gboolean
server_side_source_initable_init (GInitable *initable,
                                  GCancellable *cancellable,
                                  GError **error)
{
	EServerSideSource *source;
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	GFile *file;

	source = E_SERVER_SIDE_SOURCE (initable);
	file = e_server_side_source_get_file (source);

	if (file != NULL) {
		g_warn_if_fail (source->priv->uid == NULL);
		source->priv->uid =
			e_server_side_source_uid_from_file (file, error);
		if (source->priv->uid == NULL)
			return FALSE;
	}

	if (source->priv->uid == NULL)
		source->priv->uid = e_uid_new ();

	dbus_source = e_dbus_source_skeleton_new ();

	e_dbus_source_set_uid (dbus_source, source->priv->uid);

	dbus_object = e_source_ref_dbus_object (E_SOURCE (source));
	e_dbus_object_skeleton_set_source (
		E_DBUS_OBJECT_SKELETON (dbus_object), dbus_source);
	g_object_unref (dbus_object);

	g_signal_connect (
		dbus_source, "handle-allow-auth-prompt",
		G_CALLBACK (server_side_source_allow_auth_prompt_cb), source);

	g_object_unref (dbus_source);

	if (!e_server_side_source_load (source, cancellable, error))
		return FALSE;

	/* Chain up to parent interface's init() method. */
	return initable_parent_interface->init (initable, cancellable, error);
}

static void
e_server_side_source_class_init (EServerSideSourceClass *class)
{
	GObjectClass *object_class;
	ESourceClass *source_class;

	g_type_class_add_private (class, sizeof (EServerSideSourcePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = server_side_source_set_property;
	object_class->get_property = server_side_source_get_property;
	object_class->dispose = server_side_source_dispose;
	object_class->finalize = server_side_source_finalize;

	source_class = E_SOURCE_CLASS (class);
	source_class->changed = server_side_source_changed;
	source_class->remove_sync = server_side_source_remove_sync;
	source_class->remove = server_side_source_remove;
	source_class->remove_finish = server_side_source_remove_finish;
	source_class->write_sync = server_side_source_write_sync;
	source_class->write = server_side_source_write;
	source_class->write_finish = server_side_source_write_finish;

	g_object_class_install_property (
		object_class,
		PROP_ALLOW_AUTH_PROMPT,
		g_param_spec_boolean (
			"allow-auth-prompt",
			"Allow Auth Prompt",
			"Whether authentication sessions may "
			"interrupt the user for a password",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILE,
		g_param_spec_object (
			"file",
			"File",
			"The key file for the data source",
			G_TYPE_FILE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/* This overrides the "removable" property
	 * in ESourceClass with a writable version. */
	g_object_class_install_property (
		object_class,
		PROP_REMOVABLE,
		g_param_spec_boolean (
			"removable",
			"Removable",
			"Whether the data source is removable",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_SERVER,
		g_param_spec_object (
			"server",
			"Server",
			"The server to which the data source belongs",
			E_TYPE_SOURCE_REGISTRY_SERVER,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/* This overrides the "uid" property in
	 * ESourceClass with a construct-only version. */
	g_object_class_install_property (
		object_class,
		PROP_UID,
		g_param_spec_string (
			"uid",
			"UID",
			"The unique identity of the data source",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));

	/* This overrides the "writable" property
	 * in ESourceClass with a writable version. */
	g_object_class_install_property (
		object_class,
		PROP_WRITABLE,
		g_param_spec_boolean (
			"writable",
			"Writable",
			"Whether the data source is writable",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	/* Do not use G_PARAM_CONSTRUCT.  We initialize the
	 * property ourselves in e_server_side_source_init(). */
	g_object_class_install_property (
		object_class,
		PROP_WRITE_DIRECTORY,
		g_param_spec_string (
			"write-directory",
			"Write Directory",
			"Directory in which to write changes to disk",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_server_side_source_initable_init (GInitableIface *interface)
{
	initable_parent_interface = g_type_interface_peek_parent (interface);

	interface->init = server_side_source_initable_init;
}

static void
e_server_side_source_init (EServerSideSource *source)
{
	const gchar *user_dir;

	source->priv = E_SERVER_SIDE_SOURCE_GET_PRIVATE (source);

	source->priv->node.data = source;

	user_dir = e_server_side_source_get_user_dir ();
	source->priv->write_directory = g_strdup (user_dir);
}

/**
 * e_server_side_source_get_user_dir:
 *
 * Returns the directory where user-specific data source files are stored.
 *
 * Returns: the user-specific data source directory
 *
 * Since: 3.6
 **/
const gchar *
e_server_side_source_get_user_dir (void)
{
	static gchar *dirname = NULL;

	if (G_UNLIKELY (dirname == NULL)) {
		const gchar *config_dir = e_get_user_config_dir ();
		dirname = g_build_filename (config_dir, "sources", NULL);
		g_mkdir_with_parents (dirname, 0700);
	}

	return dirname;
}

/**
 * e_server_side_source_new_user_file:
 * @uid: unique identifier for a data source, or %NULL
 *
 * Generates a unique file name for a new user-specific data source.
 * If @uid is non-%NULL it will be used in the basename of the file,
 * otherwise a unique basename will be generated using e_uid_new().
 *
 * The returned #GFile can then be passed to e_server_side_source_new().
 * Unreference the #GFile with g_object_unref() when finished with it.
 *
 * Note the data source file itself is not created here, only its name.
 *
 * Returns: the #GFile for a new data source
 *
 * Since: 3.6
 **/
GFile *
e_server_side_source_new_user_file (const gchar *uid)
{
	GFile *file;
	gchar *safe_uid;
	gchar *basename;
	gchar *filename;
	const gchar *user_dir;

	if (uid == NULL)
		safe_uid = e_uid_new ();
	else
		safe_uid = g_strdup (uid);
	e_filename_make_safe (safe_uid);

	user_dir = e_server_side_source_get_user_dir ();
	basename = g_strconcat (safe_uid, ".source", NULL);
	filename = g_build_filename (user_dir, basename, NULL);

	file = g_file_new_for_path (filename);

	g_free (basename);
	g_free (filename);
	g_free (safe_uid);

	return file;
}

/**
 * e_server_side_source_uid_from_file:
 * @file: a #GFile for a data source
 * @error: return location for a #GError, or %NULL
 *
 * Extracts a unique identity string from the base name of @file.
 * If the base name of @file is missing a '.source' extension, the
 * function sets @error and returns %NULL.
 *
 * Returns: the unique identity string for @file, or %NULL
 *
 * Since: 3.6
 **/
gchar *
e_server_side_source_uid_from_file (GFile *file,
                                    GError **error)
{
	gchar *basename;
	gchar *uid = NULL;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	basename = g_file_get_basename (file);

	if (g_str_has_suffix (basename, ".source")) {
		/* strlen(".source") --> 7 */
		uid = g_strndup (basename, strlen (basename) - 7);
	} else {
		g_set_error (
			error, G_IO_ERROR,
			G_IO_ERROR_INVALID_FILENAME,
			_("File must have a '.source' extension"));
	}

	g_free (basename);

	return uid;
}

/**
 * e_server_side_source_new:
 * @server: an #ESourceRegistryServer
 * @file: a #GFile, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EServerSideSource which belongs to @server.  If @file
 * is non-%NULL and points to an existing file, the #EServerSideSource is
 * initialized from the file content.  If a read error occurs or the file
 * contains syntax errors, the function sets @error and returns %NULL.
 *
 * Returns: a new #EServerSideSource, or %NULL
 *
 * Since: 3.6
 **/
ESource *
e_server_side_source_new (ESourceRegistryServer *server,
                          GFile *file,
                          GError **error)
{
	EDBusObjectSkeleton *dbus_object;
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);
	g_return_val_if_fail (file == NULL || G_IS_FILE (file), NULL);

	/* XXX This is an awkward way of initializing the "dbus-object"
	 *     property, but e_source_ref_dbus_object() needs to work. */
	dbus_object = e_dbus_object_skeleton_new (DBUS_OBJECT_PATH);

	source = g_initable_new (
		E_TYPE_SERVER_SIDE_SOURCE, NULL, error,
		"dbus-object", dbus_object,
		"file", file, "server", server, NULL);

	g_object_unref (dbus_object);

	return source;
}

/**
 * e_server_side_source_new_memory_only:
 * @server: an #ESourceRegistryServer
 * @uid: a unique identifier, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a memory-only #EServerSideSource which belongs to @server.
 * No on-disk key file is created for this data source, so it will not
 * be remembered across sessions.
 *
 * Data source collections are often populated with memory-only data
 * sources to serve as proxies for resources discovered on a remote server.
 * These data sources are usually neither #EServerSideSource:writable nor
 * #EServerSideSource:removable by clients, at least not directly.
 *
 * If an error occurs while instantiating the #EServerSideSource, the
 * function sets @error and returns %NULL.  Although at this time there
 * are no known error conditions for memory-only data sources.
 *
 * Returns: a new memory-only #EServerSideSource, or %NULL
 *
 * Since: 3.6
 **/
ESource *
e_server_side_source_new_memory_only (ESourceRegistryServer *server,
                                      const gchar *uid,
                                      GError **error)
{
	EDBusObjectSkeleton *dbus_object;
	ESource *source;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), NULL);

	/* XXX This is an awkward way of initializing the "dbus-object"
	 *     property, but e_source_ref_dbus_object() needs to work. */
	dbus_object = e_dbus_object_skeleton_new (DBUS_OBJECT_PATH);

	source = g_initable_new (
		E_TYPE_SERVER_SIDE_SOURCE, NULL, error,
		"dbus-object", dbus_object,
		"server", server, "uid", uid, NULL);

	g_object_unref (dbus_object);

	return source;
}

/**
 * e_server_side_source_load:
 * @source: an #EServerSideSource
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Reloads data source content from the file pointed to by the
 * #EServerSideSource:file property.
 *
 * If the #EServerSideSource:file property is %NULL or the file it points
 * to does not exist, the function does nothing and returns %TRUE.
 *
 * If a read error occurs or the file contains syntax errors, the function
 * sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.6
 **/
gboolean
e_server_side_source_load (EServerSideSource *source,
                           GCancellable *cancellable,
                           GError **error)
{
	GDBusObject *dbus_object;
	EDBusSource *dbus_source;
	GKeyFile *key_file;
	GFile *file;
	gboolean success;
	gchar *data = NULL;
	gsize length;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), FALSE);

	file = e_server_side_source_get_file (source);

	if (file != NULL)
		g_file_load_contents (
			file, cancellable, &data,
			&length, NULL, &local_error);

	/* Disregard G_IO_ERROR_NOT_FOUND and treat it as a successful load. */
	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_error_free (local_error);

	} else if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;

	} else {
		source->priv->file_contents = g_strdup (data);
	}

	if (data == NULL) {
		/* Create the bare minimum to pass parse_data(). */
		data = g_strdup_printf ("[%s]", PRIMARY_GROUP_NAME);
		length = strlen (data);
	}

	key_file = g_key_file_new ();

	success = server_side_source_parse_data (
		key_file, data, length, error);

	g_key_file_free (key_file);

	if (!success) {
		g_free (data);
		return FALSE;
	}

	/* Update the D-Bus interface properties. */

	dbus_object = e_source_ref_dbus_object (E_SOURCE (source));
	dbus_source = e_dbus_object_get_source (E_DBUS_OBJECT (dbus_object));

	e_dbus_source_set_data (dbus_source, data);

	g_object_unref (dbus_source);
	g_object_unref (dbus_object);

	g_free (data);

	return TRUE;
}

/**
 * e_server_side_source_get_file:
 * @source: an #EServerSideSource
 *
 * Returns the #GFile from which data source content is loaded and to
 * which changes are saved.  Note the @source may not have a #GFile.
 *
 * Returns: the #GFile for @source, or %NULL
 *
 * Since: 3.6
 **/
GFile *
e_server_side_source_get_file (EServerSideSource *source)
{
	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), NULL);

	return source->priv->file;
}

/**
 * e_server_side_source_get_node:
 * @source: an #EServerSideSource
 *
 * Returns the #GNode representing the @source's hierarchical placement,
 * or %NULL if @source has not been placed in the data source hierarchy.
 * The data member of the #GNode points back to @source.  This is an easy
 * way to traverse ancestor and descendant data sources.
 *
 * Note that accessing other data sources this way is not thread-safe,
 * and this therefore function may be replaced at some later date.
 *
 * Returns: a #GNode, or %NULL
 *
 * Since: 3.6
 **/
GNode *
e_server_side_source_get_node (EServerSideSource *source)
{
	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), NULL);

	return &source->priv->node;
}

/**
 * e_server_side_source_get_server:
 * @source: an #EServerSideSource
 *
 * Returns the #ESourceRegistryServer to which @source belongs.
 *
 * Returns: the #ESourceRegistryServer for @source
 *
 * Since: 3.6
 **/
ESourceRegistryServer *
e_server_side_source_get_server (EServerSideSource *source)
{
	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), NULL);

	return source->priv->server;
}

/**
 * e_server_side_source_get_allow_auth_prompt:
 * @source: an #EServerSideSource
 *
 * Returns whether an authentication prompt is allowed to be shown
 * for @source.  #EAuthenticationSession will honor this setting by
 * dismissing the session if it can't find a valid stored password.
 *
 * See e_server_side_source_set_allow_auth_prompt() for further
 * discussion.
 *
 * Returns: whether auth prompts are allowed for @source
 *
 * Since: 3.6
 **/
gboolean
e_server_side_source_get_allow_auth_prompt (EServerSideSource *source)
{
	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), FALSE);

	return source->priv->allow_auth_prompt;
}

/**
 * e_server_side_source_set_allow_auth_prompt:
 * @source: an #EServerSideSource
 * @allow_auth_prompt: whether auth prompts are allowed for @source
 *
 * Sets whether an authentication prompt is allowed to be shown for @source.
 * #EAuthenticationSession will honor this setting by dismissing the session
 * if it can't find a valid stored password.
 *
 * If the user declines to provide a password for @source when prompted
 * by an #EAuthenticationSession, the #ESourceRegistryServer will set this
 * property to %FALSE to suppress any further prompting, which would likely
 * annoy the user.  However when an #ESourceRegistry instance is created by
 * a client application, the first thing it does is reset this property to
 * %TRUE for all registered data sources.  So suppressing authentication
 * prompts is only ever temporary.
 *
 * Since: 3.6
 **/
void
e_server_side_source_set_allow_auth_prompt (EServerSideSource *source,
                                            gboolean allow_auth_prompt)
{
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	source->priv->allow_auth_prompt = allow_auth_prompt;

	g_object_notify (G_OBJECT (source), "allow-auth-prompt");
}

/**
 * e_server_side_source_get_write_directory:
 * @source: an #EServerSideSource
 *
 * Returns the local directory path where changes to @source are written.
 *
 * By default, changes are written to the local directory path returned by
 * e_server_side_source_get_user_dir(), but an #ECollectionBackend may wish
 * to override this to use its own private cache directory for data sources
 * it creates automatically.
 *
 * Returns: the directory where changes are written
 *
 * Since: 3.6
 **/
const gchar *
e_server_side_source_get_write_directory (EServerSideSource *source)
{
	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), NULL);

	return source->priv->write_directory;
}

/**
 * e_server_side_source_set_write_directory:
 * @source: an #EServerSideSource
 * @write_directory: the directory where changes are to be written
 *
 * Sets the local directory path where changes to @source are to be written.
 *
 * By default, changes are written to the local directory path returned by
 * e_server_side_source_get_user_dir(), but an #ECollectionBackend may wish
 * to override this to use its own private cache directory for data sources
 * it creates automatically.
 *
 * Since: 3.6
 **/
void
e_server_side_source_set_write_directory (EServerSideSource *source,
                                          const gchar *write_directory)
{
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));
	g_return_if_fail (write_directory != NULL);

	g_free (source->priv->write_directory);
	source->priv->write_directory = g_strdup (write_directory);

	g_object_notify (G_OBJECT (source), "write-directory");
}

/**
 * e_server_side_source_set_removable:
 * @source: an #EServerSideSource
 * @removable: whether to export the Removable interface
 *
 * Sets whether to allow registry clients to remove @source and its
 * descendants.  If %TRUE, the Removable D-Bus interface is exported at
 * the object path for @source.  If %FALSE, the Removable D-Bus interface
 * is unexported at the object path for @source, and any attempt by clients
 * to call e_source_remove() will fail.
 *
 * Note this is only enforced for clients of the registry D-Bus service.
 * The service itself may remove any data source at any time.
 *
 * Since: 3.6
 **/
void
e_server_side_source_set_removable (EServerSideSource *source,
                                    gboolean removable)
{
	EDBusSourceRemovable *dbus_source_removable = NULL;
	GDBusObject *dbus_object;
	gboolean currently_removable;

	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	currently_removable = e_source_get_removable (E_SOURCE (source));

	if (removable == currently_removable)
		return;

	if (removable) {
		dbus_source_removable =
			e_dbus_source_removable_skeleton_new ();

		g_signal_connect (
			dbus_source_removable, "handle-remove",
			G_CALLBACK (server_side_source_remove_cb), source);
	}

	dbus_object = e_source_ref_dbus_object (E_SOURCE (source));
	e_dbus_object_skeleton_set_source_removable (
		E_DBUS_OBJECT_SKELETON (dbus_object), dbus_source_removable);
	g_object_unref (dbus_object);

	if (dbus_source_removable != NULL)
		g_object_unref (dbus_source_removable);

	g_object_notify (G_OBJECT (source), "removable");
}

/**
 * e_server_side_source_set_writable:
 * @source: an #EServerSideSource
 * @writable: whether to export the Writable interface
 *
 * Sets whether to allow registry clients to alter the content of @source.
 * If %TRUE, the Writable D-Bus interface is exported at the object path
 * for @source.  If %FALSE, the Writable D-Bus interface is unexported at
 * the object path for @source, and any attempt by clients to call
 * e_source_write() will fail.
 *
 * Note this is only enforced for clients of the registry D-Bus service.
 * The service itself can write to any data source at any time.
 *
 * Since: 3.6
 **/
void
e_server_side_source_set_writable (EServerSideSource *source,
                                   gboolean writable)
{
	EDBusSourceWritable *dbus_source_writable = NULL;
	GDBusObject *dbus_object;
	gboolean currently_writable;

	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	currently_writable = e_source_get_writable (E_SOURCE (source));

	if (writable == currently_writable)
		return;

	if (writable) {
		dbus_source_writable =
			e_dbus_source_writable_skeleton_new ();

		g_signal_connect (
			dbus_source_writable, "handle-write",
			G_CALLBACK (server_side_source_write_cb), source);
	}

	dbus_object = e_source_ref_dbus_object (E_SOURCE (source));
	e_dbus_object_skeleton_set_source_writable (
		E_DBUS_OBJECT_SKELETON (dbus_object), dbus_source_writable);
	g_object_unref (dbus_object);

	if (dbus_source_writable != NULL)
		g_object_unref (dbus_source_writable);

	g_object_notify (G_OBJECT (source), "writable");
}


/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-conn-manager.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "camel-imapx-conn-manager.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-utils.h"

#define c(...) camel_imapx_debug(conman, __VA_ARGS__)

#define CON_READ_LOCK(x) \
	(g_static_rw_lock_reader_lock (&(x)->priv->rw_lock))
#define CON_READ_UNLOCK(x) \
	(g_static_rw_lock_reader_unlock (&(x)->priv->rw_lock))
#define CON_WRITE_LOCK(x) \
	(g_static_rw_lock_writer_lock (&(x)->priv->rw_lock))
#define CON_WRITE_UNLOCK(x) \
	(g_static_rw_lock_writer_unlock (&(x)->priv->rw_lock))

#define CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_CONN_MANAGER, CamelIMAPXConnManagerPrivate))

typedef struct _ConnectionInfo ConnectionInfo;

struct _CamelIMAPXConnManagerPrivate {
	/* XXX Might be easier for this to be a hash table,
	 *     with CamelIMAPXServer pointers as the keys. */
	GList *connections;
	gpointer store;  /* weak pointer */
	GStaticRWLock rw_lock;
};

struct _ConnectionInfo {
	GMutex *lock;
	CamelIMAPXServer *is;
	GHashTable *folder_names;
	gchar *selected_folder;
	volatile gint ref_count;
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE (
	CamelIMAPXConnManager,
	camel_imapx_conn_manager,
	CAMEL_TYPE_OBJECT)

static void
imapx_conn_shutdown (CamelIMAPXServer *is, CamelIMAPXConnManager *con_man);

static void
imapx_conn_update_select (CamelIMAPXServer *is,
                          const gchar *selected_folder,
                          CamelIMAPXConnManager *con_man);

static ConnectionInfo *
connection_info_new (CamelIMAPXServer *is)
{
	ConnectionInfo *cinfo;
	GHashTable *folder_names;

	folder_names = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	cinfo = g_slice_new0 (ConnectionInfo);
	cinfo->lock = g_mutex_new ();
	cinfo->is = g_object_ref (is);
	cinfo->folder_names = folder_names;
	cinfo->ref_count = 1;

	return cinfo;
}

static ConnectionInfo *
connection_info_ref (ConnectionInfo *cinfo)
{
	g_return_val_if_fail (cinfo != NULL, NULL);
	g_return_val_if_fail (cinfo->ref_count > 0, NULL);

	g_atomic_int_inc (&cinfo->ref_count);

	return cinfo;
}

static void
connection_info_unref (ConnectionInfo *cinfo)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (cinfo->ref_count > 0);

	if (g_atomic_int_dec_and_test (&cinfo->ref_count)) {
		camel_imapx_server_connect (cinfo->is, NULL, NULL);
		g_mutex_free (cinfo->lock);
		g_object_unref (cinfo->is);
		g_hash_table_destroy (cinfo->folder_names);
		g_free (cinfo->selected_folder);

		g_slice_free (ConnectionInfo, cinfo);
	}
}

static void
connection_info_cancel_and_unref (ConnectionInfo *cinfo)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (cinfo->ref_count > 0);

	g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_shutdown, NULL);
	g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_update_select, NULL);
	g_cancellable_cancel (cinfo->is->cancellable);
	connection_info_unref (cinfo);
}

static gboolean
connection_info_is_available (ConnectionInfo *cinfo)
{
	gboolean available;

	g_return_val_if_fail (cinfo != NULL, FALSE);

	g_mutex_lock (cinfo->lock);

	/* Available means it's not tracking any folder names. */
	available = (g_hash_table_size (cinfo->folder_names) == 0);

	g_mutex_unlock (cinfo->lock);

	return available;
}

static gboolean
connection_info_has_folder_name (ConnectionInfo *cinfo,
                                 const gchar *folder_name)
{
	gpointer value;

	g_return_val_if_fail (cinfo != NULL, FALSE);

	if (folder_name == NULL)
		return FALSE;

	g_mutex_lock (cinfo->lock);

	value = g_hash_table_lookup (cinfo->folder_names, folder_name);

	g_mutex_unlock (cinfo->lock);

	return (value != NULL);
}

static void
connection_info_insert_folder_name (ConnectionInfo *cinfo,
                                    const gchar *folder_name)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (folder_name != NULL);

	g_mutex_lock (cinfo->lock);

	g_hash_table_insert (
		cinfo->folder_names,
		g_strdup (folder_name),
		GINT_TO_POINTER (1));

	g_mutex_unlock (cinfo->lock);
}

static void
connection_info_remove_folder_name (ConnectionInfo *cinfo,
                                    const gchar *folder_name)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (folder_name != NULL);

	g_mutex_lock (cinfo->lock);

	g_hash_table_remove (cinfo->folder_names, folder_name);

	g_mutex_unlock (cinfo->lock);
}

static gchar *
connection_info_dup_selected_folder (ConnectionInfo *cinfo)
{
	gchar *selected_folder;

	g_return_val_if_fail (cinfo != NULL, NULL);

	g_mutex_lock (cinfo->lock);

	selected_folder = g_strdup (cinfo->selected_folder);

	g_mutex_unlock (cinfo->lock);

	return selected_folder;
}

static void
connection_info_set_selected_folder (ConnectionInfo *cinfo,
                                     const gchar *selected_folder)
{
	g_return_if_fail (cinfo != NULL);

	g_mutex_lock (cinfo->lock);

	g_free (cinfo->selected_folder);
	cinfo->selected_folder = g_strdup (selected_folder);

	g_mutex_unlock (cinfo->lock);
}

static GList *
imapx_conn_manager_list_info (CamelIMAPXConnManager *con_man)
{
	GList *list;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	CON_READ_LOCK (con_man);

	list = g_list_copy (con_man->priv->connections);
	g_list_foreach (list, (GFunc) connection_info_ref, NULL);

	CON_READ_UNLOCK (con_man);

	return list;
}

static ConnectionInfo *
imapx_conn_manager_lookup_info (CamelIMAPXConnManager *con_man,
                                CamelIMAPXServer *is)
{
	ConnectionInfo *cinfo = NULL;
	GList *list, *link;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);
	g_return_val_if_fail (CAMEL_IS_IMAPX_SERVER (is), NULL);

	CON_READ_LOCK (con_man);

	list = con_man->priv->connections;

	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;

		if (candidate->is == is) {
			cinfo = connection_info_ref (candidate);
			break;
		}
	}

	CON_READ_UNLOCK (con_man);

	return cinfo;
}

static gboolean
imapx_conn_manager_remove_info (CamelIMAPXConnManager *con_man,
                                ConnectionInfo *cinfo)
{
	GList *list, *link;
	gboolean removed = FALSE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), FALSE);
	g_return_val_if_fail (cinfo != NULL, FALSE);

	CON_WRITE_LOCK (con_man);

	list = con_man->priv->connections;
	link = g_list_find (list, cinfo);

	if (link != NULL) {
		list = g_list_delete_link (list, link);
		connection_info_unref (cinfo);
		removed = TRUE;
	}

	con_man->priv->connections = list;

	CON_WRITE_UNLOCK (con_man);

	return removed;
}

static void
imapx_conn_manager_set_store (CamelIMAPXConnManager *con_man,
                              CamelStore *store)
{
	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (con_man->priv->store == NULL);

	con_man->priv->store = store;

	g_object_add_weak_pointer (
		G_OBJECT (store), &con_man->priv->store);
}

static void
imapx_conn_manager_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			imapx_conn_manager_set_store (
				CAMEL_IMAPX_CONN_MANAGER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_conn_manager_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			g_value_set_object (
				value,
				camel_imapx_conn_manager_get_store (
				CAMEL_IMAPX_CONN_MANAGER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_conn_manager_dispose (GObject *object)
{
	CamelIMAPXConnManagerPrivate *priv;

	priv = CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE (object);

	g_list_free_full (
		priv->connections,
		(GDestroyNotify) connection_info_unref);
	priv->connections = NULL;

	if (priv->store != NULL) {
		g_object_remove_weak_pointer (
			G_OBJECT (priv->store), &priv->store);
		priv->store = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_conn_manager_parent_class)->dispose (object);
}

static void
imapx_conn_manager_finalize (GObject *object)
{
	CamelIMAPXConnManagerPrivate *priv;

	priv = CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE (object);

	g_static_rw_lock_free (&priv->rw_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_conn_manager_parent_class)->finalize (object);
}

static void
camel_imapx_conn_manager_class_init (CamelIMAPXConnManagerClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXConnManagerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_conn_manager_set_property;
	object_class->get_property = imapx_conn_manager_get_property;
	object_class->dispose = imapx_conn_manager_dispose;
	object_class->finalize = imapx_conn_manager_finalize;

	g_object_class_install_property (
		object_class,
		PROP_STORE,
		g_param_spec_object (
			"store",
			"Store",
			"The CamelStore to which we belong",
			CAMEL_TYPE_STORE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_conn_manager_init (CamelIMAPXConnManager *con_man)
{
	con_man->priv = CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE (con_man);

	g_static_rw_lock_init (&con_man->priv->rw_lock);
}

/* Static functions go here */

/* TODO destroy unused connections in a time-out loop */
static void
imapx_conn_shutdown (CamelIMAPXServer *is,
                     CamelIMAPXConnManager *con_man)
{
	ConnectionInfo *cinfo;

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo != NULL) {
		imapx_conn_manager_remove_info (con_man, cinfo);
		connection_info_unref (cinfo);
	}
}

static void
imapx_conn_update_select (CamelIMAPXServer *is,
                          const gchar *selected_folder,
                          CamelIMAPXConnManager *con_man)
{
	ConnectionInfo *cinfo;
	gchar *old_selected_folder;

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo == NULL)
		return;

	old_selected_folder = connection_info_dup_selected_folder (cinfo);

	if (old_selected_folder != NULL) {
		IMAPXJobQueueInfo *jinfo;

		jinfo = camel_imapx_server_get_job_queue_info (is);
		if (!g_hash_table_lookup (jinfo->folders, old_selected_folder)) {
			connection_info_remove_folder_name (cinfo, old_selected_folder);
			c (is->tagprefix, "Removed folder %s from connection folder list - select changed \n", old_selected_folder);
		}
		camel_imapx_destroy_job_queue_info (jinfo);

		g_free (old_selected_folder);
	}

	connection_info_set_selected_folder (cinfo, selected_folder);

	connection_info_unref (cinfo);
}

/* This should find a connection if the slots are full, returns NULL if there are slots available for a new connection for a folder */
static CamelIMAPXServer *
imapx_find_connection_unlocked (CamelIMAPXConnManager *con_man,
                                const gchar *folder_name)
{
	CamelService *service;
	CamelSettings *settings;
	CamelIMAPXServer *is = NULL;
	ConnectionInfo *cinfo = NULL;
	GList *list, *link;
	guint concurrent_connections;
	guint min_jobs = G_MAXUINT;

	/* Caller must be holding CON_WRITE_LOCK. */

	service = CAMEL_SERVICE (con_man->priv->store);

	settings = camel_service_ref_settings (service);

	concurrent_connections =
		camel_imapx_settings_get_concurrent_connections (
		CAMEL_IMAPX_SETTINGS (settings));

	g_object_unref (settings);

	/* XXX Have a dedicated connection for INBOX ? */

	list = con_man->priv->connections;

	/* If a folder was not given, find the least-busy connection. */
	if (folder_name == NULL)
		goto least_busy;

	/* First try to find a connection already handling this folder. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;

		if (connection_info_has_folder_name (candidate, folder_name)) {
			cinfo = connection_info_ref (candidate);
			goto exit;
		}
	}

	/* Next try to find a connection not handling any folders. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;

		if (connection_info_is_available (candidate)) {
			cinfo = connection_info_ref (candidate);
			goto exit;
		}
	}

least_busy:
	/* Pick the connection with the least number of jobs in progress. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;
		IMAPXJobQueueInfo *jinfo = NULL;

		jinfo = camel_imapx_server_get_job_queue_info (candidate->is);

		if (cinfo == NULL) {
			cinfo = connection_info_ref (candidate);
			min_jobs = jinfo->queue_len;

		} else if (jinfo->queue_len < min_jobs) {
			connection_info_unref (cinfo);
			cinfo = connection_info_ref (candidate);
			min_jobs = jinfo->queue_len;
		}

		camel_imapx_destroy_job_queue_info (jinfo);
	}

exit:
	if (cinfo != NULL && folder_name != NULL)
		connection_info_insert_folder_name (cinfo, folder_name);

	if (cinfo != NULL) {
		is = g_object_ref (cinfo->is);
		connection_info_unref (cinfo);
	}

	if (camel_debug_flag (conman))
		g_assert (!(concurrent_connections == g_list_length (con_man->priv->connections) && is == NULL));

	return is;
}

static CamelIMAPXServer *
imapx_create_new_connection_unlocked (CamelIMAPXConnManager *con_man,
                                      const gchar *folder_name,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelIMAPXServer *is = NULL;
	CamelIMAPXStore *imapx_store;
	ConnectionInfo *cinfo = NULL;
	gboolean success;

	/* Caller must be holding CON_WRITE_LOCK. */

	imapx_store = CAMEL_IMAPX_STORE (con_man->priv->store);

	/* Check if we got cancelled while we were waiting. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return NULL;

	is = camel_imapx_server_new (imapx_store);

	/* XXX As part of the connect operation the CamelIMAPXServer will
	 *     have to call camel_session_authenticate_sync(), but it has
	 *     no way to pass itself through in that call so the service
	 *     knows which CamelIMAPXServer is trying to authenticate.
	 *
	 *     IMAPX is the only provider that does multiple connections
	 *     like this, so I didn't want to pollute the CamelSession and
	 *     CamelService authentication APIs with an extra argument.
	 *     Instead we do this little hack so the service knows which
	 *     CamelIMAPXServer to act on in its authenticate_sync() method.
	 *
	 *     Because we're holding the CAMEL_SERVICE_REC_CONNECT_LOCK
	 *     we should not have multiple IMAPX connections trying to
	 *     authenticate at once, so this should be thread-safe.
	 */
	imapx_store->authenticating_server = g_object_ref (is);
	success = camel_imapx_server_connect (is, cancellable, error);
	g_object_unref (imapx_store->authenticating_server);
	imapx_store->authenticating_server = NULL;

	if (!success) {
		g_object_unref (is);
		return NULL;
	}

	g_signal_connect (
		is, "shutdown",
		G_CALLBACK (imapx_conn_shutdown), con_man);
	g_signal_connect (
		is, "select_changed",
		G_CALLBACK (imapx_conn_update_select), con_man);

	cinfo = connection_info_new (is);

	if (folder_name != NULL)
		connection_info_insert_folder_name (cinfo, folder_name);

	/* Takes ownership of the ConnectionInfo. */
	con_man->priv->connections = g_list_prepend (
		con_man->priv->connections, cinfo);

	c (is->tagprefix, "Created new connection for %s and total connections %d \n", folder_name, g_list_length (con_man->priv->connections));

	return is;
}

/****************************/

CamelIMAPXConnManager *
camel_imapx_conn_manager_new (CamelStore *store)
{
	g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

	return g_object_new (
		CAMEL_TYPE_IMAPX_CONN_MANAGER, "store", store, NULL);
}

CamelStore *
camel_imapx_conn_manager_get_store (CamelIMAPXConnManager *con_man)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	return CAMEL_STORE (con_man->priv->store);
}

CamelIMAPXServer *
camel_imapx_conn_manager_get_connection (CamelIMAPXConnManager *con_man,
                                         const gchar *folder_name,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelIMAPXServer *is = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	/* Hold the writer lock while we requisition a CamelIMAPXServer
	 * to prevent other threads from adding or removing connections. */
	CON_WRITE_LOCK (con_man);

	/* Check if we got cancelled while waiting for the lock. */
	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		is = imapx_find_connection_unlocked (con_man, folder_name);
		if (is == NULL)
			is = imapx_create_new_connection_unlocked (
				con_man, folder_name, cancellable, error);
	}

	CON_WRITE_UNLOCK (con_man);

	return is;
}

GList *
camel_imapx_conn_manager_get_connections (CamelIMAPXConnManager *con_man)
{
	GList *list, *link;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	list = imapx_conn_manager_list_info (con_man);

	/* Swap ConnectionInfo for CamelIMAPXServer in each link. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *cinfo = link->data;
		link->data = g_object_ref (cinfo->is);
		connection_info_unref (cinfo);
	}

	return list;
}

/* Used for handling operations that fails to execute and that needs to removed from folder list */
void
camel_imapx_conn_manager_update_con_info (CamelIMAPXConnManager *con_man,
                                          CamelIMAPXServer *is,
                                          const gchar *folder_name)
{
	ConnectionInfo *cinfo;
	IMAPXJobQueueInfo *jinfo;

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo == NULL)
		return;

	jinfo = camel_imapx_server_get_job_queue_info (cinfo->is);
	if (!g_hash_table_lookup (jinfo->folders, folder_name)) {
		connection_info_remove_folder_name (cinfo, folder_name);
		c (is->tagprefix, "Removed folder %s from connection folder list - op done \n", folder_name);
	}
	camel_imapx_destroy_job_queue_info (jinfo);

	connection_info_unref (cinfo);
}

void
camel_imapx_conn_manager_close_connections (CamelIMAPXConnManager *con_man)
{
	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	CON_WRITE_LOCK (con_man);

	g_list_free_full (
		con_man->priv->connections,
		(GDestroyNotify) connection_info_cancel_and_unref);
	con_man->priv->connections = NULL;

	CON_WRITE_UNLOCK (con_man);
}


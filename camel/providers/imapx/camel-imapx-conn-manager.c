/*-*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-conn-manager.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Chenthill Palanisamy <pchenthill@novell.com>
 */

#include "camel-imapx-conn-manager.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-store.h"
#include "camel-imapx-utils.h"

#define c(...) camel_imapx_debug(conman, __VA_ARGS__)

#define CON_READ_LOCK(x) \
	(g_rw_lock_reader_lock (&(x)->priv->rw_lock))
#define CON_READ_UNLOCK(x) \
	(g_rw_lock_reader_unlock (&(x)->priv->rw_lock))
#define CON_WRITE_LOCK(x) \
	(g_rw_lock_writer_lock (&(x)->priv->rw_lock))
#define CON_WRITE_UNLOCK(x) \
	(g_rw_lock_writer_unlock (&(x)->priv->rw_lock))

#define CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_CONN_MANAGER, CamelIMAPXConnManagerPrivate))

typedef struct _ConnectionInfo ConnectionInfo;

struct _CamelIMAPXConnManagerPrivate {
	/* XXX Might be easier for this to be a hash table,
	 *     with CamelIMAPXServer pointers as the keys. */
	GList *connections;
	GWeakRef store;
	GRWLock rw_lock;
	guint limit_max_connections;

	GMutex pending_connections_lock;
	GSList *pending_connections; /* GCancellable * */
};

struct _ConnectionInfo {
	GMutex lock;
	CamelIMAPXServer *is;
	GHashTable *folder_names;
	gchar *selected_folder;
	GError *shutdown_error;
	volatile gint ref_count;
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE (
	CamelIMAPXConnManager,
	camel_imapx_conn_manager,
	G_TYPE_OBJECT)

static void
imapx_conn_shutdown (CamelIMAPXServer *is,
		     const GError *error,
		     CamelIMAPXConnManager *con_man);

static void
imapx_conn_update_select (CamelIMAPXServer *is,
                          CamelIMAPXMailbox *mailbox,
                          CamelIMAPXConnManager *con_man);
static void
imapx_conn_mailbox_closed (CamelIMAPXServer *is,
			   CamelIMAPXMailbox *mailbox,
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
	g_mutex_init (&cinfo->lock);
	cinfo->is = g_object_ref (is);
	cinfo->folder_names = folder_names;
	cinfo->shutdown_error = NULL;
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
		camel_imapx_server_shutdown (cinfo->is, cinfo->shutdown_error);
		g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_shutdown, NULL);
		g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_update_select, NULL);
		g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_mailbox_closed, NULL);

		g_mutex_clear (&cinfo->lock);
		g_object_unref (cinfo->is);
		g_hash_table_destroy (cinfo->folder_names);
		g_free (cinfo->selected_folder);
		g_clear_error (&cinfo->shutdown_error);

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
	g_signal_handlers_disconnect_matched (cinfo->is, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, imapx_conn_mailbox_closed, NULL);
	camel_imapx_server_shutdown (cinfo->is, cinfo->shutdown_error);
	connection_info_unref (cinfo);
}

static gboolean
connection_info_is_available (ConnectionInfo *cinfo)
{
	gboolean available;

	g_return_val_if_fail (cinfo != NULL, FALSE);

	g_mutex_lock (&cinfo->lock);

	/* Available means it's not tracking any folder names or no jobs are running. */
	available = (g_hash_table_size (cinfo->folder_names) == 0) ||
		    camel_imapx_server_get_command_count (cinfo->is) == 0;

	g_mutex_unlock (&cinfo->lock);

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

	g_mutex_lock (&cinfo->lock);

	value = g_hash_table_lookup (cinfo->folder_names, folder_name);

	g_mutex_unlock (&cinfo->lock);

	return (value != NULL);
}

static void
connection_info_insert_folder_name (ConnectionInfo *cinfo,
                                    const gchar *folder_name)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (folder_name != NULL);

	g_mutex_lock (&cinfo->lock);

	g_hash_table_insert (
		cinfo->folder_names,
		g_strdup (folder_name),
		GINT_TO_POINTER (1));

	g_mutex_unlock (&cinfo->lock);
}

static void
connection_info_remove_folder_name (ConnectionInfo *cinfo,
                                    const gchar *folder_name)
{
	g_return_if_fail (cinfo != NULL);
	g_return_if_fail (folder_name != NULL);

	g_mutex_lock (&cinfo->lock);

	g_hash_table_remove (cinfo->folder_names, folder_name);

	g_mutex_unlock (&cinfo->lock);
}

static gchar *
connection_info_dup_selected_folder (ConnectionInfo *cinfo)
{
	gchar *selected_folder;

	g_return_val_if_fail (cinfo != NULL, NULL);

	g_mutex_lock (&cinfo->lock);

	selected_folder = g_strdup (cinfo->selected_folder);

	g_mutex_unlock (&cinfo->lock);

	return selected_folder;
}

static void
connection_info_set_selected_folder (ConnectionInfo *cinfo,
                                     const gchar *selected_folder)
{
	g_return_if_fail (cinfo != NULL);

	g_mutex_lock (&cinfo->lock);

	g_free (cinfo->selected_folder);
	cinfo->selected_folder = g_strdup (selected_folder);

	g_mutex_unlock (&cinfo->lock);
}

static void
connection_info_set_shutdown_error (ConnectionInfo *cinfo,
                                    const GError *shutdown_error)
{
	g_return_if_fail (cinfo != NULL);

	g_mutex_lock (&cinfo->lock);

	if (cinfo->shutdown_error != shutdown_error) {
		g_clear_error (&cinfo->shutdown_error);
		if (shutdown_error)
			cinfo->shutdown_error = g_error_copy (shutdown_error);
	}

	g_mutex_unlock (&cinfo->lock);
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
imax_conn_manager_cancel_pending_connections (CamelIMAPXConnManager *con_man)
{
	GSList *link;

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	g_mutex_lock (&con_man->priv->pending_connections_lock);
	for (link = con_man->priv->pending_connections; link; link = g_slist_next (link)) {
		GCancellable *cancellable = link->data;

		if (cancellable)
			g_cancellable_cancel (cancellable);
	}
	g_mutex_unlock (&con_man->priv->pending_connections_lock);
}

static void
imapx_conn_manager_set_store (CamelIMAPXConnManager *con_man,
                              CamelStore *store)
{
	g_return_if_fail (CAMEL_IS_STORE (store));

	g_weak_ref_set (&con_man->priv->store, store);
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
			g_value_take_object (
				value,
				camel_imapx_conn_manager_ref_store (
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

	imax_conn_manager_cancel_pending_connections (CAMEL_IMAPX_CONN_MANAGER (object));

	g_weak_ref_set (&priv->store, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_conn_manager_parent_class)->dispose (object);
}

static void
imapx_conn_manager_finalize (GObject *object)
{
	CamelIMAPXConnManagerPrivate *priv;

	priv = CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE (object);

	g_warn_if_fail (priv->pending_connections == NULL);

	g_rw_lock_clear (&priv->rw_lock);
	g_mutex_clear (&priv->pending_connections_lock);
	g_weak_ref_clear (&priv->store);

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

	g_rw_lock_init (&con_man->priv->rw_lock);
	g_mutex_init (&con_man->priv->pending_connections_lock);
	g_weak_ref_init (&con_man->priv->store, NULL);
}

static void
imapx_conn_shutdown (CamelIMAPXServer *is,
		     const GError *error,
                     CamelIMAPXConnManager *con_man)
{
	ConnectionInfo *cinfo;

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo != NULL) {
		imapx_conn_manager_remove_info (con_man, cinfo);
		connection_info_unref (cinfo);
	}

	/* If one connection ends with this error, then it means all
	   other opened connections also may end with the same error,
	   thus better to kill them all from the list of connections.
	*/
	if (g_error_matches (error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
		camel_imapx_conn_manager_close_connections (con_man, error);
	}
}

static void
imapx_conn_update_select (CamelIMAPXServer *is,
                          CamelIMAPXMailbox *mailbox,
                          CamelIMAPXConnManager *con_man)
{
	ConnectionInfo *cinfo;
	gchar *old_selected_folder, *selected_folder = NULL;

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo == NULL)
		return;

	old_selected_folder = connection_info_dup_selected_folder (cinfo);

	if (old_selected_folder != NULL) {
		if (!camel_imapx_server_folder_name_in_jobs (is, old_selected_folder)) {
			connection_info_remove_folder_name (cinfo, old_selected_folder);
			c (is->tagprefix, "Removed folder %s from connection folder list - select changed \n", old_selected_folder);
		}

		g_free (old_selected_folder);
	}

	if (mailbox)
		selected_folder = camel_imapx_mailbox_dup_folder_path (mailbox);
	connection_info_set_selected_folder (cinfo, selected_folder);
	g_free (selected_folder);

	connection_info_unref (cinfo);
}

static void
imapx_conn_mailbox_closed (CamelIMAPXServer *is,
			   CamelIMAPXMailbox *mailbox,
			   CamelIMAPXConnManager *con_man)
{
	imapx_conn_update_select (is, NULL, con_man);
}

/* This should find a connection if the slots are full, returns NULL if there are slots available for a new connection for a folder */
static CamelIMAPXServer *
imapx_find_connection_unlocked (CamelIMAPXConnManager *con_man,
                                const gchar *folder_name,
				gboolean for_expensive_job)
{
	CamelStore *store;
	CamelSettings *settings;
	CamelIMAPXServer *is = NULL;
	ConnectionInfo *cinfo = NULL;
	GList *list, *link;
	guint concurrent_connections, opened_connections, expensive_connections = 0;
	guint min_jobs = G_MAXUINT;

	/* Caller must be holding CON_WRITE_LOCK. */

	store = camel_imapx_conn_manager_ref_store (con_man);
	g_return_val_if_fail (store != NULL, NULL);

	settings = camel_service_ref_settings (CAMEL_SERVICE (store));

	concurrent_connections =
		camel_imapx_settings_get_concurrent_connections (
		CAMEL_IMAPX_SETTINGS (settings));

	if (con_man->priv->limit_max_connections > 0 &&
	    con_man->priv->limit_max_connections < concurrent_connections)
		concurrent_connections = con_man->priv->limit_max_connections;

	g_object_unref (settings);

	/* XXX Have a dedicated connection for INBOX ? */

	opened_connections = g_list_length (con_man->priv->connections);
	list = con_man->priv->connections;

	/* If a folder was not given, find the least-busy connection. */
	if (folder_name == NULL) {
		goto least_busy;
	}

	/* First try to find a connection already handling this folder. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;

		if (camel_imapx_server_has_expensive_command (candidate->is))
			expensive_connections++;

		if (connection_info_has_folder_name (candidate, folder_name) && camel_imapx_server_is_connected (candidate->is) &&
		    (opened_connections >= concurrent_connections || for_expensive_job || !camel_imapx_server_has_expensive_command (candidate->is))) {
			if (cinfo) {
				/* group expensive jobs into one connection */
				if (for_expensive_job && camel_imapx_server_has_expensive_command (cinfo->is))
					continue;

				if (!for_expensive_job && camel_imapx_server_get_command_count (cinfo->is) < camel_imapx_server_get_command_count (candidate->is))
					continue;

				connection_info_unref (cinfo);
			}

			cinfo = connection_info_ref (candidate);
			if (for_expensive_job && camel_imapx_server_has_expensive_command (cinfo->is))
				goto exit;
		}
	}

 least_busy:
	if (for_expensive_job) {
		/* allow only half connections being with expensive operations */
		if (expensive_connections > 0 &&
		    expensive_connections < concurrent_connections / 2 &&
		    opened_connections < concurrent_connections)
			goto exit;

		/* cinfo here doesn't have any expensive command, thus ignore it */
		if (cinfo) {
			connection_info_unref (cinfo);
			cinfo = NULL;
		}

		/* Pick the connection with the least number of jobs in progress among those with expensive jobs. */
		for (link = list; link != NULL; link = g_list_next (link)) {
			ConnectionInfo *candidate = link->data;
			guint jobs;

			if (!camel_imapx_server_is_connected (candidate->is) ||
			    !camel_imapx_server_has_expensive_command (candidate->is))
				continue;

			jobs = camel_imapx_server_get_command_count (candidate->is);

			if (cinfo == NULL) {
				cinfo = connection_info_ref (candidate);
				min_jobs = jobs;

			} else if (jobs < min_jobs) {
				connection_info_unref (cinfo);
				cinfo = connection_info_ref (candidate);
				min_jobs = jobs;
			}
		}

		if (cinfo)
			goto exit;
	}

	/* Next try to find a connection not handling any folders. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;

		if (camel_imapx_server_is_connected (candidate->is) &&
		    connection_info_is_available (candidate)) {
			if (cinfo)
				connection_info_unref (cinfo);
			cinfo = connection_info_ref (candidate);
			goto exit;
		}
	}

	/* open a new connection, if there is a room for it */
	if (opened_connections < concurrent_connections && (!for_expensive_job || opened_connections < concurrent_connections / 2)) {
		if (cinfo && camel_imapx_server_get_command_count (cinfo->is) != 0) {
			connection_info_unref (cinfo);
			cinfo = NULL;
		}
		goto exit;
	} else {
		if (cinfo)
			min_jobs = camel_imapx_server_get_command_count (cinfo->is);
	}

	/* Pick the connection with the least number of jobs in progress. */
	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *candidate = link->data;
		gint n_commands;

		if (!camel_imapx_server_is_connected (candidate->is))
			continue;

		n_commands = camel_imapx_server_get_command_count (candidate->is);

		if (cinfo == NULL) {
			cinfo = connection_info_ref (candidate);
			min_jobs = n_commands;

		} else if (n_commands < min_jobs) {
			connection_info_unref (cinfo);
			cinfo = connection_info_ref (candidate);
			min_jobs = n_commands;
		}
	}

exit:
	if (cinfo != NULL && folder_name != NULL)
		connection_info_insert_folder_name (cinfo, folder_name);

	if (camel_debug_flag (conman)) {
		printf ("%s: for-expensive:%d will return:%p cmd-count:%d has-expensive:%d found:%d; connections opened:%d max:%d\n", G_STRFUNC, for_expensive_job, cinfo, cinfo ? camel_imapx_server_get_command_count (cinfo->is) : -2, cinfo ? camel_imapx_server_has_expensive_command (cinfo->is) : -2, expensive_connections, g_list_length (list), concurrent_connections);
		for (link = list; link != NULL; link = g_list_next (link)) {
			ConnectionInfo *candidate = link->data;

			printf ("   cmds:%d has-expensive:%d avail:%d cinfo:%p server:%p\n", camel_imapx_server_get_command_count (candidate->is), camel_imapx_server_has_expensive_command (candidate->is), connection_info_is_available (candidate), candidate, candidate->is);
		}
	}

	if (cinfo != NULL) {
		is = g_object_ref (cinfo->is);
		connection_info_unref (cinfo);
	}

	g_object_unref (store);

	return is;
}

static gchar
imapx_conn_manager_get_next_free_tagprefix_unlocked (CamelIMAPXConnManager *con_man)
{
	gchar adept;
	GList *iter;

	/* the 'Z' is dedicated to auth types query */
	adept = 'A';
	while (adept < 'Z') {
		for (iter = con_man->priv->connections; iter; iter = g_list_next (iter)) {
			ConnectionInfo *cinfo = iter->data;

			if (!cinfo || !cinfo->is)
				continue;

			if (cinfo->is->tagprefix == adept)
				break;
		}

		/* Read all current active connections and none has the same tag prefix */
		if (!iter)
			break;

		adept++;
	}

	g_return_val_if_fail (adept >= 'A' && adept < 'Z', 'Z');

	return adept;
}

static CamelIMAPXServer *
imapx_create_new_connection_unlocked (CamelIMAPXConnManager *con_man,
                                      const gchar *folder_name,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelStore *store;
	CamelIMAPXServer *is = NULL;
	CamelIMAPXStore *imapx_store;
	ConnectionInfo *cinfo = NULL;
	gboolean success;

	/* Caller must be holding CON_WRITE_LOCK. */

	/* Check if we got cancelled while we were waiting. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return NULL;

	store = camel_imapx_conn_manager_ref_store (con_man);
	g_return_val_if_fail (store != NULL, NULL);

	imapx_store = CAMEL_IMAPX_STORE (store);

	is = camel_imapx_server_new (imapx_store);
	is->tagprefix = imapx_conn_manager_get_next_free_tagprefix_unlocked (con_man);

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
	camel_imapx_store_set_connecting_server (imapx_store, is, con_man->priv->connections != NULL);
	success = camel_imapx_server_connect (is, cancellable, error);
	camel_imapx_store_set_connecting_server (imapx_store, NULL, FALSE);

	if (!success) {
		g_clear_object (&is);
		goto exit;
	}

	g_signal_connect (
		is, "shutdown",
		G_CALLBACK (imapx_conn_shutdown), con_man);

	g_signal_connect (
		is, "mailbox-select",
		G_CALLBACK (imapx_conn_update_select), con_man);
	g_signal_connect (
		is, "mailbox-closed",
		G_CALLBACK (imapx_conn_mailbox_closed), con_man);

	cinfo = connection_info_new (is);

	if (folder_name != NULL)
		connection_info_insert_folder_name (cinfo, folder_name);

	/* Takes ownership of the ConnectionInfo. */
	con_man->priv->connections = g_list_prepend (
		con_man->priv->connections, cinfo);

	c (is->tagprefix, "Created new connection %p (server:%p) for %s; total connections %d\n", cinfo, cinfo->is, folder_name, g_list_length (con_man->priv->connections));

exit:
	g_object_unref (store);

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
camel_imapx_conn_manager_ref_store (CamelIMAPXConnManager *con_man)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	return g_weak_ref_get (&con_man->priv->store);
}

CamelIMAPXServer *
camel_imapx_conn_manager_get_connection (CamelIMAPXConnManager *con_man,
                                         const gchar *folder_name,
					 gboolean for_expensive_job,
                                         GCancellable *cancellable,
                                         GError **error)
{
	CamelIMAPXServer *is = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	g_mutex_lock (&con_man->priv->pending_connections_lock);
	if (cancellable) {
		g_object_ref (cancellable);
	} else {
		cancellable = g_cancellable_new ();
	}
	con_man->priv->pending_connections = g_slist_prepend (con_man->priv->pending_connections, cancellable);
	g_mutex_unlock (&con_man->priv->pending_connections_lock);

	/* Hold the writer lock while we requisition a CamelIMAPXServer
	 * to prevent other threads from adding or removing connections. */
	CON_WRITE_LOCK (con_man);

	/* Check if we've got cancelled while waiting for the lock. */
	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		is = imapx_find_connection_unlocked (con_man, folder_name, for_expensive_job);
		if (is == NULL) {
			GError *local_error = NULL;

			is = imapx_create_new_connection_unlocked (con_man, folder_name, cancellable, &local_error);

			if (!is) {
				gboolean limit_connections =
					g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR,
					CAMEL_IMAPX_SERVER_ERROR_CONCURRENT_CONNECT_FAILED) &&
					con_man->priv->connections;

				c ('*', "Failed to open a new connection, while having %d opened, with error: %s; will limit connections: %s\n",
					g_list_length (con_man->priv->connections),
					local_error ? local_error->message : "Unknown error",
					limit_connections ? "yes" : "no");

				if (limit_connections) {
					/* limit to one-less than current connection count - be nice to the server */
					con_man->priv->limit_max_connections = g_list_length (con_man->priv->connections) - 1;
					if (!con_man->priv->limit_max_connections)
						con_man->priv->limit_max_connections = 1;

					g_clear_error (&local_error);
					is = imapx_find_connection_unlocked (con_man, folder_name, for_expensive_job);
				} else if (local_error) {
					g_propagate_error (error, local_error);
				}
			}
		}
	}

	CON_WRITE_UNLOCK (con_man);

	g_mutex_lock (&con_man->priv->pending_connections_lock);
	con_man->priv->pending_connections = g_slist_remove (con_man->priv->pending_connections, cancellable);
	g_object_unref (cancellable);
	g_mutex_unlock (&con_man->priv->pending_connections_lock);

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

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	/* Returns a new ConnectionInfo reference. */
	cinfo = imapx_conn_manager_lookup_info (con_man, is);

	if (cinfo == NULL)
		return;

	if (camel_imapx_server_folder_name_in_jobs (is, folder_name)) {
		connection_info_remove_folder_name (cinfo, folder_name);
		c (is->tagprefix, "Removed folder %s from connection folder list - op done \n", folder_name);
	}

	connection_info_unref (cinfo);
}

void
camel_imapx_conn_manager_close_connections (CamelIMAPXConnManager *con_man,
					    const GError *error)
{
	GList *iter, *connections;

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	/* Do this before acquiring the write lock, because any pending
	   connection holds the write lock, thus makes this request starve. */
	imax_conn_manager_cancel_pending_connections (con_man);

	CON_WRITE_LOCK (con_man);

	c('*', "Closing all %d connections, with propagated error: %s\n", g_list_length (con_man->priv->connections), error ? error->message : "none");

	connections = con_man->priv->connections;
	con_man->priv->connections = NULL;

	CON_WRITE_UNLOCK (con_man);

	for (iter = connections; iter; iter = g_list_next (iter)) {
		connection_info_set_shutdown_error (iter->data, error);
	}

	g_list_free_full (connections, (GDestroyNotify) connection_info_cancel_and_unref);
}

/* for debugging purposes only */
void
camel_imapx_conn_manager_dump_queue_status (CamelIMAPXConnManager *con_man)
{
	GList *list, *link;

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	list = imapx_conn_manager_list_info (con_man);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ConnectionInfo *cinfo = link->data;
		camel_imapx_server_dump_queue_status (cinfo->is);
		connection_info_unref (cinfo);
	}

	g_list_free (list);
}

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

#include <glib.h>
#include "camel-imapx-conn-manager.h"
#include "camel-imapx-utils.h"

#define c(x) camel_imapx_debug(conman, x)

#define CAMEL_IMAPX_CONN_MANAGER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_OBJECT, CamelIMAPXConnManager))

#define CON_LOCK(x) (g_static_rec_mutex_lock(&(x)->priv->con_man_lock))
#define CON_UNLOCK(x) (g_static_rec_mutex_unlock(&(x)->priv->con_man_lock))

G_DEFINE_TYPE (CamelIMAPXConnManager, camel_imapx_conn_manager, CAMEL_TYPE_OBJECT)

struct _CamelIMAPXConnManagerPrivate {
	GSList *connections;
	guint n_connections;
	CamelStore *store;
	GStaticRecMutex con_man_lock;
	gboolean clearing_connections;
};

typedef struct {
	GHashTable *folders;
	CamelIMAPXServer *conn;
	gchar *selected_folder;
} ConnectionInfo;

static void
free_connection (gpointer data, gpointer user_data)
{
	ConnectionInfo *cinfo = (ConnectionInfo *) data;
	CamelIMAPXServer *conn = cinfo->conn;

	camel_imapx_server_connect (conn, NULL);

	g_object_unref (conn);
	g_hash_table_destroy (cinfo->folders);
	g_free (cinfo->selected_folder);

	g_free (cinfo);
}

static void
imapx_prune_connections (CamelIMAPXConnManager *con_man)
{
	CON_LOCK(con_man);

	con_man->priv->clearing_connections = TRUE;
	g_slist_foreach (con_man->priv->connections, (GFunc) free_connection, NULL);
	con_man->priv->connections = NULL;
	con_man->priv->clearing_connections = FALSE;

	CON_UNLOCK(con_man);
}

static void
imapx_conn_manager_finalize (GObject *object)
{
	CamelIMAPXConnManager *con_man = CAMEL_IMAPX_CONN_MANAGER(object);

	imapx_prune_connections (con_man);
	g_static_rec_mutex_free (&con_man->priv->con_man_lock);
	g_object_unref (con_man->priv->store);
}

static void
camel_imapx_conn_manager_class_init (CamelIMAPXConnManagerClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXConnManagerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imapx_conn_manager_finalize;
}

static void
camel_imapx_conn_manager_init (CamelIMAPXConnManager *con_man)
{
	CamelIMAPXConnManagerPrivate *priv;

	priv = g_new0 (CamelIMAPXConnManagerPrivate, 1);
	con_man->priv = priv;

	/* default is 1 connection */
	con_man->priv->n_connections = 1;
	g_static_rec_mutex_init (&con_man->priv->con_man_lock);

	con_man->priv->clearing_connections = FALSE;
}

/* Static functions go here */

/* TODO destroy unused connections in a time-out loop */
static void
imapx_conn_shutdown (CamelIMAPXServer *conn, CamelIMAPXConnManager *con_man)
{
	GSList *l;
	ConnectionInfo *cinfo;
	gboolean found = FALSE;

	/* when clearing connections then other thread than a parser thread,
	   in which this function is called, holds the CON_LOCK, thus skip
	   this all, because otherwise a deadlock will happen.
	   The connection will be freed later anyway. */
	if (con_man->priv->clearing_connections) {
		c(printf ("%s: called on %p when clearing connections, skipping it...\n", G_STRFUNC, conn));
		return;
	}

	CON_LOCK(con_man);

	for (l = con_man->priv->connections; l != NULL; l = g_slist_next (l)) {
		cinfo = (ConnectionInfo *) l->data;
		if (cinfo->conn == conn) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		con_man->priv->connections = g_slist_remove (con_man->priv->connections, cinfo);
		free_connection (cinfo, GINT_TO_POINTER (1));
	}

	CON_UNLOCK(con_man);
}

static void
imapx_conn_update_select (CamelIMAPXServer *conn, const gchar *selected_folder, CamelIMAPXConnManager *con_man)
{
	GSList *l;
	ConnectionInfo *cinfo;
	gboolean found = FALSE;

	CON_LOCK(con_man);

	for (l = con_man->priv->connections; l != NULL; l = g_slist_next (l)) {
		cinfo = (ConnectionInfo *) l->data;
		if (cinfo->conn == conn) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		if (cinfo->selected_folder) {
			IMAPXJobQueueInfo *jinfo;

			jinfo = camel_imapx_server_get_job_queue_info (cinfo->conn);
			if (!g_hash_table_lookup (jinfo->folders, cinfo->selected_folder)) {
				g_hash_table_remove (cinfo->folders, cinfo->selected_folder);
				c(printf ("Removed folder %s from connection folder list - select changed \n", cinfo->selected_folder));
			}
			camel_imapx_destroy_job_queue_info (jinfo);
			g_free (cinfo->selected_folder);
		}

		cinfo->selected_folder = g_strdup (selected_folder);
	}

	CON_UNLOCK(con_man);
}

/* This should find a connection if the slots are full, returns NULL if there are slots available for a new connection for a folder */
static CamelIMAPXServer *
imapx_find_connection (CamelIMAPXConnManager *con_man, const gchar *folder_name)
{
	guint i = 0, prev_len = -1, n = -1;
	GSList *l;
	CamelIMAPXServer *conn = NULL;
	ConnectionInfo *cinfo = NULL;

	CON_LOCK(con_man);

	/* Have a dedicated connection for INBOX ? */
	for (l = con_man->priv->connections, i = 0; l != NULL; l = g_slist_next (l), i++) {
		IMAPXJobQueueInfo *jinfo = NULL;

		cinfo = (ConnectionInfo *) l->data;
		jinfo = camel_imapx_server_get_job_queue_info (cinfo->conn);

		if (prev_len == -1) {
			prev_len = jinfo->queue_len;
			n = 0;
		}

		if (jinfo->queue_len < prev_len)
			n = i;

		camel_imapx_destroy_job_queue_info (jinfo);

		if (folder_name && (g_hash_table_lookup (cinfo->folders, folder_name) || g_hash_table_size (cinfo->folders) == 0)) {
			conn = g_object_ref (cinfo->conn);

			if (folder_name)
				g_hash_table_insert (cinfo->folders, g_strdup (folder_name), GINT_TO_POINTER (1));
			c(printf ("Found connection for %s and connection number %d \n", folder_name, i+1));
			break;
		}
	}

	if (!conn && n != -1 && (!folder_name || con_man->priv->n_connections == g_slist_length (con_man->priv->connections))) {
		cinfo = g_slist_nth_data (con_man->priv->connections, n);
		conn = g_object_ref (cinfo->conn);

		if (folder_name) {
			g_hash_table_insert (cinfo->folders, g_strdup (folder_name), GINT_TO_POINTER (1));
			c(printf ("Adding folder %s to connection number %d \n", folder_name, n+1));
		}
	}

	c(g_assert (!(con_man->priv->n_connections == g_slist_length (con_man->priv->connections) && !conn)));

	CON_UNLOCK(con_man);

	return conn;
}

static CamelIMAPXServer *
imapx_create_new_connection (CamelIMAPXConnManager *con_man, const gchar *folder_name, GError **error)
{
	CamelIMAPXServer *conn;
	CamelStore *store = con_man->priv->store;
	ConnectionInfo *cinfo = NULL;

	CON_LOCK(con_man);

	camel_service_lock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	conn = camel_imapx_server_new (CAMEL_STORE(store), CAMEL_SERVICE(store)->url);
	if (camel_imapx_server_connect(conn, error)) {
		g_object_ref (conn);
	} else {
		g_object_unref (conn);

		camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);
		CON_UNLOCK (con_man);

		return NULL;
	}

	g_signal_connect (conn, "shutdown", G_CALLBACK (imapx_conn_shutdown), con_man);
	g_signal_connect (conn, "select_changed", G_CALLBACK (imapx_conn_update_select), con_man);

	camel_service_unlock (CAMEL_SERVICE (store), CAMEL_SERVICE_REC_CONNECT_LOCK);

	cinfo = g_new0 (ConnectionInfo, 1);
	cinfo->conn = conn;
	cinfo->folders = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	if (folder_name)
		g_hash_table_insert (cinfo->folders, g_strdup (folder_name), GINT_TO_POINTER (1));

	con_man->priv->connections = g_slist_prepend (con_man->priv->connections, cinfo);

	c(printf ("Created new connection for %s and total connections %d \n", folder_name, g_slist_length (con_man->priv->connections)));

	CON_UNLOCK(con_man);

	return conn;
}

/****************************/

CamelIMAPXConnManager *
camel_imapx_conn_manager_new (CamelStore *store)
{
	CamelIMAPXConnManager *con_man;

	con_man = g_object_new (CAMEL_TYPE_IMAPX_CONN_MANAGER, NULL);
	con_man->priv->store = g_object_ref (store);

	return con_man;
}

void
camel_imapx_conn_manager_set_n_connections (CamelIMAPXConnManager *con_man, guint n_connections)
{
	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	con_man->priv->n_connections = n_connections;
}

CamelIMAPXServer *
camel_imapx_conn_manager_get_connection (CamelIMAPXConnManager *con_man, const gchar *folder_name, GError **error)
{
	CamelIMAPXServer *conn = NULL;

	g_return_val_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man), NULL);

	CON_LOCK(con_man);

	conn = imapx_find_connection (con_man, folder_name);
	if (!conn)
		conn = imapx_create_new_connection (con_man, folder_name, error);

	CON_UNLOCK(con_man);

	return conn;
}

GSList *
camel_imapx_conn_manager_get_connections (CamelIMAPXConnManager *con_man)
{
	GSList *l, *conns = NULL;

	CON_LOCK(con_man);

	for (l = con_man->priv->connections; l != NULL; l = g_slist_next (l)) {
		ConnectionInfo *cinfo = (ConnectionInfo *) l->data;

		conns = g_slist_prepend (conns, g_object_ref (cinfo->conn));
	}

	CON_UNLOCK(con_man);

	return conns;
}

/* Used for handling operations that fails to execute and that needs to removed from folder list */
void
camel_imapx_conn_manager_update_con_info (CamelIMAPXConnManager *con_man, CamelIMAPXServer *conn,
					  const gchar *folder_name)
{
	GSList *l;
	ConnectionInfo *cinfo;
	gboolean found = FALSE;

	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	CON_LOCK(con_man);

	for (l = con_man->priv->connections; l != NULL; l = g_slist_next (l)) {
		cinfo = (ConnectionInfo *) l->data;
		if (cinfo->conn == conn) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		IMAPXJobQueueInfo *jinfo;

		jinfo = camel_imapx_server_get_job_queue_info (cinfo->conn);
		if (!g_hash_table_lookup (jinfo->folders, folder_name)) {
			g_hash_table_remove (cinfo->folders, folder_name);
			c(printf ("Removed folder %s from connection folder list - op done \n", folder_name));
		}
		camel_imapx_destroy_job_queue_info (jinfo);
	}

	CON_UNLOCK(con_man);
}

void
camel_imapx_conn_manager_close_connections (CamelIMAPXConnManager *con_man)
{
	g_return_if_fail (CAMEL_IS_IMAPX_CONN_MANAGER (con_man));

	imapx_prune_connections (con_man);
}

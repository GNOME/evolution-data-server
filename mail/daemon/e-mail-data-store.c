/* e-mail-data-store.c */

#include <string.h>
#include "e-mail-data-store.h"
#include "e-mail-data-folder.h"
#include "e-gdbus-emailstore.h"
#include "mail-ops.h"
#include "utils.h"
#include <glib/gi18n.h>

#define micro(x) if (mail_debug_log(EMAIL_DEBUG_STORE|EMAIL_DEBUG_MICRO)) x;
#define ipc(x) if (mail_debug_log(EMAIL_DEBUG_STORE|EMAIL_DEBUG_IPC)) x;


G_DEFINE_TYPE (EMailDataStore, e_mail_data_store, G_TYPE_OBJECT)

#define DATA_STORE_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMAIL_TYPE_DATA_STORE, EMailDataStorePrivate))

typedef struct _EMailDataStorePrivate EMailDataStorePrivate;

struct _EMailDataStorePrivate
{
	EGdbusStoreMS *gdbus_object;

	CamelStore *store;
	char *url;

	GMutex *folders_lock;
	/* 'uri' -> EBookBackend */
	GHashTable *folders;

	GMutex *datafolders_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *datafolders;

	char *object_path;

	
};

static void
e_mail_data_store_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_mail_data_store_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_mail_data_store_dispose (GObject *object)
{
  G_OBJECT_CLASS (e_mail_data_store_parent_class)->dispose (object);
}

static void
e_mail_data_store_finalize (GObject *object)
{
  G_OBJECT_CLASS (e_mail_data_store_parent_class)->finalize (object);
}

static void
e_mail_data_store_class_init (EMailDataStoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EMailDataStorePrivate));

  object_class->get_property = e_mail_data_store_get_property;
  object_class->set_property = e_mail_data_store_set_property;
  object_class->dispose = e_mail_data_store_dispose;
  object_class->finalize = e_mail_data_store_finalize;
}

static gchar *
construct_mail_store_path (char *full_name)
{
	static volatile gint counter = 1;
	int i, len;
	char *path = g_strdup_printf (
		       "/org/gnome/evolution/dataserver/mail/folder/%s/%d/%u",
		       full_name, getpid (), g_atomic_int_exchange_and_add (&counter, 1));
	len = strlen(path);
	for (i=0; i<len ; i++)
		if (path[i] == '.')
			path[i] = '_';
		else if (path[i] == '#')
			path[i] = '_';
		else if (path[i] == '(')
			path[i] = '_';
		else if (path[i] == '@')
			path[i] = '_';
		else if (path[i] == ')')
			path[i] = '_';
		else if (path[i] == ' ')
			path[i] = '_';
		else if (path[i] == '[' || path[i] == ']')
			path[i] = '_';
	
	

	return path;
}


static void
convert_folder_info (CamelFolderInfo *info, GVariantBuilder *builder)
{
	while (info) {
		g_variant_builder_add (builder, "(sssuii)", 
					info->uri,
					info->name,
					info->full_name,
					info->flags,
					info->unread,
					info->total);

		convert_folder_info (info->child, builder);
		info = info->next;
	}
}

typedef struct _get_folder_info_data {
	EGdbusStoreMS *object; 
	GDBusMethodInvocation *invocation; 
	EMailDataStore *mstore;
}GFIData;

static gboolean 
handle_get_folder_info_cb (CamelStore *store, CamelFolderInfo *info, gpointer data, GError *error)
{
	GFIData *gfi_data = (GFIData *)data;
	EMailDataStore *mstore = gfi_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	
	GVariantBuilder *builder;
	GVariant *variant;

	if (!info) {
		g_warning ("Unable to get folder info on Store %p: %s\n", store, error ? error->message: "");
		if (error)
			g_dbus_method_invocation_return_gerror (gfi_data->invocation, error);
		else
			g_dbus_method_invocation_return_error (gfi_data->invocation, CAMEL_ERROR, CAMEL_STORE_ERROR_NO_FOLDER, _("Unable to fetch requested folder info"));
			
		ipc (printf("EMailDataStore: get folder info failed : %s - %s\n", priv->object_path, error ? error->message : ""));

		return FALSE;
	}

	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

	convert_folder_info (info, builder);
	/* Added a empty entry */
	g_variant_builder_add (builder, "(sssuii)", "", "", "", 0, -1, -1);
	
	variant = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);
	
	egdbus_store_ms_complete_get_folder_info (gfi_data->object, gfi_data->invocation, variant);
	g_variant_unref (variant);

	g_free (gfi_data);
	ipc (printf("EMailDataStore: get folder info success: %s\n", priv->object_path));

	return TRUE;
}

static gboolean
impl_Mail_getFolderInfo (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, char *top, guint32 flags, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	GFIData *gfi_data = g_new0 (GFIData, 1);

	gfi_data->object = object;
	gfi_data->invocation = invocation;
	gfi_data->mstore = mstore;
	
	ipc (printf("EMailDataStore: get folder info: %s - %s: %d\n", priv->object_path, top ? top : "", flags));

	mail_get_folderinfo_full (priv->store, top, flags, NULL, handle_get_folder_info_cb, gfi_data);

	return TRUE;
}

typedef struct _email_get_folder_data {
	EMailDataStore *mstore;
	EGdbusStoreMS *object;
	GDBusMethodInvocation *invocation;
	char *folder_name;
	gboolean junk;
	gboolean trash;
	gboolean inbox;
}EMailGetFolderData;

static void
handle_mail_get_folder (CamelFolder *folder, gpointer data, GError *error)
{
	EMailGetFolderData *send_data = (EMailGetFolderData *)data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	char *new_name;
	EMailDataFolder *efolder = NULL;
	char *path;

	if (folder == NULL) {
		g_mutex_unlock (priv->folders_lock);
		g_mutex_unlock (priv->datafolders_lock);
		g_warning ("Unable to get folder : %s\n", error->message);
		ipc (printf("EMailDataStore: get folder failed : %s - %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}

	new_name = g_strdup (camel_folder_get_full_name (folder));
	g_mutex_lock (priv->folders_lock);
	g_mutex_lock (priv->datafolders_lock);

	g_hash_table_insert (priv->folders, new_name, folder);
	efolder = e_mail_data_folder_new (folder);

	path = construct_mail_store_path (new_name);
	e_mail_data_folder_register_gdbus_object (efolder, g_dbus_method_invocation_get_connection (send_data->invocation), path, NULL);
	g_hash_table_insert (priv->datafolders, g_strdup(new_name), efolder);

	if (send_data->folder_name)
		egdbus_store_ms_complete_get_folder (send_data->object, send_data->invocation, path);
	else if (send_data->inbox)
		egdbus_store_ms_complete_get_inbox (send_data->object, send_data->invocation, path);
	else if (send_data->junk)
		egdbus_store_ms_complete_get_junk (send_data->object, send_data->invocation, path);
	else
		egdbus_store_ms_complete_get_trash (send_data->object, send_data->invocation, path);

	ipc (printf("EMailDataStore: get folder : %s %s: %s\n", priv->object_path, new_name, path));

	g_mutex_unlock (priv->folders_lock);
	g_mutex_unlock (priv->datafolders_lock);

	g_free (send_data->folder_name);
	g_free (send_data);
	g_free (path);
}

static gboolean
impl_Mail_getFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, const gchar *full_name, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	CamelFolder *folder;
	gchar *path=NULL;
	EMailGetFolderData *send_data;
	EMailDataFolder *efolder = NULL;

	folder = g_hash_table_lookup (priv->folders, full_name);

	ipc (printf("EMailDataStore: get folder %s - %s\n", priv->object_path, full_name));

	if (folder == NULL) {
		char *new_name = g_strdup (full_name);

		send_data = g_new0 (EMailGetFolderData, 1);
		send_data->mstore = mstore;
		send_data->object = object;
		send_data->folder_name = new_name;
		send_data->invocation = invocation;

		mail_get_folder_from_name (priv->store, new_name, FALSE, FALSE, FALSE, 0,
					handle_mail_get_folder, send_data, mail_msg_unordered_push);

		return TRUE;
	}


	efolder = g_hash_table_lookup (priv->datafolders, full_name);
	path = (char *) e_mail_data_folder_get_path (efolder);

	ipc (printf("EMailDataStore: get folder success: %s %s: %s d\n", priv->object_path, full_name, path));

	egdbus_store_ms_complete_get_folder (object, invocation, path);

	return TRUE;
}

static gboolean
impl_Mail_getInbox (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailGetFolderData *send_data;


	send_data = g_new0 (EMailGetFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->inbox = TRUE;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: get inbox: %s\n", priv->object_path));
	
	mail_get_folder_from_name (priv->store, NULL, FALSE, FALSE, TRUE, 0,
				   handle_mail_get_folder, send_data, mail_msg_unordered_push);
	
	return TRUE;

}

static gboolean
impl_Mail_getTrash (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailGetFolderData *send_data;


	send_data = g_new0 (EMailGetFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->trash = TRUE;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: get trash: %s\n", priv->object_path));

	mail_get_folder_from_name (priv->store, NULL, FALSE, TRUE, FALSE, 0,
				   handle_mail_get_folder, send_data, mail_msg_unordered_push);
	
	return TRUE;

}

static gboolean
impl_Mail_getJunk (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailGetFolderData *send_data;


	send_data = g_new0 (EMailGetFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->junk = TRUE;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: get junk: %s\n", priv->object_path));

	mail_get_folder_from_name (priv->store, NULL, TRUE, FALSE, FALSE, 0,
				   handle_mail_get_folder, send_data, mail_msg_unordered_push);
	
	return TRUE;

}

typedef struct _email_cdr_folder_data {
	EMailDataStore *mstore;
	EGdbusStoreMS *object;
	GDBusMethodInvocation *invocation;
}EMailCDRFolderData;

static void 
handle_create_folder_cb (CamelFolderInfo *fi, gpointer user_data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)user_data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);	
	GVariantBuilder *builder;
	GVariant *variant;
	
	if (!fi) {
		/* Handle error */
		g_warning ("Unable to create folder: %s\n", error->message);
		ipc (printf("EMailDataStore: folder create failed : %s - %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}
	
	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

	convert_folder_info (fi, builder);
	/* Added a empty entry */
	g_variant_builder_add (builder, "(sssuii)", "", "", "", 0, -1, -1);
	
	variant = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);
	
	ipc (printf("EMailDataStore: folder create success: %s\n", priv->object_path));

	egdbus_store_ms_complete_create_folder (send_data->object, send_data->invocation, variant);
	g_variant_unref (variant);

	g_free (send_data);
}

static gboolean
impl_Mail_createFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, const char *parent, const char *folder_name, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;
	
	ipc (printf("EMailDataStore: folder create: %s\n", folder_name));

	mail_create_folder (priv->store, parent, folder_name, handle_create_folder_cb, send_data);
	return TRUE;

}

static void 
handle_delete_folder_cb (gpointer user_data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)user_data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	
	if (error && error->message) {
		/* Handle error */
		g_warning ("Unable to delete folder: %s\n", error->message);
		ipc (printf("EMailDataStore: folder delete failed : %s - %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}
	
	ipc (printf("EMailDataStore: folder delete success: %s\n", priv->object_path));

	egdbus_store_ms_complete_delete_folder (send_data->object, send_data->invocation, TRUE);

	g_free (send_data);
}

static gboolean
impl_Mail_deleteFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, const char *folder_name, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: folder delete: %s - %s\n", priv->object_path, folder_name));

	mail_delete_folder (priv->store, folder_name, handle_delete_folder_cb, send_data);
	return TRUE;

}

static void 
handle_rename_folder_cb (gpointer user_data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)user_data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	
	if (error && error->message) {
		/* Handle error */
		g_warning ("Unable to rename folder: %s\n", error->message);
		ipc (printf("EMailDataStore: folder rename failed : %s - %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}
	ipc (printf("EMailDataStore: folder rename success: %s\n", priv->object_path));
	
	egdbus_store_ms_complete_rename_folder (send_data->object, send_data->invocation, TRUE);

	g_free (send_data);
}


static gboolean
impl_Mail_renameFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, const char *old_name, const char *new_name, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: folder rename: %s: %s to %s\n", priv->object_path, old_name, new_name));

	mail_rename_folder (priv->store, old_name, new_name, handle_rename_folder_cb, send_data);
	return TRUE;

}

static gboolean
impl_Mail_supportsSubscriptions (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	gboolean support;

	support = camel_store_supports_subscriptions (priv->store);
	
	ipc (printf("EMailDataStore: supports subscription: %s - %d\n", priv->object_path, support));

	egdbus_store_ms_complete_supports_subscriptions (object, invocation, support);

	return TRUE;
}

typedef struct _email_folder_sub_data {
	EMailDataStore *mstore;
	EGdbusStoreMS *object;
	GDBusMethodInvocation *invocation;
	MailFolderSubscription operation;
}EMailFolderSubData;

static void 
handle_folder_subscriptions (gboolean success, gpointer data, GError *error)
{
	EMailFolderSubData *send_data = (EMailFolderSubData *)data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);

	if (error && error->message) {
		g_warning ("folder sub: %s\n", error->message);
		ipc (printf("EMailDataStore: folder-sub failed : %s - %d: %s\n", priv->object_path, send_data->operation, error->message));
		
		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}

	if (send_data->operation == FOLDER_IS_SUBSCRIBED)
		egdbus_store_ms_complete_is_folder_subscribed (send_data->object, send_data->invocation, success);
	else if (send_data->operation == FOLDER_SUBSCRIBE)
		egdbus_store_ms_complete_subscribe_folder (send_data->object, send_data->invocation, TRUE);
	else
		egdbus_store_ms_complete_unsubscribe_folder (send_data->object, send_data->invocation, TRUE);

	ipc (printf("EMailDataStore: folder-sub success: %s - %d\n", priv->object_path, send_data->operation));

	g_free (send_data);
}

static gboolean
impl_Mail_isFolderSubscribed (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, char *folder, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailFolderSubData *send_data;

	send_data = g_new0 (EMailFolderSubData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;
	send_data->operation = FOLDER_IS_SUBSCRIBED;

	ipc (printf("EMailDataStore: folder issubscribed: %s\n", priv->object_path));

	mail_folder_subscription (priv->store, folder, send_data->operation, handle_folder_subscriptions, send_data);
	return TRUE;
}

static gboolean
impl_Mail_subscribeFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, char *folder, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailFolderSubData *send_data;

	send_data = g_new0 (EMailFolderSubData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;
	send_data->operation = FOLDER_SUBSCRIBE;

	ipc (printf("EMailDataStore: folder subscribe: %s\n", priv->object_path));

	mail_folder_subscription (priv->store, folder, send_data->operation, handle_folder_subscriptions, send_data);
	return TRUE;
}

static gboolean
impl_Mail_unsubscribeFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, char *folder, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailFolderSubData *send_data;

	send_data = g_new0 (EMailFolderSubData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;
	send_data->operation = FOLDER_UNSUBSCRIBE;

	ipc (printf("EMailDataStore: folder unsubscribe: %s\n", priv->object_path));
	
	mail_folder_subscription (priv->store, folder, send_data->operation, handle_folder_subscriptions, send_data);
	return TRUE;
}

static void
handle_mail_sync (CamelStore *store, gpointer data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);

	if (error && error->message) {
		g_warning ("Error while syncing store: %s\n", error->message);
		ipc (printf("EMailDataStore: sync: failed: %s %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}
	ipc (printf("EMailDataStore: sync: success: %s\n", priv->object_path));

	egdbus_store_ms_complete_sync (send_data->object, send_data->invocation, TRUE);
}

static gboolean
impl_Mail_sync (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, gboolean expunge, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;
	
	ipc (printf("EMailDataStore: sync: %s\n", priv->object_path));

	mail_sync_store (priv->store, expunge, handle_mail_sync, send_data);
	return TRUE;
}

static void
handle_mail_noop (CamelStore *store, gpointer data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)data;
	EMailDataStore *mstore = send_data->mstore;
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);

	if (error && error->message) {
		g_warning ("Error while noop for store: %s\n", error->message);
		ipc (printf("EMailDataStore: noop: failed: %s %s\n", priv->object_path, error->message));

		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}

	ipc (printf("EMailDataStore: noop: success: %s\n", priv->object_path));
	egdbus_store_ms_complete_noop (send_data->object, send_data->invocation, TRUE);
}


static gboolean
impl_Mail_noop (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;

	ipc (printf("EMailDataStore: noop: %s\n", priv->object_path));

	mail_noop_store (priv->store, handle_mail_noop, send_data);
	return TRUE;
}

static void
handle_mail_can_refresh_folder_cb (gboolean success, gpointer data, GError *error)
{
	EMailCDRFolderData *send_data = (EMailCDRFolderData *)data;

	if (error && error->message) {
		g_warning ("Error while noop for store: %s\n", error->message);
		ipc (printf("EMailDataStore: Refresh Folder: failed: %s\n", error->message));
		g_dbus_method_invocation_return_gerror (send_data->invocation, error);
		return;
	}

	ipc (printf("EMailDataStore: Can Refresh Folder: success\n"));

	egdbus_store_ms_complete_can_refresh_folder (send_data->object, send_data->invocation, success);
}



static gboolean
impl_Mail_canRefreshFolder (EGdbusStoreMS *object, GDBusMethodInvocation *invocation, GVariant *variant, EMailDataStore *mstore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(mstore);
	EMailCDRFolderData *send_data;
	CamelFolderInfo *info = NULL;
	GVariantIter iter;
	char *full_name, *name, *uri;
	guint32 flag;
	int total, unread;

	send_data = g_new0 (EMailCDRFolderData, 1);
	send_data->mstore = mstore;
	send_data->object = object;
	send_data->invocation = invocation;

	g_variant_iter_init (&iter, variant);
	while (g_variant_iter_next (&iter, "(sssuii)", &uri, &name, &full_name, &flag, &unread, &total)) {
		if (uri && *uri) {
			info = camel_folder_info_new ();
			info->uri = uri;
			info->name = name;
			info->full_name = full_name;
			info->flags = flag;
			info->unread = unread;
			info->total = total;
			ipc (printf("EMailDataStore: can refresh %s\n", info->full_name));

			break;
		}
	}

	mail_can_refresh_folder (priv->store, info, handle_mail_can_refresh_folder_cb, send_data);
	return TRUE;
}


static void
e_mail_data_store_init (EMailDataStore *self)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(self);

	priv->object_path = NULL;

	priv->folders_lock = g_mutex_new ();
	priv->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	priv->datafolders_lock = g_mutex_new ();
	priv->datafolders = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	priv->gdbus_object = egdbus_store_ms_stub_new ();
	g_signal_connect (priv->gdbus_object, "handle-get-folder", G_CALLBACK (impl_Mail_getFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-get-folder-info", G_CALLBACK (impl_Mail_getFolderInfo), self);
	g_signal_connect (priv->gdbus_object, "handle-get-inbox", G_CALLBACK (impl_Mail_getInbox), self);
	g_signal_connect (priv->gdbus_object, "handle-get-trash", G_CALLBACK (impl_Mail_getTrash), self);
	g_signal_connect (priv->gdbus_object, "handle-get-junk", G_CALLBACK (impl_Mail_getJunk), self);
	g_signal_connect (priv->gdbus_object, "handle-create-folder", G_CALLBACK (impl_Mail_createFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-delete-folder", G_CALLBACK (impl_Mail_deleteFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-rename-folder", G_CALLBACK (impl_Mail_renameFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-supports-subscriptions", G_CALLBACK (impl_Mail_supportsSubscriptions), self);
	g_signal_connect (priv->gdbus_object, "handle-is-folder-subscribed", G_CALLBACK (impl_Mail_isFolderSubscribed), self);
	g_signal_connect (priv->gdbus_object, "handle-subscribe-folder", G_CALLBACK (impl_Mail_subscribeFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-unsubscribe-folder", G_CALLBACK (impl_Mail_unsubscribeFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-sync", G_CALLBACK (impl_Mail_sync), self);
	g_signal_connect (priv->gdbus_object, "handle-noop", G_CALLBACK (impl_Mail_noop), self);
	g_signal_connect (priv->gdbus_object, "handle-can-refresh-folder", G_CALLBACK (impl_Mail_canRefreshFolder), self);

	
}

EMailDataStore*
e_mail_data_store_new (CamelStore *store, const char *url)
{
	EMailDataStore *estore;
	EMailDataStorePrivate *priv;

  	estore = g_object_new (EMAIL_TYPE_DATA_STORE, NULL);
	priv = DATA_STORE_PRIVATE(estore);
	priv->store = g_object_ref(store);
	priv->url = g_strdup (url);

	return estore;
}

static GVariant *
variant_from_info (CamelFolderInfo *info)
{
	GVariantBuilder *builder;
	GVariant *variant;

	builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);

	convert_folder_info (info, builder);
	/* Added a empty entry */
	g_variant_builder_add (builder, "(sssuii)", "", "", "", 0, -1, -1);
	
	variant = g_variant_builder_end (builder);
	g_variant_ref (variant);
	g_variant_builder_unref (builder);

	return variant;
}

static void
store_folder_subscribed_cb (CamelStore *store,
                            CamelFolderInfo *info,
                            EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GVariant *variant;

	variant = variant_from_info (info);
	egdbus_store_ms_emit_folder_subscribed (priv->gdbus_object, variant);

	g_variant_unref (variant);
}

static void
store_folder_unsubscribed_cb (CamelStore *store,
                              CamelFolderInfo *info,
                              EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GVariant *variant;

	variant = variant_from_info (info);
	egdbus_store_ms_emit_folder_unsubscribed (priv->gdbus_object, variant);

	g_variant_unref (variant);
	
}

static void
store_folder_created_cb (CamelStore *store,
                         CamelFolderInfo *info,
                         EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GVariant *variant;

	variant = variant_from_info (info);
	egdbus_store_ms_emit_folder_created (priv->gdbus_object, variant);

	g_variant_unref (variant);
	
}

static void
store_folder_opened_cb (CamelStore *store,
                        CamelFolder *folder,
                        EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	const char *path;
	char *full_name;
	EMailDataFolder *efolder;

	full_name = camel_folder_get_full_name (folder);
	efolder = g_hash_table_lookup (priv->datafolders, full_name);
	if (!efolder) /* Don't bother to return about folders that aren't being used by the client. */
		return;
	path = e_mail_data_folder_get_path (efolder);
	egdbus_store_ms_emit_folder_opened (priv->gdbus_object, path);
}

static void
store_folder_deleted_cb (CamelStore *store,
                         CamelFolderInfo *info,
                         EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GVariant *variant;

	variant = variant_from_info (info);
	egdbus_store_ms_emit_folder_deleted (priv->gdbus_object, variant);

	g_variant_unref (variant);	
}

static void
store_folder_renamed_cb (CamelStore *store,
                         const gchar *old_name,
                         CamelFolderInfo *info,
                         EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GVariant *variant;

	variant = variant_from_info (info);
	egdbus_store_ms_emit_folder_renamed (priv->gdbus_object, old_name, variant);

	g_variant_unref (variant);	
}


guint 
e_mail_data_store_register_gdbus_object (EMailDataStore *estore, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);

	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	priv->object_path = g_strdup (object_path);
	g_object_set_data ((GObject *)priv->store, "object-path", priv->object_path);

	ipc (printf("EMailDataStore: Registering gdbus path: %s: %p\n", object_path, priv->store));

	g_signal_connect (
		priv->store, "folder-opened",
		G_CALLBACK (store_folder_opened_cb), estore);
	g_signal_connect (
		priv->store, "folder-created",
		G_CALLBACK (store_folder_created_cb), estore);
	g_signal_connect (
		priv->store, "folder-deleted",
		G_CALLBACK (store_folder_deleted_cb), estore);
	g_signal_connect (
		priv->store, "folder-renamed",
		G_CALLBACK (store_folder_renamed_cb), estore);
	g_signal_connect (
		priv->store, "folder-subscribed",
		G_CALLBACK (store_folder_subscribed_cb), estore);
	g_signal_connect (
		priv->store, "folder-unsubscribed",
		G_CALLBACK (store_folder_unsubscribed_cb), estore);

 	return g_dbus_interface_register_object (G_DBUS_INTERFACE (priv->gdbus_object),
               	                     	connection,
                                    	object_path,
                                    	error);	
}

const char *
e_mail_data_store_get_path (EMailDataStore *estore)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);

	return priv->object_path;
}

CamelFolder *
e_mail_data_store_get_camel_folder (EMailDataStore *estore, const char *path)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, priv->datafolders);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		EMailDataFolder *efolder = (EMailDataFolder *)value;
		const char *opath = e_mail_data_folder_get_path (efolder);

		if (strcmp (opath, path) == 0) {
			CamelFolder *f = e_mail_data_folder_get_camel_folder (efolder);
			micro(printf("e_mail_data_store_get_camel_folder: %s %p\n", path, f));

			return f;
		}
	}

	micro(printf("e_mail_data_store_get_camel_folder: %s NULL\n", path));
	return NULL;
}

const char *
e_mail_data_store_get_folder_path (EMailDataStore *estore, GDBusConnection *connection, CamelFolder *folder)
{
	EMailDataStorePrivate *priv = DATA_STORE_PRIVATE(estore);
	const char *full_name;
	EMailDataFolder *efolder;
	gchar *path;

	full_name = camel_folder_get_full_name (folder);


	g_mutex_lock (priv->folders_lock);
	g_mutex_lock (priv->datafolders_lock);
	
	efolder = g_hash_table_lookup (priv->datafolders, full_name);
	if (!efolder) {

		g_hash_table_insert (priv->folders, g_strdup(full_name), folder);
		efolder = e_mail_data_folder_new (folder);
		path = construct_mail_store_path (full_name);
		e_mail_data_folder_register_gdbus_object (efolder, connection, path, NULL);
		g_hash_table_insert (priv->datafolders, g_strdup(full_name), efolder);
		micro (printf("EMailDataStore: Created object from folder : %s %s: %s\n", priv->object_path, full_name, path));
		g_free (path);
	}

	g_mutex_unlock (priv->folders_lock);
	g_mutex_unlock (priv->datafolders_lock);

	return e_mail_data_folder_get_path (efolder);
}

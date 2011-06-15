/* e-mail-data-session.c */

#include "e-mail-data-session.h"
#include "e-mail-local.h"
#include "e-mail-data-store.h"
#include "e-gdbus-emailsession.h"
#include <camel/camel.h>
#include <gio/gio.h>
#include "mail-ops.h"
#include "mail-tools.h"
#include "mail-send-recv.h"
#include "utils.h"
#include <libedataserver/e-account-list.h>
#include <libedataserverui/e-passwords.h>
#include <string.h>

extern CamelSession *session;
#define micro(x) if (mail_debug_log(EMAIL_DEBUG_SESSION|EMAIL_DEBUG_MICRO)) x;
#define ipc(x) if (mail_debug_log(EMAIL_DEBUG_SESSION|EMAIL_DEBUG_IPC)) x;


G_DEFINE_TYPE (EMailDataSession, e_mail_data_session, G_TYPE_OBJECT)

#define DATA_SESSION_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMAIL_TYPE_DATA_SESSION, EMailDataSessionPrivate))

typedef struct _EMailDataSessionPrivate EMailDataSessionPrivate;

struct _EMailDataSessionPrivate
{
	EGdbusSessionCS *gdbus_object;

	GMutex *stores_lock;
	/* 'uri' -> EBookBackend */
	GHashTable *stores;

	GMutex *datastores_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *datastores;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDataBooks */
	GHashTable *connections;

	guint exit_timeout;
	
};

static gchar *
construct_mail_session_path (void)
{
	static volatile gint counter = 1;

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/mail/store/%d/%u",
		getpid (), g_atomic_int_exchange_and_add (&counter, 1));
}

typedef struct _email_get_store_data {
	EMailDataSession *msession;
	EGdbusSessionCS *object;
	GDBusMethodInvocation *invocation;
	
}EMailGetStoreData;

static void
process_store (EMailDataSession *msession, const char *url, CamelStore *store, EGdbusSessionCS *object, GDBusMethodInvocation *invocation)
{
	char *path;
	EMailDataStore *estore;
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	const gchar *sender;
	GList *list = NULL;

	/* Hold store & datastore the lock when you are calling this function */
	path = construct_mail_session_path ();
	estore = e_mail_data_store_new (store, url);

	g_hash_table_insert (priv->datastores, store, estore);
	e_mail_data_store_register_gdbus_object (estore, g_dbus_method_invocation_get_connection (invocation), path, NULL);
	
	g_mutex_lock (priv->connections_lock);
	sender = g_dbus_method_invocation_get_sender (invocation);

	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, estore);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);

	ipc (printf("EMailDataSession: Get Store: Success %s  for sender: '%s'\n", path, sender));

	egdbus_session_cs_complete_get_store (object, invocation, path);
}

static void 
get_store_cb (gchar *uri, CamelStore *store, gpointer data, GError *error)
{
	EMailGetStoreData *session_data = (EMailGetStoreData *)data;
	EMailDataSession *msession = session_data->msession;
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	char *uri_key = g_strdup (uri);

	if (store == NULL) {
		/* Must handle error now... */
		g_mutex_unlock (priv->datastores_lock);
		g_mutex_unlock (priv->stores_lock);
		g_warning ("Unable to get store %s: %s\n", uri, error->message);
		ipc(printf("Unable to get store %s: %s\n", uri, error->message));
		g_dbus_method_invocation_return_gerror (session_data->invocation, error);
		return;
	}

	g_mutex_lock (priv->stores_lock);
	g_mutex_lock (priv->datastores_lock);
	
	g_hash_table_insert (priv->stores, uri_key, store);

	process_store (msession, uri, store, session_data->object, session_data->invocation);
	
	g_mutex_unlock (priv->datastores_lock);
	g_mutex_unlock (priv->stores_lock);

	g_free (data);
}

static gboolean
impl_Mail_getStore (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, const gchar *url, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	CamelStore *store;

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	if (!url || !*url) {
		ipc(printf("GetStore: empty url passed\n"));
		return FALSE;
	}
	
	g_mutex_lock (priv->stores_lock);
	g_mutex_lock (priv->datastores_lock);
	store = g_hash_table_lookup (priv->stores, url);
	
	if (store == NULL) {
		EMailGetStoreData *store_data = g_new0 (EMailGetStoreData, 1);

		store_data->object = object;
		store_data->msession = msession;
		store_data->invocation = invocation;

		g_mutex_unlock (priv->datastores_lock);
		g_mutex_unlock (priv->stores_lock);

		mail_get_store (url, NULL, get_store_cb, store_data);
		return TRUE;
	}


	process_store (msession, url, store, object, invocation);
	
	g_mutex_unlock (priv->datastores_lock);
	g_mutex_unlock (priv->stores_lock);
	
	return TRUE;
}

static gboolean
impl_Mail_getLocalStore (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	CamelStore *store;
	EMailDataStore *estore;
	char *path = NULL;
	char *url=NULL;
	GList *list;
	const gchar *sender;

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}
	
	store = e_mail_local_get_store ();

	g_mutex_lock (priv->datastores_lock);
	estore = g_hash_table_lookup (priv->datastores, store);
	
	if (estore == NULL) {
		url = camel_url_to_string(((CamelService *)store)->url, CAMEL_URL_HIDE_ALL);
		path = construct_mail_session_path ();
		estore = e_mail_data_store_new (store, url);

		g_hash_table_insert (priv->datastores, store, estore);
		e_mail_data_store_register_gdbus_object (estore, g_dbus_method_invocation_get_connection (invocation), path, NULL);
	

	} else 
		path = g_strdup (e_mail_data_store_get_path (estore));

	g_mutex_unlock (priv->datastores_lock);

	g_mutex_lock (priv->connections_lock);
	sender = g_dbus_method_invocation_get_sender (invocation);

	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, estore);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);

	ipc (printf("EMailDataSession: Get Local Store: Success %s  for sender: '%s'\n", path, sender));

	egdbus_session_cs_complete_get_local_store (object, invocation, path);

	g_free (path);
	g_free(url);
	
	return TRUE;
}

static gboolean
impl_Mail_getLocalFolder (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, const char *type, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	CamelStore *store;
	CamelFolder *folder;
	EMailLocalFolder ftype;
	EMailDataStore *estore;
	char *fpath, *spath;
	char *url;

	if (type[0] == 'i' || type[0] == 'I')
		ftype = E_MAIL_FOLDER_INBOX;
	else 	if (type[0] == 'd' || type[0] == 'D')
		ftype = E_MAIL_FOLDER_DRAFTS;
	else if (type[0] == 'o' || type[0] == 'O')
		ftype = E_MAIL_FOLDER_OUTBOX;
	else if (type[0] == 's' || type[0] == 'S')
		ftype = E_MAIL_FOLDER_SENT;
	else if (type[0] == 't' || type[0] == 'T')
		ftype = E_MAIL_FOLDER_TEMPLATES;
	else 
		ftype = E_MAIL_FOLDER_LOCAL_INBOX;
	
	folder = e_mail_local_get_folder (ftype);
	store = e_mail_local_get_store ();

	g_mutex_lock (priv->datastores_lock);
	estore = g_hash_table_lookup (priv->datastores, store);
	
	if (estore == NULL) {
		url = camel_url_to_string(((CamelService *)store)->url, CAMEL_URL_HIDE_ALL);
		spath = construct_mail_session_path ();
		estore = e_mail_data_store_new (store, url);

		g_hash_table_insert (priv->datastores, store, estore);
		e_mail_data_store_register_gdbus_object (estore, g_dbus_method_invocation_get_connection (invocation), spath, NULL);
		g_free (url);
		g_free (spath);	
	}

	g_mutex_unlock (priv->datastores_lock);

	fpath = e_mail_data_store_get_folder_path (estore, g_dbus_method_invocation_get_connection (invocation), folder);

	egdbus_session_cs_complete_get_local_folder (object, invocation, fpath);

	return TRUE;
}

static void
get_folder_done (gchar *uri, CamelFolder *folder,  gpointer d, GError *error)
{
	EMailGetStoreData *data = (EMailGetStoreData *)d;
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(data->msession);
	CamelStore *store;
	char *fpath, *spath;
	char *url;
	EMailDataStore *estore;
	const gchar *sender;
	GList *list;

	if (folder == NULL) {
		ipc(printf("Unable to get folder: %s\n", error->message));
		g_dbus_method_invocation_return_gerror (data->invocation, error);		
	}

	store = camel_folder_get_parent_store (folder);
	g_mutex_lock (priv->datastores_lock);
	estore = g_hash_table_lookup (priv->datastores, store);
	
	if (estore == NULL) {
		url = camel_url_to_string(((CamelService *)store)->url, CAMEL_URL_HIDE_ALL);
		spath = construct_mail_session_path ();
		estore = e_mail_data_store_new (store, url);

		g_hash_table_insert (priv->datastores, store, estore);
		e_mail_data_store_register_gdbus_object (estore, g_dbus_method_invocation_get_connection (data->invocation), spath, NULL);
		g_free (url);
		g_free (spath);	
	}

	g_mutex_unlock (priv->datastores_lock);

	g_mutex_lock (priv->connections_lock);
	sender = g_dbus_method_invocation_get_sender (data->invocation);

	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, estore);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);
	
	fpath = e_mail_data_store_get_folder_path (estore, g_dbus_method_invocation_get_connection (data->invocation), folder);

	egdbus_session_cs_complete_get_folder_from_uri (data->object, data->invocation, fpath);

	ipc (printf("EMailDataSession: Get Folder from URI : Success %s  for sender: '%s'\n", fpath, sender));

	g_free (data);
}
static gboolean
impl_Mail_getFolderFromUri (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, const char *uri, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	EMailGetStoreData *data = g_new0(EMailGetStoreData, 1);

	data->invocation = invocation;
	data->msession = msession;
	data->object = object;

	mail_get_folder (uri, 0,  get_folder_done, data, mail_msg_unordered_push);

	return TRUE;
}

static gboolean
impl_Mail_findPassword (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, const char *key, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	char *password;

	ipc(printf("Finding Password for: %s\n", key));
	password = e_passwords_get_password ("Mail", key);

	egdbus_session_cs_complete_find_password (object, invocation, password);
	g_free (password);

	return TRUE;
}


static gboolean
impl_Mail_addPassword (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, const char *key, const char *password, gboolean remember, EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);

	ipc(printf("Adding Password for: %s (remember: %d)\n", key, remember));
	e_passwords_add_password (key, password);
	if (remember)
		e_passwords_remember_password ("Mail", key);

	egdbus_session_cs_complete_add_password (object, invocation);

	return TRUE;
}

static gboolean
impl_Mail_sendReceive (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, EMailDataSession *msession)
{
	ipc(printf("Initiating Send/Receive\n"));

	mail_send_receive (NULL);

	egdbus_session_cs_complete_send_receive (object, invocation);
	return TRUE;
}
static gboolean
impl_Mail_fetchAccount (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, char *uid, EMailDataSession *msession)
{
	EIterator *iter;
	EAccountList *accounts;
	EAccount *account;
	
	accounts = e_get_account_list ();
	for (iter = e_list_get_iterator ((EList *)accounts);
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		account = (EAccount *) e_iterator_get (iter);
		if (account->uid && strcmp (account->uid, uid) == 0) {
			mail_fetch_account (account);
		}
	}

	egdbus_session_cs_complete_fetch_account (object, invocation);
	return TRUE;
}

static void
fetch_old_messages_done (gboolean still_more, EMailGetStoreData *data)
{
	ipc(printf("Done: Fetch old messages in POP: %d\n", still_more));
	egdbus_session_cs_complete_fetch_old_messages (data->object, data->invocation, still_more);

	g_free (data);
}

static gboolean
impl_Mail_fetchOldMessages (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, char *uid, int count, EMailDataSession *msession)
{
	EIterator *iter;
	EAccountList *accounts;
	EAccount *account;
	EMailGetStoreData *data = g_new0(EMailGetStoreData, 1);

	data->invocation = invocation;
	data->msession = msession;
	data->object = object;
	
	accounts = e_get_account_list ();
	for (iter = e_list_get_iterator ((EList *)accounts);
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		account = (EAccount *) e_iterator_get (iter);
		if (account->uid && strcmp (account->uid, uid) == 0) {
			const gchar *uri;
			gboolean keep_on_server;

			uri = e_account_get_string (
				account, E_ACCOUNT_SOURCE_URL);
			keep_on_server = e_account_get_bool (
				account, E_ACCOUNT_SOURCE_KEEP_ON_SERVER);
			mail_fetch_mail (uri, keep_on_server,
				 E_FILTER_SOURCE_INCOMING,
				 NULL, count,
				 NULL, NULL,
				 NULL, NULL,
				 (void (*)(const gchar *, void *)) fetch_old_messages_done, data);
		}
	}

	return TRUE;
}

static gboolean
impl_Mail_cancelOperations (EGdbusSessionCS *object, GDBusMethodInvocation *invocation, EMailDataSession *msession)
{
	ipc(printf("Canceling all Mail Operations\n"));

	/* This is the only known reliable way to cancel an issued operation. No harm in canceling this. */
	mail_cancel_all ();

	egdbus_session_cs_complete_cancel_operations (object, invocation);
	return TRUE;
}

static void
e_mail_data_session_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_mail_data_session_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_mail_data_session_dispose (GObject *object)
{
  G_OBJECT_CLASS (e_mail_data_session_parent_class)->dispose (object);
}

static void
e_mail_data_session_finalize (GObject *object)
{
  G_OBJECT_CLASS (e_mail_data_session_parent_class)->finalize (object);
}

static void
e_mail_data_session_class_init (EMailDataSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EMailDataSessionPrivate));

  object_class->get_property = e_mail_data_session_get_property;
  object_class->set_property = e_mail_data_session_set_property;
  object_class->dispose = e_mail_data_session_dispose;
  object_class->finalize = e_mail_data_session_finalize;
}

static void
e_mail_data_session_init (EMailDataSession *self)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(self);

	priv->gdbus_object = egdbus_session_cs_stub_new ();
	g_signal_connect (priv->gdbus_object, "handle-get-store", G_CALLBACK (impl_Mail_getStore), self);
	g_signal_connect (priv->gdbus_object, "handle-get-local-store", G_CALLBACK (impl_Mail_getLocalStore), self);
	g_signal_connect (priv->gdbus_object, "handle-get-local-folder", G_CALLBACK (impl_Mail_getLocalFolder), self);
	g_signal_connect (priv->gdbus_object, "handle-get-folder-from-uri", G_CALLBACK (impl_Mail_getFolderFromUri), self);
	g_signal_connect (priv->gdbus_object, "handle-add-password", G_CALLBACK (impl_Mail_addPassword), self);
	g_signal_connect (priv->gdbus_object, "handle-find-password", G_CALLBACK (impl_Mail_findPassword), self);
	g_signal_connect (priv->gdbus_object, "handle-send-receive", G_CALLBACK (impl_Mail_sendReceive), self);
	g_signal_connect (priv->gdbus_object, "handle-fetch-account", G_CALLBACK (impl_Mail_fetchAccount), self);
	g_signal_connect (priv->gdbus_object, "handle-fetch-old-messages", G_CALLBACK (impl_Mail_fetchOldMessages), self);
	g_signal_connect (priv->gdbus_object, "handle-cancel-operations", G_CALLBACK (impl_Mail_cancelOperations), self);

	priv->stores_lock = g_mutex_new ();
	priv->stores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	priv->datastores_lock = g_mutex_new ();
	priv->datastores = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) NULL);

	priv->connections_lock = g_mutex_new ();
	priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);
	
	
}

EMailDataSession*
e_mail_data_session_new (void)
{
  return g_object_new (EMAIL_TYPE_DATA_SESSION, NULL);
}

guint
e_mail_data_session_register_gdbus_object (EMailDataSession *msession, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	guint ret;

	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

 	ret = g_dbus_interface_register_object (G_DBUS_INTERFACE (priv->gdbus_object),
               	                     	connection,
                                    	object_path,
                                    	error);

	ipc (printf("EMailDataSession: Registering gdbus object %s\n", object_path));

	return ret;
}

void 
e_mail_data_session_release (EMailDataSession *session, GDBusConnection *connection, const char *name)
{

}

const char *
e_mail_data_session_get_path_from_store (EMailDataSession *msession, gpointer store)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	EMailDataStore *mstore;

	mstore = g_hash_table_lookup (priv->datastores, store);
	g_assert (mstore != NULL);
	
	micro(printf("e_mail_data_session_get_path_from_store: %p: %s\n", store, e_mail_data_store_get_path(mstore)));

	return e_mail_data_store_get_path (mstore);
}

CamelFolder *
e_mail_session_get_folder_from_path (EMailDataSession *msession, const char *path)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	GHashTableIter iter;
	gpointer key, value;
	
	g_hash_table_iter_init (&iter, priv->datastores);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		EMailDataStore *estore = (EMailDataStore *)value;
		CamelFolder *folder;

		folder = e_mail_data_store_get_camel_folder (estore, path);
		if (folder) {
			micro(printf("e_mail_session_get_folder_from_path: %s %p\n", path, folder));

			return folder;
		}
	}	

	micro(printf("e_mail_session_get_folder_from_path: %s %p\n", path, NULL));
	g_warning ("Unable to find CamelFolder from the object path\n");

	return NULL;	
}

void
e_mail_session_emit_ask_password (EMailDataSession *msession, const char *title, const gchar *prompt, const gchar *key)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	ipc(printf("Emitting for Ask Password: %s %s %s\n", title, prompt, key));
	egdbus_session_cs_emit_get_password (priv->gdbus_object, title, prompt, key);
}

void
e_mail_session_emit_send_receive_completed (EMailDataSession *msession)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	ipc(printf("Emitting Send/Receive completed signal\n"));
	egdbus_session_cs_emit_send_receive_complete (priv->gdbus_object);
}

void
e_mail_session_emit_account_added (EMailDataSession *msession, const char *uid)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	ipc(printf("Emitting Account added signal\n"));
	egdbus_session_cs_emit_account_added (priv->gdbus_object, uid);
}

void
e_mail_session_emit_account_removed (EMailDataSession *msession, const char *uid)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	ipc(printf("Emitting Account removed signal\n"));
	egdbus_session_cs_emit_account_removed (priv->gdbus_object, uid);
}

void
e_mail_session_emit_account_changed (EMailDataSession *msession, const char *uid)
{
	EMailDataSessionPrivate *priv = DATA_SESSION_PRIVATE(msession);
	
	ipc(printf("Emitting Account changed signal\n"));
	egdbus_session_cs_emit_account_changed (priv->gdbus_object, uid);
}




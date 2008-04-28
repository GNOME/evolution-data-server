/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <camel/camel-sasl.h>
#include <camel/camel-utf8.h>
#include <camel/camel-tcp-stream-raw.h>

#ifdef HAVE_SSL
#include <camel/camel-tcp-stream-ssl.h>
#endif

#include <camel/camel-private.h>
#include <camel/camel-i18n.h>
#include <camel/camel-net-utils.h>
#include "camel-mapi-store.h"
#include "camel-mapi-folder.h"
#include "camel-mapi-store-summary.h"
#include <camel/camel-session.h>
#include <camel/camel-service.h>
#include <camel/camel-store-summary.h>

//#include <mapi/exchange-mapi-folder.h>
//#define d(x) x

/* This definition should be in-sync with those in exchange-mapi-account-setup.c and exchange-account-listener.c */
#define E_PASSWORD_COMPONENT "ExchangeMAPI"

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <libmapi/libmapi.h>
#include <param.h>

#define REACHED printf("%s(%d):%s:Reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define EXITING printf("%s(%d):%s:Exiting \n", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define d(x) printf("%s(%d):%s:%s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, x)

struct _CamelMapiStorePrivate {
	gchar *user;
	gchar *profile;
	gchar *base_url;
	gchar *storage_path;

	GHashTable *id_hash; /*get names from ids*/
	GHashTable *name_hash;/*get ids from names*/
	GHashTable *parent_hash;
};

static CamelStore *gl_store = NULL;

static CamelOfflineStoreClass *parent_class = NULL;

static void	camel_mapi_store_class_init(CamelMapiStoreClass *);
CamelType	camel_mapi_store_get_type(void);
static void	camel_mapi_store_init(CamelMapiStore *, CamelMapiStoreClass *);
static void	camel_mapi_store_finalize(CamelObject *);

/* service methods */
static void	mapi_construct(CamelService *, CamelSession *,
				     CamelProvider *, CamelURL *,
				     CamelException *);
static char	*mapi_get_name(CamelService *, gboolean );
static gboolean	mapi_connect(CamelService *, CamelException *);
static gboolean	mapi_disconnect(CamelService *, gboolean , CamelException *);
static GList	*mapi_query_auth_types(CamelService *, CamelException *);

/* store methods */
static CamelFolder	*mapi_get_folder(CamelStore *, const char *, guint32, CamelException *);
static CamelFolderInfo	*mapi_create_folder(CamelStore *, const char *, const char *, CamelException *);
static void		mapi_delete_folder(CamelStore *, const char *, CamelException *);
static void		mapi_rename_folder(CamelStore *, const char *, const char *, CamelException *);
static CamelFolderInfo	*mapi_get_folder_info(CamelStore *, const char *, guint32, CamelException *);
static void		mapi_subscribe_folder(CamelStore *, const char *, CamelException *);
static void		mapi_unsubscribe_folder(CamelStore *, const char *, CamelException *);
static void		mapi_noop(CamelStore *, CamelException *);
static CamelFolderInfo * mapi_build_folder_info(CamelMapiStore *mapi_store, const char *parent_name, const char *folder_name);
static void mapi_folders_sync (CamelMapiStore *store, CamelException *ex);
static gboolean mapi_is_system_folder (const char *folder_name);

CamelStore
*get_store(void)
{
	return (gl_store);
}

void
set_store(CamelStore *store)
{
	gl_store = store;
}



static guint
mapi_hash_folder_name(gconstpointer key)
{
	return g_str_hash(key);
}

static guint
mapi_compare_folder_name(gconstpointer a, gconstpointer b)
{
	gconstpointer	aname = a; 
	gconstpointer	bname = b;
  
	return g_str_equal(aname, bname);
}

static void
camel_mapi_store_class_init(CamelMapiStoreClass *klass)
{
	CamelServiceClass	*service_class = 
		CAMEL_SERVICE_CLASS (klass);
	CamelStoreClass		*store_class = (CamelStoreClass *) klass;
		CAMEL_STORE_CLASS (klass);

	parent_class = (CamelOfflineStoreClass *) camel_type_get_global_classfuncs(CAMEL_TYPE_OFFLINE_STORE);

	service_class->construct = mapi_construct;
	service_class->get_name = mapi_get_name;
	service_class->connect = mapi_connect;
	service_class->disconnect = mapi_disconnect;
	service_class->query_auth_types = mapi_query_auth_types;

	store_class->hash_folder_name = mapi_hash_folder_name;
	store_class->compare_folder_name = mapi_compare_folder_name;
	/* store_class->get_inbox = mapi_get_inbox; */
	store_class->get_folder = mapi_get_folder;
	store_class->create_folder = mapi_create_folder;
	store_class->delete_folder = mapi_delete_folder;
	store_class->rename_folder = mapi_rename_folder;
	store_class->get_folder_info = mapi_get_folder_info;
	store_class->subscribe_folder = mapi_subscribe_folder;
	store_class->unsubscribe_folder = mapi_unsubscribe_folder;
	store_class->noop = mapi_noop;
}

CamelType 
camel_mapi_store_get_type(void)
{
	REACHED;
	static CamelType type = 0;
  
	if (!type) {
		type = camel_type_register(camel_offline_store_get_type (),
				     "CamelMapiStores",
				     sizeof (CamelMapiStore),
				     sizeof (CamelMapiStoreClass),
				     (CamelObjectClassInitFunc) camel_mapi_store_class_init,
				     NULL,
				     (CamelObjectInitFunc) camel_mapi_store_init,
				     (CamelObjectFinalizeFunc) camel_mapi_store_finalize);
	}

	return type;
}

/*
** store is already initilyse to NULL or 0 value
** klass already have a parent_class
** nothing must be doing here
*/
static void camel_mapi_store_init(CamelMapiStore *store, CamelMapiStoreClass *klass)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate *priv = g_new0 (CamelMapiStorePrivate, 1);

	mapi_store->summary = NULL;

	priv->storage_path = NULL;
	priv->base_url = NULL;

	((CamelStore *)mapi_store)->flags |= CAMEL_STORE_SUBSCRIPTIONS;

	mapi_store->priv = priv;

/* 	store->camel_url = NULL; */
/* 	store->fi = NULL; */
/* 	store->trash_name = NULL; */
/* 	store->folders = NULL; */
/* 	store->folders_lock = NULL; */
/* 	store->connect_lock = NULL; */
}

static void camel_mapi_store_finalize(CamelObject *object)
{
}

/* service methods */
static void mapi_construct(CamelService *service, CamelSession *session,
				 CamelProvider *provider, CamelURL *url,
				 CamelException *ex)
{
	CAMEL_SERVICE (service);
	CamelMapiStore	*mapi_store = CAMEL_MAPI_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	const char *property_value;
	CamelMapiStorePrivate *priv = mapi_store->priv;
	char *path = NULL;
	
	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);

	if (camel_exception_is_set (ex))
		return;
	
/* 	if (!(url->host || url->user)) { */
/* 		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, */
/* 				     _("Host or user not available in url")); */
/* 	} */

	/*storage path*/
	priv->storage_path = camel_session_get_storage_path (session, service, ex);
	if (!priv->storage_path)
		return;
	
	/*store summary*/
	path = g_alloca (strlen (priv->storage_path) + 32);

	mapi_store->summary = camel_mapi_store_summary_new ();
	camel_store_summary_set_filename ((CamelStoreSummary *)mapi_store->summary, path);

	camel_store_summary_touch ((CamelStoreSummary *)mapi_store->summary);
	camel_store_summary_load ((CamelStoreSummary *) mapi_store->summary);

	/*user and profile*/
	priv->user = g_strdup (url->user);
	priv->profile = camel_url_get_param(url, "profile");

	/*base url*/
	priv->base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
						       CAMEL_URL_HIDE_PARAMS   |
						       CAMEL_URL_HIDE_AUTH)  );

	/*Hash Table*/	
	priv->id_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->parent_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	store->flags &= ~CAMEL_STORE_VJUNK;
	//store->flags &= ~CAMEL_STORE_VTRASH;
}

static char
*mapi_get_name(CamelService *service, gboolean brief)
{
	if (brief) {
		return g_strdup_printf(_("Exchange MAPI server %s"), service->url->host);
	} else {
		return g_strdup_printf(_("Exchange MAPI for %s on %s"),
				       service->url->user, service->url->host);
	}
}

static gboolean
check_for_connection (CamelService *service, CamelException *ex)
{
	/*Fixme : What happens when the network connection drops. 
	  will mapi subsystem handle that ?*/
	return exchange_mapi_connection_exists ();
}

static gboolean
mapi_auth_loop (CamelService *service, CamelException *ex)
{
	CamelSession *session = camel_service_get_session (service);
	CamelStore *store = CAMEL_STORE (service);
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate *priv = mapi_store->priv;

	char *errbuf = NULL;
	gboolean authenticated = FALSE;
	char *uri;
/* 	char *profile_name = NULL; */

	service->url->passwd = NULL;

	while (!authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			camel_session_forget_password (session, service, E_PASSWORD_COMPONENT, "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}
	
		if (!service->url->passwd ){
			char *prompt;
			
			prompt = g_strdup_printf (_("%sPlease enter the MAPI "
						    "password for %s@%s"),
						  errbuf ? errbuf : "",
						  service->url->user,
						  service->url->host);
			service->url->passwd =
				camel_session_get_password (session, service, E_PASSWORD_COMPONENT,
							    prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, ex);
			g_free (prompt);
			g_free (errbuf);
			errbuf = NULL;
			
			if (!service->url->passwd) {
				camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
						     _("You did not enter a password."));
				return FALSE;
			}
		}
		
/* 		profile_name = camel_url_get_param (service->url, "profile"); */
/* 		printf("%s(%d):%s:url->profile \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, profile_name);		 */

		exchange_mapi_connection_new (NULL,service->url->passwd);

		if (!exchange_mapi_connection_exists ()) {
			errbuf = g_strdup_printf (_("Unable to authenticate "
					    "to Exchange MAPI server. "));
						  
			camel_exception_clear (ex);
		} else 
			authenticated = TRUE;
		
	}
	return TRUE;
}


static gboolean
mapi_connect(CamelService *service, CamelException *ex)
{
	REACHED;
	CamelMapiStore *store = CAMEL_MAPI_STORE (service);
	CamelMapiStorePrivate *priv = store->priv;
	CamelSession *session = service->session;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL ||
	    (service->status == CAMEL_SERVICE_DISCONNECTED)) {
		return FALSE;
	}

	if (service->status == CAMEL_SERVICE_DISCONNECTED) {
		return FALSE;
	}

	if (!priv) {
		store->priv = g_new0 (CamelMapiStorePrivate, 1);
		priv = store->priv;
		camel_service_construct (service, service->session, service->provider, service->url, ex);
	}

	CAMEL_SERVICE_REC_LOCK (service, connect_lock);
	if (check_for_connection (service, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);
		return TRUE;
	}

	if (!mapi_auth_loop (service, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}
	
	service->status = CAMEL_SERVICE_CONNECTED;
	((CamelOfflineStore *) store)->state = CAMEL_OFFLINE_STORE_NETWORK_AVAIL;

	if (camel_store_summary_count ((CamelStoreSummary *)store->summary) == 0) {
		/*Settting the refresh stamp to the current time*/
		//store->refresh_stamp = time (NULL);
	}

	//camel_store_summary_save ((CamelStoreSummary *) store->summary);

	CAMEL_SERVICE_REC_UNLOCK (service, connect_lock);

	return TRUE;
}

static gboolean 
mapi_disconnect(CamelService *service, gboolean clean, CamelException *ex)
{
	/* Close the mapi subsystem */
	exchange_mapi_connection_close ();
	return TRUE;
}

static GList *mapi_query_auth_types(CamelService *service, CamelException *ex)
{
	return NULL;
}

static gboolean
mapi_is_system_folder (const char *folder_name)
{
	if (!strcmp (folder_name, "Inbox") ||
	    !strcmp (folder_name, "Deleted Items") ||
	    !strcmp (folder_name, "Junk Mail") ||
	    !strcmp (folder_name, "Sent Items"))
		return TRUE;
	else
		return FALSE;
}


static CamelFolder *
mapi_get_folder(CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate *priv = mapi_store->priv;
	char *storage_path = NULL;
	char *folder_dir = NULL;

	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
	//	folder_dir = e_path_to_physical (storage_path, folder_name);
	//	g_free(storage_path);

	//	return camel_mapi_folder_new(store, folder_name, folder_dir, flags, ex);
	return camel_mapi_folder_new(store, folder_name, storage_path, flags, ex);
}

static CamelFolderInfo*
mapi_create_folder(CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;
	CamelFolderInfo *root = NULL;
	char *parent_id;
	mapi_id_t parent_fid, new_folder_id;
	int status;

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create MAPI folders in offline mode."));
		return NULL;
	}
	
	if(parent_name == NULL) {
		parent_name = "";
		if (mapi_is_system_folder (folder_name)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, NULL);
			return NULL;
		}
	}

	if (parent_name && (strlen(parent_name) > 0) )
		parent_id = g_hash_table_lookup (priv->name_hash, parent_name);
	else
		parent_id = "";

	if (!mapi_connect (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
	}

	CAMEL_SERVICE_REC_LOCK (store, connect_lock);


	exchange_mapi_util_mapi_id_from_string (parent_id, &parent_fid);
	new_folder_id = exchange_mapi_create_folder(olFolderInbox, parent_fid, folder_name);
	if (new_folder_id != 0) {
		root = mapi_build_folder_info(mapi_store, parent_name, folder_name);
		camel_store_summary_save((CamelStoreSummary *)mapi_store->summary);

		g_hash_table_insert (priv->id_hash, exchange_mapi_util_mapi_id_to_string (new_folder_id), g_strdup(folder_name));
		g_hash_table_insert (priv->name_hash, g_strdup(root->full_name), exchange_mapi_util_mapi_id_to_string (new_folder_id));
		g_hash_table_insert (priv->parent_hash, exchange_mapi_util_mapi_id_to_string (new_folder_id), g_strdup(parent_id));

		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	}

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
	return root;

}

/* FIXME: testing */
static CamelFolderInfo 
*create_exchange_folder(CamelStore *store, const char *folder_name)
{
	return NULL;
}
/* FIXME */
static CamelFolderInfo 
*create_exchange_subfolder(CamelStore *store, const char *parent_name, const char *folder_name)
{
	return NULL;
}

static void
mapi_forget_folder (CamelMapiStore *mapi_store, const char *folder_name, CamelException *ex)
{
	CamelFolderSummary *summary;
	CamelMapiStorePrivate *priv = mapi_store->priv;
	char *summary_file, *state_file;
	char *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const char *name;

	name = folder_name;

	storage_path = g_strdup_printf ("%s/folders", priv->storage_path);

	/* Fixme Path - e_*-to_path */
	folder_dir = g_strdup(g_strconcat (storage_path, "/", folder_name, NULL));

	if (g_access(folder_dir, F_OK) != 0) {
		g_free(folder_dir);
		return;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	summary = camel_mapi_summary_new(NULL,summary_file);
	if(!summary) {
		g_free(summary_file);
		g_free(folder_dir);
		return;
	}

	camel_object_unref (summary);
	g_unlink (summary_file);
	g_free (summary_file);

	state_file = g_strdup_printf ("%s/cmeta", folder_dir);
	g_unlink (state_file);
	g_free (state_file);

	g_rmdir (folder_dir);
	g_free (folder_dir);

	camel_store_summary_remove_path ( (CamelStoreSummary *)mapi_store->summary, folder_name);
	camel_store_summary_save ( (CamelStoreSummary *)mapi_store->summary);

	fi = mapi_build_folder_info(mapi_store, NULL, folder_name);
	camel_object_trigger_event (CAMEL_OBJECT (mapi_store), "folder_deleted", fi);
	camel_folder_info_free (fi);
}

static void 
mapi_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;

	const char *folder_id; 
	mapi_id_t folder_fid;
	gboolean status = FALSE;
	
	CAMEL_SERVICE_REC_LOCK (store, connect_lock);
	
	if (!camel_mapi_store_connected ((CamelMapiStore *)store, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return;
	}

	folder_id = g_hash_table_lookup (priv->name_hash, folder_name);
	exchange_mapi_util_mapi_id_from_string (folder_id, &folder_fid);
	status = exchange_mapi_remove_folder (0, folder_fid);

	if (status) {
		/* Fixme ??  */
/* 		if (mapi_store->current_folder) */
/* 			camel_object_unref (mapi_store->current_folder); */
		mapi_forget_folder(mapi_store,folder_name,ex);

		g_hash_table_remove (priv->id_hash, folder_id);
		g_hash_table_remove (priv->name_hash, folder_name);
		
		g_hash_table_remove (priv->parent_hash, folder_id);
	} 

	CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);

}

static void 
mapi_rename_folder(CamelStore *store, const char *old_name, const char *new_name, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate  *priv = mapi_store->priv;
	char *oldpath, *newpath, *storepath;
	const char *folder_id;
	char *temp_new = NULL;
	mapi_id_t fid;

	if (mapi_is_system_folder (old_name)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename Mapi folder `%s' to `%s'"),
				      old_name, new_name);
		return;
	}

	CAMEL_SERVICE_REC_LOCK (mapi_store, connect_lock);
	
	if (!camel_mapi_store_connected ((CamelMapiStore *)store, ex)) {
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		return;
	}
	
	folder_id = camel_mapi_store_folder_id_lookup (mapi_store, old_name);
	if (!folder_id) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename MAPI folder `%s'. Folder doesn't exist"),
				      old_name);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		return;
	}

	exchange_mapi_util_mapi_id_from_string (folder_id, &fid);
		
	temp_new = strrchr (new_name, '/');
	if (temp_new) 
		temp_new++;
	else
		temp_new = (char *)new_name;
	
	if (!exchange_mapi_rename_folder (NULL, fid , temp_new))
	{
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename MAPI folder `%s' to `%s'"),
				      old_name, new_name);
		CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
		return;
	}

	g_hash_table_replace (priv->id_hash, g_strdup(folder_id), g_strdup(temp_new));

	g_hash_table_insert (priv->name_hash, g_strdup(new_name), g_strdup(folder_id));
	g_hash_table_remove (priv->name_hash, old_name);

	storepath = g_strdup_printf ("%s/folders", priv->storage_path);
	oldpath = e_path_to_physical (storepath, old_name);
	newpath = e_path_to_physical (storepath, new_name);
	g_free (storepath);

	/*XXX: make sure the summary is also renamed*/
	if (g_rename (oldpath, newpath) == -1) {
		g_warning ("Could not rename message cache '%s' to '%s': %s: cache reset",
				oldpath, newpath, strerror (errno));
	}

	g_free (oldpath);
	g_free (newpath);
	CAMEL_SERVICE_REC_UNLOCK (mapi_store, connect_lock);
}

char *
camel_mapi_store_summary_path_to_full(CamelMapiStoreSummary *s, const char *path, char dir_sep)
{
	unsigned char *full, *f;
	guint32 c, v = 0;
	const char *p;
	int state=0;
	char *subpath, *last = NULL;
	CamelStoreInfo *si;

	/* check to see if we have a subpath of path already defined */
	subpath = alloca(strlen(path)+1);
	strcpy(subpath, path);
	do {
		si = camel_store_summary_path((CamelStoreSummary *)s, subpath);
		if (si == NULL) {
			last = strrchr(subpath, '/');
			if (last)
				*last = 0;
		}
	} while (si == NULL && last);

	/* path is already present, use the raw version we have */
	if (si && strlen(subpath) == strlen(path)) {
		f = g_strdup(camel_mapi_store_info_full_name(s, si));
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		return f;
	}

	f = full = alloca(strlen(path)*2+1);
	if (si)
		p = path + strlen(subpath);
	else
		p = path;

	while ( (c = camel_utf8_getc((const unsigned char **)&p)) ) {
		switch(state) {
			case 0:
				if (c == '%')
					state = 1;
				else {
					if (c == '/')
						c = dir_sep;
					camel_utf8_putc(&f, c);
				}
				break;
			case 1:
				state = 2;
				v = hexnib(c)<<4;
				break;
			case 2:
				state = 0;
				v |= hexnib(c);
				camel_utf8_putc(&f, v);
				break;
		}
	}
	camel_utf8_putc(&f, c);

	/* merge old path part if required */
	f = g_strdup (full);
	if (si) {
		full = g_strdup_printf("%s%s", camel_mapi_store_info_full_name(s, si), f);
		g_free(f);
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		f = full;
	} 

	return f ;
}


//do we realy need this. move to utils then ! 
static int 
match_path(const char *path, const char *name)
{
	char p, n;

	p = *path++;
	n = *name++;
	while (n && p) {
		if (n == p) {
			p = *path++;
			n = *name++;
		} else if (p == '%') {
			if (n != '/') {
				n = *name++;
			} else {
				p = *path++;
			}
		} else if (p == '*') {
			return TRUE;
		} else
			return FALSE;
	}

	return n == 0 && (p == '%' || p == 0);
}

char *
mapi_concat ( const char *prefix, const char *suffix)
{
	size_t len;

	len = strlen (prefix);
	if (len == 0 || prefix[len - 1] == '/')
		return g_strdup_printf ("%s%s", prefix, suffix);
	else
		return g_strdup_printf ("%s%c%s", prefix, '/', suffix);
}

static CamelFolderInfo *
mapi_build_folder_info(CamelMapiStore *mapi_store, const char *parent_name, const char *folder_name)
{
	CamelURL *url;
	const char *name;
	CamelFolderInfo *fi;
	CamelMapiStorePrivate *priv = mapi_store->priv;

	fi = g_malloc0(sizeof(*fi));
	
	fi->unread = -1;
	fi->total = -1;

	if (parent_name) {
		if (strlen(parent_name) > 0) 
			fi->full_name = g_strconcat(parent_name, "/", folder_name, NULL);
		else
			fi->full_name = g_strdup (folder_name);
	} else 
		fi->full_name = g_strdup(folder_name);
 
	url = camel_url_new(priv->base_url,NULL);
	g_free(url->path);
	url->path = g_strdup_printf("/%s", fi->full_name);
	fi->uri = camel_url_to_string(url,CAMEL_URL_HIDE_ALL);
	camel_url_free(url);

	name = strrchr(fi->full_name,'/');
	if(name == NULL)
		name = fi->full_name;
	else
		name++;

	if (!strcmp (folder_name, "Sent Items"))
		fi->flags |= CAMEL_FOLDER_TYPE_SENT;
	else if (!strcmp (folder_name, "Inbox"))
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	else if (!strcmp (folder_name, "Deleted Items"))
		fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	else if (!strcmp (folder_name, "Junk Mail"))
		fi->flags |= CAMEL_FOLDER_TYPE_JUNK;
		
	fi->name = g_strdup(name);
	return fi;
}

static CamelFolderInfo *
mapi_get_folder_info_offline (CamelStore *store, const char *top,
			 guint32 flags, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelFolderInfo *fi;
	GPtrArray *folders;
	char *path, *name;
	int i;

	folders = g_ptr_array_new ();

	if (top == NULL)
		top = "";

	/* get starting point */
	if (top[0] == 0) {
			name = g_strdup("");
	} else {
		name = camel_mapi_store_summary_full_from_path(mapi_store->summary, top);
		if (name == NULL)
			name = camel_mapi_store_summary_path_to_full(mapi_store->summary, top, '/');
	}

	path = mapi_concat (name, "*");

	for (i=0;i<camel_store_summary_count((CamelStoreSummary *)mapi_store->summary);i++) {
		CamelStoreInfo *si = camel_store_summary_index((CamelStoreSummary *)mapi_store->summary, i);

		if (si == NULL) 
			continue;

		if ( !strcmp(name, camel_mapi_store_info_full_name (mapi_store->summary, si))
		     || match_path (path, camel_mapi_store_info_full_name (mapi_store->summary, si))) {

			fi = mapi_build_folder_info(mapi_store, NULL, camel_store_info_path((CamelStoreSummary *)mapi_store->summary, si));

			fi->unread = si->unread;
			fi->total = si->total;

			g_ptr_array_add (folders, fi);
		}
		camel_store_summary_info_free((CamelStoreSummary *)mapi_store->summary, si);
	}

	g_free(name);
	g_free (path);
	fi = camel_folder_info_build (folders, top, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	return fi;
}

static CamelFolderInfo *
convert_to_folder_info (CamelMapiStore *store, ExchangeMAPIFolder *folder, const char *url, CamelException *ex)
{
	const char *name = NULL;
	gchar *parent, *id = NULL;
	mapi_id_t mapi_id_folder;

	char *par_name = NULL;
	char *folder_name = NULL;
	CamelFolderInfo *fi;
	CamelMapiStoreInfo *si = NULL;
	CamelMapiStorePrivate *priv = store->priv;
	ExchangeMAPIFolderType type;

	name = exchange_mapi_folder_get_name (folder);

	id = exchange_mapi_util_mapi_id_to_string (exchange_mapi_folder_get_fid (folder));
		
	fi = g_new0 (CamelFolderInfo, 1);

	if (!strcmp (name, "Sent Items"))
		fi->flags |= CAMEL_FOLDER_TYPE_SENT;
	else if (!strcmp (name, "Inbox"))
		fi->flags |= CAMEL_FOLDER_TYPE_INBOX;
	else if (!strcmp (name, "Deleted Items"))
		fi->flags |= CAMEL_FOLDER_TYPE_TRASH;
	else if (!strcmp (name, "Junk Mail"))
		fi->flags |= CAMEL_FOLDER_TYPE_JUNK;

	/*
	   parent_hash contains the "parent id <-> folder id" combination. So we form
	   the path for the full name in camelfolder info by looking up the hash table until
	   NULL is found
	 */

	mapi_id_folder = exchange_mapi_folder_get_parent_id (folder);
	parent = exchange_mapi_util_mapi_id_to_string (mapi_id_folder);
	par_name = g_hash_table_lookup (priv->id_hash, parent);

	if (par_name != NULL) {
		gchar *temp_parent = NULL;
		gchar *temp = NULL;
		gchar *str = g_strconcat (par_name, "/", name, NULL);

		fi->name = g_strdup (name);

		temp_parent = g_hash_table_lookup (priv->parent_hash, parent);
		while (temp_parent) {
			temp = g_hash_table_lookup (priv->id_hash, temp_parent );
			if (temp == NULL) {
				break;
			}	
			str = g_strconcat ( temp, "/", str, NULL);

			temp_parent = g_hash_table_lookup (priv->parent_hash, temp_parent);

		} 
		fi->full_name = g_strdup (str);
		fi->uri = g_strconcat (url, str, NULL);
		g_free (str);
	}
	else {
		fi->name =  g_strdup (name);
		fi->full_name = g_strdup (name);
		fi->uri = g_strconcat (url, "", name, NULL);
	}

	si = camel_mapi_store_summary_add_from_full (store->summary, fi->full_name, '/');
	if (si == NULL) {
		camel_folder_info_free (fi);
		return NULL;
	}

	/*name_hash returns the container id given the name */
	g_hash_table_insert (priv->name_hash, g_strdup(fi->full_name), id);

	fi->total = folder->total;
	fi->unread = folder->unread_count;

	si->info.total = fi->total;
	si->info.unread = fi->unread;
	si->info.flags = 0;

	return fi;
}

gboolean
camel_mapi_store_connected (CamelMapiStore *store, CamelException *ex)
{
/* 	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL */
/* 	    && camel_service_connect ((CamelService *)store, ex)) { */
	if (camel_service_connect ((CamelService *)store, ex))
		return TRUE;

	return FALSE;
}

static void
mapi_folders_sync (CamelMapiStore *store, CamelException *ex)
{
	REACHED;
	CamelMapiStorePrivate  *priv = store->priv;
	gboolean status;
	GSList *folder_list = NULL, *temp_list = NULL, *list = NULL;
	char *url, *temp_url;
	CamelFolderInfo *info = NULL, *hfi = NULL;
	GHashTable *present;
	CamelStoreInfo *si = NULL;
	int count, i;

/* 	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL) { */
		if (((CamelService *)store)->status == CAMEL_SERVICE_DISCONNECTED){
			((CamelService *)store)->status = CAMEL_SERVICE_CONNECTING;
			mapi_connect ((CamelService *)store, ex);
		}
/* 	} */

	if (!camel_mapi_store_connected (store, ex)) {
/* 		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE, */
/* 				_("Folder list not available in offline mode.")); */
		return;
	}

	status = exchange_mapi_get_folders_list (&folder_list);
	if (!status) {
		g_warning ("Could not get folder list..\n");
		return;
	}

	temp_list = folder_list;
	list = folder_list;

	url = camel_url_to_string (CAMEL_SERVICE(store)->url,
				   (CAMEL_URL_HIDE_PASSWORD|
				    CAMEL_URL_HIDE_PARAMS|
				    CAMEL_URL_HIDE_AUTH) );

	if ( url[strlen(url) - 1] != '/') {
		temp_url = g_strconcat (url, "/", NULL);
		g_free ((char *)url);
		url = temp_url;
	}
	
	/*populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL ; temp_list = g_slist_next (temp_list) ) {
		const char *name;
		gchar *fid = NULL, *parent_id = NULL;

		name = exchange_mapi_folder_get_name ((ExchangeMAPIFolder *)(temp_list->data));
		fid = exchange_mapi_util_mapi_id_to_string (exchange_mapi_folder_get_fid((ExchangeMAPIFolder *)(temp_list->data)));
		parent_id = exchange_mapi_util_mapi_id_to_string (exchange_mapi_folder_get_parent_id ((ExchangeMAPIFolder *)(temp_list->data)));

		if (exchange_mapi_folder_is_root ((ExchangeMAPIFolder *)(temp_list->data)))
			continue;

		/*id_hash returns the name for a given container id*/
		g_hash_table_insert (priv->id_hash, g_strdup (fid), g_strdup(name)); 

		/*parent_hash returns the parent container id, given an id*/
		g_hash_table_insert (priv->parent_hash, g_strdup(fid), g_strdup(parent_id));
	}

	present = g_hash_table_new (g_str_hash, g_str_equal);

	for (;folder_list != NULL; folder_list = g_list_next (folder_list)) {
		ExchangeMAPIFolder *folder = (ExchangeMAPIFolder *) folder_list->data;
		
		if (exchange_mapi_folder_is_root ((ExchangeMAPIFolder *)(folder)))
			continue;

		if ( folder->container_class != MAPI_FOLDER_TYPE_MAIL) 
			continue;

		info = convert_to_folder_info (store, folder, (const char *)url, ex);
		if (info) {
			hfi = g_hash_table_lookup (present, info->full_name);
			if (hfi == NULL) {
				g_hash_table_insert (present, info->full_name, info);
			} else {
				camel_folder_info_free (info);
				info = NULL;
			}
		}
	}
	
	g_free ((char *)url);

	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index ((CamelStoreSummary *)store->summary, i);
		if (si == NULL)
			continue;

		info = g_hash_table_lookup (present, camel_store_info_path (store->summary, si));
		if (info != NULL) {
			camel_store_summary_touch ((CamelStoreSummary *)store->summary);
		}
/* 		FIXME: BAD BAD !! Y? shud v include == 3  */
		/* else { */
/* 			printf("%s(%d):%s:removing : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, si->path); */
/* 			camel_store_summary_remove ((CamelStoreSummary *)store->summary, si); */
/* 			count--; */
/* 			i--; */
/* 		} */
		camel_store_summary_info_free ((CamelStoreSummary *)store->summary, si);
	}

	//	g_hash_table_foreach (present, get_folders_free, NULL);
	//	g_hash_table_destroy (present);

}

static CamelFolderInfo*
mapi_get_folder_info(CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelMapiStore *mapi_store = CAMEL_MAPI_STORE (store);
	CamelMapiStorePrivate *priv = mapi_store->priv;
	CamelFolderInfo *info = NULL;
	char *top_folder = NULL;
	int s_count = 0;	
	if (top) {
		top_folder = g_hash_table_lookup (priv->name_hash, top);
		/* 'top' is a valid path, but doesnt have a container id
		 *  return NULL */
		/*if (!top_folder) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					_("You must be working online to complete this operation"));
			return NULL;
		}*/
	}

	if (top && mapi_is_system_folder (top))
		return mapi_build_folder_info (mapi_store, NULL, top );

	/*
	 * Thanks to Michael, for his cached folders implementation in IMAP
	 * is used as is here.
	 */
	if (camel_store_summary_count ((CamelStoreSummary *)mapi_store->summary) == 0) {
/* 		if (mapi_store->list_loaded == 3) { */
		
			mapi_folders_sync (mapi_store, ex);
/* 			mapi_store->list_loaded -= 1; */
/* 		} */
		if (camel_exception_is_set (ex)) {
			camel_store_summary_save ((CamelStoreSummary *) mapi_store->summary);
			return NULL;
		}
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		camel_store_summary_save ((CamelStoreSummary *)mapi_store->summary);
		goto end_r;
	}

	if ((camel_store_summary_count((CamelStoreSummary *)mapi_store->summary) > 0))
		/*Load from cache*/
		goto end_r;


	if (check_for_connection((CamelService *)store, ex)) {
		if ((camel_store_summary_count((CamelStoreSummary *)mapi_store->summary) > 0) ) {
			/*Load from cache*/
			goto end_r;
		}

		mapi_folders_sync (mapi_store, ex);
		if (camel_exception_is_set (ex)) {
			CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
			return NULL;
		}
		camel_store_summary_touch ((CamelStoreSummary *)mapi_store->summary);
		camel_store_summary_save ((CamelStoreSummary *)mapi_store->summary);
	}
	/*camel_exception_clear (ex);*/
end_r:
	s_count = camel_store_summary_count((CamelStoreSummary *)mapi_store->summary);
	info = mapi_get_folder_info_offline (store, top, flags, ex);
	return info;
}

const char *
camel_mapi_store_folder_id_lookup (CamelMapiStore *mapi_store, const char *folder_name)
{
	CamelMapiStorePrivate *priv = mapi_store->priv;

	return g_hash_table_lookup (priv->name_hash, folder_name);
}

const char *
camel_mapi_store_folder_lookup (CamelMapiStore *mapi_store, const char *folder_id)
{
	CamelMapiStorePrivate *priv = mapi_store->priv;

	return g_hash_table_lookup (priv->id_hash, folder_id);
}




static void
mapi_subscribe_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void 
mapi_unsubscribe_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void
mapi_noop(CamelStore *store, CamelException *ex)
{

}




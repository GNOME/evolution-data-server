/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-groupwise-store.c : class for an groupwise store */

/*
 *  Authors:
 *  Sivaiah Nallagatla <snallagatla@novell.com>
 *  parthasarathi susarla <sparthasarathi@novell.com>
 *
 *  Copyright 2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>


#include "camel-groupwise-store.h"
#include "camel-groupwise-summary.h"
#include "camel-groupwise-store-summary.h"
#include "camel-groupwise-folder.h"
#include "camel-groupwise-utils.h"

#include "camel-session.h"
#include "camel-debug.h"
#include "camel-i18n.h" 
#include "camel-disco-diary.h"
#include "camel-types.h"
#include "camel-folder.h" 
#include "camel-private.h"

#define d(x) printf(x);

struct _CamelGroupwiseStorePrivate {
	char *server_name;
	char *port;
	char *user;
	char *use_ssl;

	char *base_url ;
	char *storage_path ;

	GHashTable *id_hash ; //get names from ids
	GHashTable *name_hash ;//get ids from names
	GHashTable *parent_hash ;
	EGwConnection *cnc;
};

static CamelDiscoStoreClass *parent_class = NULL;

extern CamelServiceAuthType camel_groupwise_password_authtype; /*for the query_auth_types function*/

/*prototypes*/
static void groupwise_rename_folder (CamelStore *store, 
					   const char *old_name, 
					   const char *new_name, 
					   CamelException *ex);

static CamelFolderInfo *groupwise_create_folder(CamelStore *store,
				 	       const char *parent_name,
					       const char *folder_name,
					       CamelException *ex) ;

static void groupwise_store_construct (CamelService *service, CamelSession *session,
					     CamelProvider *provider, CamelURL *url,
					     CamelException *ex) ;

static gboolean groupwise_connect_online (CamelService *service, CamelException *ex) ;
static gboolean groupwise_connect_offline (CamelService *service, CamelException *ex) ;
static gboolean
groupwise_disconnect_online (CamelService *service, gboolean clean, CamelException *ex);

static gboolean
groupwise_disconnect_offline (CamelService *service, gboolean clean, CamelException *ex);

static  GList *groupwise_store_query_auth_types (CamelService *service, CamelException *ex) ;

/*static CamelFolder * groupwise_get_folder_online( CamelStore *store,
					  const char *folder_name,
					  guint32 flags,
					  CamelException *ex) ;*/

static CamelFolderInfo *groupwise_build_folder_info(CamelGroupwiseStore *gw_store, const char *parent_name, const char *folder_name) ;


static CamelFolderInfo *groupwise_get_folder_info_online (CamelStore *store, const char *top, guint32 flags, CamelException *ex) ;

static CamelFolderInfo *groupwise_get_folder_info_offline (CamelStore *store, const char *top, guint32 flags, CamelException *ex) ;


static void groupwise_delete_folder(CamelStore *store,
				   const char *folder_name,
				   CamelException *ex) ;



char * groupwise_get_name(CamelService *service, gboolean brief) ;
static guint groupwise_hash_folder_name (gconstpointer key);
static gint groupwise_compare_folder_name (gconstpointer namea, gconstpointer nameb);

static CamelFolder *groupwise_get_folder_online (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex) ;
static CamelFolder *groupwise_get_folder_offline (CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex) ;

static void groupwise_forget_folder (CamelGroupwiseStore *gw_store, const char *folder_name, CamelException *ex) ;

static gboolean  groupwise_can_work_offline (CamelDiscoStore *disco_store);

static void free_hash (gpointer key, gpointer value, gpointer data) ;
	
static void update_folder_counts (CamelGroupwiseStore *gw_store, CamelFolderInfo *fi, CamelException *ex) ;

/*End of prototypes*/




static void
camel_groupwise_store_class_init (CamelGroupwiseStoreClass *camel_groupwise_store_class)
{
	/*	CamelObjectClass *camel_object_class =
		CAMEL_OBJECT_CLASS (camel_groupwise_store_class);*/
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_groupwise_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_groupwise_store_class);
	CamelDiscoStoreClass *camel_disco_store_class =
		CAMEL_DISCO_STORE_CLASS (camel_groupwise_store_class) ;
	
	parent_class = CAMEL_DISCO_STORE_CLASS (camel_type_get_global_classfuncs (camel_disco_store_get_type ()));
	
	camel_service_class->construct = groupwise_store_construct;
	camel_service_class->query_auth_types = groupwise_store_query_auth_types;
	camel_service_class->get_name = groupwise_get_name ;
	
	camel_store_class->hash_folder_name = groupwise_hash_folder_name;
	camel_store_class->compare_folder_name = groupwise_compare_folder_name;
	
	camel_store_class->create_folder = groupwise_create_folder ;
	camel_store_class->delete_folder = groupwise_delete_folder ;
	camel_store_class->rename_folder = groupwise_rename_folder ;

	camel_disco_store_class->can_work_offline = groupwise_can_work_offline ;
	camel_disco_store_class->connect_online  = groupwise_connect_online;
	camel_disco_store_class->connect_offline = groupwise_connect_offline;
	camel_disco_store_class->disconnect_offline = groupwise_disconnect_offline;
	camel_disco_store_class->disconnect_online = groupwise_disconnect_online;
	camel_disco_store_class->get_folder_online = groupwise_get_folder_online ;
	camel_disco_store_class->get_folder_offline = groupwise_get_folder_offline ;
	camel_disco_store_class->get_folder_resyncing = groupwise_get_folder_online ;
	camel_disco_store_class->get_folder_info_online = groupwise_get_folder_info_online; 
	camel_disco_store_class->get_folder_info_offline = groupwise_get_folder_info_offline ;
	camel_disco_store_class->get_folder_info_resyncing = groupwise_get_folder_info_online ;

}


/*This frees the private structure*/
static void
camel_groupwise_store_finalize (CamelObject *object)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object) ;
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv ;

	if (groupwise_store->summary) {
		camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary) ;
		camel_object_unref (groupwise_store->summary) ;
	}
	
	if (priv) {
		if (priv->user) {
			g_free (priv->user);
			priv->user = NULL;
		}
		if (priv->server_name) {
			g_free (priv->server_name);
			priv->server_name = NULL;
		}
		if (priv->port) {
			g_free (priv->port);
			priv->port = NULL;
		}
		if (priv->use_ssl) {
			g_free (priv->use_ssl);
			priv->use_ssl = NULL;
		}
		if (priv->base_url) {
			g_free (priv->base_url) ;
			priv->base_url = NULL ;
		}
		
		if (E_IS_GW_CONNECTION (priv->cnc)) {
			g_object_unref (priv->cnc);
			priv->cnc = NULL;
		}
		g_free (groupwise_store->priv);
		groupwise_store->priv = NULL;

		if (priv->storage_path)
			g_free(priv->storage_path) ;

		if(groupwise_store->root_container)
			g_free (groupwise_store->root_container) ;
		
		if (priv->id_hash) {
			g_hash_table_foreach (priv->id_hash, free_hash, NULL) ;
			g_hash_table_destroy (priv->id_hash);
		}
		if (priv->name_hash) {
			g_hash_table_foreach (priv->name_hash, free_hash, NULL) ;
			g_hash_table_destroy (priv->name_hash) ;
		}
		if (priv->parent_hash) {
			g_hash_table_foreach (priv->parent_hash, free_hash, NULL) ;
			g_hash_table_destroy (priv->parent_hash) ;
		}
	}

}

static void
camel_groupwise_store_init (gpointer object, gpointer klass)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (object);
	CamelGroupwiseStorePrivate *priv = g_new0 (CamelGroupwiseStorePrivate, 1);
	
	d("in groupwise store init\n");
	priv->server_name = NULL;
	priv->port = NULL;
	priv->use_ssl = NULL;
	priv->user = NULL;
	priv->cnc = NULL;
	groupwise_store->priv = priv;
	
}


CamelType
camel_groupwise_store_get_type (void)
{
	static CamelType camel_groupwise_store_type = CAMEL_INVALID_TYPE;
	
	if (camel_groupwise_store_type == CAMEL_INVALID_TYPE)	{
		camel_groupwise_store_type =
			camel_type_register (CAMEL_DISCO_STORE_TYPE,
					     "CamelGroupwiseStore",
					     sizeof (CamelGroupwiseStore),
					     sizeof (CamelGroupwiseStoreClass),
					     (CamelObjectClassInitFunc) camel_groupwise_store_class_init,
					     NULL,
					     (CamelObjectInitFunc) camel_groupwise_store_init,
					     (CamelObjectFinalizeFunc) camel_groupwise_store_finalize);
	}
	
	return camel_groupwise_store_type;
}


static void
groupwise_store_construct (CamelService *service, CamelSession *session,
				 CamelProvider *provider, CamelURL *url,
				 CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelStore *store = CAMEL_STORE (service);
	const char *property_value, *base_url ;
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv ;
	
	d("in groupwise store constrcut\n");

	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);
	if (camel_exception_is_set (ex))
		return;
	
	if (!(url->host || url->user)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID,
				     _("Host or user not availbale in url"));
	}

	/*store summary*/

	/*storage path*/
	priv->storage_path = camel_session_get_storage_path (session, service, ex) ;
	if (!priv->storage_path)
		return ;
	
	/*host and user*/
	priv->server_name = g_strdup (url->host);
	priv->user = g_strdup (url->user);

	/*base url*/
	base_url = camel_url_to_string (service->url, (CAMEL_URL_HIDE_PASSWORD |
						       CAMEL_URL_HIDE_PARAMS   |
						       CAMEL_URL_HIDE_AUTH)  ) ;
	priv->base_url = g_strdup (base_url) ;								       

	/*soap port*/
	property_value =  camel_url_get_param (url, "soap_port");
	if (property_value == NULL)
		priv->port = g_strdup ("7181");
	else if(strlen(property_value) == 0)
		priv->port = g_strdup ("7181");
	else
		priv->port = g_strdup (property_value);

	/*Hash Table*/	
	priv->id_hash = g_hash_table_new (g_str_hash, g_str_equal) ;
	priv->name_hash = g_hash_table_new (g_str_hash, g_str_equal) ;
	priv->parent_hash = g_hash_table_new (g_str_hash, g_str_equal) ;

	/*ssl*/
	priv->use_ssl = g_strdup (camel_url_get_param (url, "soap_ssl"));
	
	store->flags = 0; //XXX: Shouldnt do this....
	
}

static guint
groupwise_hash_folder_name (gconstpointer key)
{
	
	return g_str_hash (key);
}

static gint
groupwise_compare_folder_name (gconstpointer a, gconstpointer b)
{
	gconstpointer aname = a, bname = b;

	return g_str_equal (aname, bname);
}


static gboolean
groupwise_can_work_offline (CamelDiscoStore *disco_store) 
{
	return TRUE;
	
}


static gboolean
groupwise_auth_loop (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelSession *session = camel_service_get_session (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	char *errbuf = NULL;
	gboolean authenticated = FALSE;
	const char *auth_domain;
	char *uri;

	CAMEL_SERVICE_ASSERT_LOCKED (groupwise_store, connect_lock);
	auth_domain = camel_url_get_param (service->url, "auth-domain");
	if (priv->use_ssl) 
		uri = g_strconcat ("https://", priv->server_name, ":", priv->port, "/soap", NULL);
	else 
		uri = g_strconcat ("http://", priv->server_name, ":", priv->port, "/soap", NULL);
	service->url->passwd = NULL;
	while (!authenticated) {
		if (errbuf) {
			/* We need to un-cache the password before prompting again */
			camel_session_forget_password (session, service, auth_domain, "password", ex);
			g_free (service->url->passwd);
			service->url->passwd = NULL;
		}
		
		if (!service->url->passwd) {
			char *prompt;
			
			prompt = g_strdup_printf (_("%sPlease enter the Groupwise "
						    "password for %s@%s"),
						  errbuf ? errbuf : "",
						  service->url->user,
						  service->url->host);
			service->url->passwd =
				camel_session_get_password (session, service, auth_domain,
							    prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, ex);
			g_free (prompt);
			g_free (errbuf);
			errbuf = NULL;
			
			if (!service->url->passwd) {
				camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
						     _("You didn't enter a password."));
				return FALSE;
			}
		}
		
		
				
		priv->cnc = e_gw_connection_new (uri, priv->user, service->url->passwd);
		if (!priv->cnc) {
			errbuf = g_strdup_printf (_("Unable to authenticate "
					    "to GroupWise server."));
						  
			camel_exception_clear (ex);
		} else 
			authenticated = TRUE;
		
	}
	
	return TRUE;
}

static gboolean
groupwise_connect_online (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (service);
	char *path;
	d("in groupwise store connect\n");
	CAMEL_SERVICE_LOCK (service, connect_lock);
	if (priv->cnc) {
		CAMEL_SERVICE_UNLOCK (service, connect_lock);
		return TRUE;
	}
	
	if (!groupwise_auth_loop (service, ex)) {
		CAMEL_SERVICE_UNLOCK (service, connect_lock);
		camel_service_disconnect (service, TRUE, NULL);
		return FALSE;
	}
	

	path = g_strdup_printf ("%s/journal", priv->storage_path);
	disco_store->diary = camel_disco_diary_new (disco_store, path, ex);
	g_free (path);
	CAMEL_SERVICE_UNLOCK (service, connect_lock);
	if (E_IS_GW_CONNECTION (priv->cnc)) {
		return TRUE;
	}

	return FALSE;

}

static gboolean
groupwise_connect_offline (CamelService *service, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	CamelGroupwiseStorePrivate *priv = groupwise_store->priv;
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (service);
	char *path;

	path = g_strdup_printf ("%s/journal", priv->storage_path);
	disco_store->diary = camel_disco_diary_new (disco_store, path, ex);
	g_free (path);
	if (!disco_store->diary)
		return FALSE;
	
	//imap_store_refresh_folders (store, ex);
	
	//store->connected = !camel_exception_is_set (ex);
	return !camel_exception_is_set (ex);//store->connected;
	//	return TRUE;
}


static gboolean
groupwise_disconnect_online (CamelService *service, gboolean clean, CamelException *ex)
{
	
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (service);
	
	if (groupwise_store->priv->cnc) {
		g_object_unref (groupwise_store->priv->cnc);
		groupwise_store->priv->cnc = NULL;
	}
		
	groupwise_disconnect_offline (service, clean, ex);
	
	return TRUE;
}

static gboolean
groupwise_disconnect_offline (CamelService *service, gboolean clean, CamelException *ex)
{
	
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (service);
		
	if (disco->diary) {
		camel_object_unref (disco->diary);
		disco->diary = NULL;
	}
	
	return TRUE;
}

static  GList*
groupwise_store_query_auth_types (CamelService *service, CamelException *ex)
{
	GList *auth_types = NULL;
	
	d("in query auth types\n");
	auth_types = g_list_prepend (auth_types,  &camel_groupwise_password_authtype);
	return auth_types;
}


/*****************/
static CamelFolder * 
groupwise_get_folder_online( CamelStore *store,
					  const char *folder_name,
					  guint32 flags,
					  CamelException *ex)
{
	CamelGroupwiseStore *gw_store = CAMEL_GROUPWISE_STORE (store) ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	CamelFolder *folder ;
	char *storage_path, *folder_dir, *temp_str,*container_id ;
	const char *temp_name;
	EGwConnectionStatus status ;
	GList *list = NULL ;

	g_print ("||GW:Get folder online\n") ;
	temp_name = folder_name ;
	temp_str = strrchr(folder_name,'/') ;
	if(temp_str == NULL) {
		container_id = 	g_strdup (g_hash_table_lookup (priv->name_hash, g_strdup(folder_name))) ;
	}
	else {
		temp_str++ ;
		container_id = 	g_strdup (g_hash_table_lookup (priv->name_hash, g_strdup(temp_str))) ;
	}

	camel_operation_start (NULL, _("Fetching summary information for new messages"));

	status = e_gw_connection_get_items (priv->cnc, container_id, "attachments", NULL, &list) ;
	if (status != E_GW_CONNECTION_STATUS_OK) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_INVALID, _("Authentication failed"));
		g_free (container_id) ;
		camel_operation_end (NULL);
		return NULL;
	}
	storage_path = g_strdup_printf("%s/folders", priv->storage_path);
        folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path) ;
	
	
	folder = camel_gw_folder_new(store,folder_name, folder_dir,ex) ;
	if(folder) {
		CamelException local_ex ;
		int count ;

		gw_store->current_folder = folder ;
		camel_object_ref (folder) ;
		camel_exception_init (&local_ex) ;
		
		/*gw_folder_selected() ;*/
		
		gw_update_summary (folder, list,  ex) ;

		count = camel_folder_summary_count (folder->summary) ;
		/*gw_rescan() ;*/
		camel_folder_summary_save(folder->summary) ;
	}
	//g_free_list (list) ;
	camel_operation_end (NULL);
	return folder ;
}

static CamelFolder *
groupwise_get_folder_offline (CamelStore *store, const char *folder_name,
		    guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelFolder *new_folder;
	char *folder_dir, *storage_path;
	
	
	storage_path = g_strdup_printf("%s/folders", groupwise_store->priv->storage_path);
	folder_dir = e_path_to_physical (storage_path, folder_name);
	g_free(storage_path);
	if (!folder_dir || access (folder_dir, F_OK) != 0) {
		g_free (folder_dir);
		camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
				      _("No such folder %s"), folder_name);
		return NULL;
	}
	
	new_folder = camel_gw_folder_new (store, folder_name, folder_dir, ex);
	g_free (folder_dir);
	
	return new_folder;
	
}


/*Build/populate CamelFolderInfo structure
  based on the imap_build_folder_info function*/
static CamelFolderInfo *
groupwise_build_folder_info(CamelGroupwiseStore *gw_store, const char *parent_name, const char *folder_name)
{
	CamelURL *url ;
	const char *name, *full_name ;
	CamelFolderInfo *fi ;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;

	fi = g_malloc0(sizeof(*fi)) ;
	
	fi->unread = 0 ;
	fi->total = 0 ;

	
	
	if (parent_name)
		if (strlen(parent_name) > 0) {
			full_name = gw_get_path (gw_store, parent_name) ;
			fi->full_name = g_strconcat(full_name,"/",g_strdup(folder_name), NULL) ;
		} else
			fi->full_name = g_strdup (folder_name) ;
	else {
		full_name = gw_get_path (gw_store, folder_name) ;
		if (full_name)
			fi->full_name = g_strdup (full_name) ;
		else
			fi->full_name = g_strdup(folder_name) ;
	}

	url = camel_url_new(priv->base_url,NULL) ;
	g_free(url->path) ;
	url->path = g_strdup_printf("/%s", fi->full_name) ;
	fi->uri = camel_url_to_string(url,CAMEL_URL_HIDE_ALL) ;
	camel_url_free(url) ;

	name = strrchr(fi->full_name,'/') ;
	if(name == NULL)
		name = fi->full_name ;
	else
		name++ ;
	
	fi->name = g_strdup(name) ;

	return fi ;
}


CamelFolderInfo *
groupwise_get_folder_info_online (CamelStore *store,
				       const char *top,
				       guint32 flags,
				       CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	int status;
	GPtrArray *folders;
	GList *folder_list = NULL, *temp_list = NULL ;
	const char *url, *top_folder;
	char *temp_str = NULL;
	CamelFolderInfo *info = NULL ;

	g_print ("||GW:Get folder info online\n") ;
	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!groupwise_connect_online (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
		}
	}
	if (top == NULL)
		top_folder = "folders" ;
	else {
		temp_str = strrchr (top, '/') ;
		if (temp_str) {
			temp_str++ ;
			top_folder = g_hash_table_lookup (priv->name_hash, temp_str) ;	
		} else
			top_folder = g_hash_table_lookup (priv->name_hash, top) ;	

	}

	
	status = e_gw_connection_get_container_list (priv->cnc, top_folder, &folder_list);
	if (status != E_GW_CONNECTION_STATUS_OK ) {
		/*FIX ME set the camel exception id*/
		return NULL;
	}
	status = e_gw_connection_get_container_list (priv->cnc, top_folder, &temp_list);
	if (status != E_GW_CONNECTION_STATUS_OK ) {
		/*FIX ME set the camel exception id*/
		return NULL;
	}
	//	*temp_list = &folder_list ;
	
	folders = g_ptr_array_new();
	
	url = camel_url_to_string (CAMEL_SERVICE(groupwise_store)->url,
				   (CAMEL_URL_HIDE_PASSWORD|
				    CAMEL_URL_HIDE_PARAMS|
				    CAMEL_URL_HIDE_AUTH) );

	/*Populate the hash table for finding the mapping from container id <-> folder name*/
	for (;temp_list != NULL ; temp_list = g_list_next (temp_list) ) {
		const char *name, *id, *parent ;
		name = e_gw_container_get_name (E_GW_CONTAINER (temp_list->data));
		id = e_gw_container_get_id(E_GW_CONTAINER(temp_list->data)) ;
		parent = e_gw_container_get_parent_id (E_GW_CONTAINER(temp_list->data)) ;

		if (e_gw_container_is_root (E_GW_CONTAINER(temp_list->data))) {
			groupwise_store->root_container = g_strdup (id) ;
			continue ;
		}

		/*id_hash returns the name for a given container id*/
		g_hash_table_insert (priv->id_hash, g_strdup(id), g_strdup(name)) ; 
		/*name_hash returns the container id given the name */
		g_hash_table_insert (priv->name_hash, g_strdup(name), g_strdup(id)) ;
		/*parent_hash returns the parent container id, given an id*/
		g_hash_table_insert (priv->parent_hash, g_strdup(id), g_strdup(parent)) ;

	}

	

	for (; folder_list != NULL; folder_list = g_list_next(folder_list)) {
		CamelFolderInfo *fi;
		const char *parent ;
		gchar *par_name = NULL;
		
		if (e_gw_container_is_root (E_GW_CONTAINER(folder_list->data))) 
			continue ;

		fi = g_new0 (CamelFolderInfo, 1);

		/*
		  parent_hash contains the "parent id <-> container id" combination. So we form
		  the path for the full name in camelfolder info by looking up the hash table until
		  NULL is found
		*/

		parent = e_gw_container_get_parent_id (E_GW_CONTAINER (folder_list->data)) ;
		par_name = g_hash_table_lookup (priv->id_hash, parent) ;

		if (par_name != NULL) {
			gchar *temp_parent = NULL, *temp = NULL ;
			gchar *str = g_strconcat (par_name,"/",e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)), NULL) ;

			fi->name = g_strdup (e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)) ) ;

			temp_parent = g_hash_table_lookup (priv->parent_hash, parent) ;
			while (temp_parent) {
				temp = g_hash_table_lookup (priv->id_hash, temp_parent ) ;
				if (temp == NULL) {
					break ;
				}	
				str = g_strconcat ( temp, "/", str, NULL) ;

				temp_parent = g_hash_table_lookup (priv->parent_hash, temp_parent) ;
				
			} 
			fi->full_name = g_strdup (str)  ;
			fi->uri = g_strconcat (url,str,NULL) ;
			g_free (str) ;
		}
		else {
			fi->name =  fi->full_name = g_strdup (e_gw_container_get_name (E_GW_CONTAINER (folder_list->data)));
			fi->uri = g_strconcat (url, "", e_gw_container_get_name(E_GW_CONTAINER(folder_list->data)), NULL) ;

		}
		
		g_ptr_array_add (folders, fi);
		
/*		if (par_name)
			g_print ("parent: %s\n", par_name)  ;
		g_print ("name: %s\n", fi->name)  ;
		g_print ("full name: %s\n", fi->full_name)  ;
		g_print ("full name: %s\n", fi->uri)  ;*/
	
		//g_free (parent) ;
		//g_free (par_name) ;
		//fi = parent = par_name = NULL ;
		//fi = NULL ;
		
	}
	if ( (top != NULL) && (folders->len == 0)) {
		/*temp_str already contains the value if any*/
		if (temp_str)
			return groupwise_build_folder_info (groupwise_store, NULL, temp_str ) ;
		else
			return groupwise_build_folder_info (groupwise_store, NULL, top ) ;
	}
	info = camel_folder_info_build (folders, NULL, '/', FALSE) ;
	g_ptr_array_free (folders, TRUE) ;

	/*Now update the folder counts, the idea is taken from the imap provider implementation*/
	if (!(flags & CAMEL_STORE_FOLDER_INFO_FAST))
		update_folder_counts (groupwise_store, info, ex) ;

	//	camel_store_summary_save ((CamelStoreSummary *)groupwise_store->summary) ;
	return info ;
}

static gboolean
get_one_folder_offline (const char *physical_path, const char *path, gpointer data)
{
	GPtrArray *folders = data;
	CamelGroupwiseStore *groupwise_store = folders->pdata[0];
	CamelFolderInfo *fi;
	//      	CamelURL *url = NULL;
	
	if (*path != '/')
		return TRUE;
	
	fi = groupwise_build_folder_info(groupwise_store, NULL, path+1);
	g_ptr_array_add (folders, fi);
	return TRUE;
}

static CamelFolderInfo *
groupwise_get_folder_info_offline (CamelStore *store, const char *top,
			 guint32 flags, CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelFolderInfo *fi;
	GPtrArray *folders;
	char *storage_path;

	/* FIXME: obey other flags */

	folders = g_ptr_array_new ();

	/* A kludge to avoid having to pass a struct to the callback */
	g_ptr_array_add (folders, groupwise_store);
	storage_path = g_strdup_printf("%s/folders", groupwise_store->priv->storage_path);
	if (!e_path_find_folders (storage_path, get_one_folder_offline, folders)) {
		camel_disco_store_check_online (CAMEL_DISCO_STORE (groupwise_store), ex);
		fi = NULL;
	} else {
		g_ptr_array_remove_index_fast (folders, 0);
		fi = camel_folder_info_build (folders, "", '/', TRUE);
	}
	g_free(storage_path);

	g_ptr_array_free (folders, TRUE);
	return fi;
       
}

static CamelFolderInfo*
groupwise_create_folder(CamelStore *store,
		const char *parent_name,
		const char *folder_name,
		CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	CamelFolderInfo *root = NULL ;

	char *parent_id , *child_container_id, *temp_parent = NULL;

	int status;

	if(parent_name == NULL)
		parent_name = "" ;

	if (parent_name && (strlen(parent_name) > 0) ) {
		temp_parent = strrchr (parent_name,'/') ;
		if (temp_parent && temp_parent[0]) {
			temp_parent++ ;
			parent_id = g_hash_table_lookup (priv->name_hash, g_strdup(temp_parent)) ;
		} else
			parent_id = g_hash_table_lookup (priv->name_hash, g_strdup(parent_name)) ;
	} else
		parent_id = "" ;


	if (!E_IS_GW_CONNECTION( priv->cnc)) {
		if (!groupwise_connect_online (CAMEL_SERVICE(store), ex)) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_CANT_AUTHENTICATE, _("Authentication failed"));
			return NULL;
		}
	}
	status = e_gw_connection_create_folder(priv->cnc,parent_id,folder_name, &child_container_id) ;
	if (status == E_GW_CONNECTION_STATUS_OK) {
		root = groupwise_build_folder_info(groupwise_store, parent_name,folder_name) ;

		g_hash_table_insert (priv->id_hash, g_strdup(child_container_id), g_strdup(folder_name)) ; 
		g_hash_table_insert (priv->name_hash, g_strdup(folder_name), g_strdup(child_container_id)) ;
		g_hash_table_insert (priv->parent_hash, g_strdup(child_container_id), g_strdup(parent_id)) ;

		camel_object_trigger_event (CAMEL_OBJECT (store), "folder_created", root);
	}
	return root ;
}

static void 
groupwise_delete_folder(CamelStore *store,
				   const char *folder_name,
				   CamelException *ex)
{
	CamelGroupwiseStore *groupwise_store = CAMEL_GROUPWISE_STORE (store);
	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
	EGwConnectionStatus status ;
	const char *name = NULL;
	//const char * container = e_gw_connection_get_container_id(priv->cnc,folder_name) ;
	const char * container ; 
	
	name = strrchr (folder_name, '/') ;
	if (name) {
		name++ ;
		container = g_hash_table_lookup (priv->name_hash, name) ;
	} else
		container = g_hash_table_lookup (priv->name_hash, folder_name) ;

	
	status = e_gw_connection_remove_item (priv->cnc, container, container) ;

	if (status == E_GW_CONNECTION_STATUS_OK) {
		groupwise_forget_folder(groupwise_store,folder_name,ex) ;
		
		g_hash_table_remove (priv->id_hash, container) ;
		
		if (name)
			g_hash_table_remove (priv->name_hash, name) ;
		else 
			g_hash_table_remove (priv->name_hash, folder_name) ;
		
		g_hash_table_remove (priv->parent_hash, container) ;
	}
}
						     



static void 
groupwise_rename_folder(CamelStore *store,
					  const char *old_name,
					  const char *new_name,
					  CamelException *ex)
{
	/*	CamelGroupwiseStorePrivate  *priv = groupwise_store->priv;
		char *oldpath, *newpath, *storepath, *newname ;*/

	/*
	  1. check if online(if online/offline modes are supported
	  2. check if there are any subscriptions
	*/

}

char * 
groupwise_get_name(CamelService *service, gboolean brief) 
{
	if(brief) 
		return g_strdup_printf(_("GroupWise server %s"), service->url->host) ;
	else
		return g_strdup_printf(_("GroupWise service for %s on %s"), 
				       service->url->user, service->url->host) ;
}



static void
groupwise_forget_folder (CamelGroupwiseStore *gw_store, const char *folder_name, CamelException *ex)
{
	/**** IMPLEMENT MESSAGE CACHE *****/
	CamelFolderSummary *summary;
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;
	char *summary_file, *state_file;
	char *folder_dir, *storage_path;
	CamelFolderInfo *fi;
	const char *name ;

	
	name = folder_name ;

	storage_path = g_strdup_printf ("%s/folders", priv->storage_path) ;
	folder_dir = g_strdup(e_path_to_physical (storage_path,folder_name)) ;

	if (access(folder_dir, F_OK) != 0) {
		g_free(folder_dir) ;
		return ;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir) ;
	summary = camel_groupwise_summary_new(NULL,summary_file) ;
	if(!summary) {
		g_free(summary_file) ;
		g_free(folder_dir) ;
		return ;
	}

	/*	cache = camel_groupwise_message_cache_new (folder_dir, summary, ex) ;
		if (cache) 
		camel_groupwise_message_cache_clear (cache) ;

		camel_object_unref (cache) ;*/
	camel_object_unref (summary) ;
	unlink (summary_file) ;
	g_free (summary_file) ;


	state_file = g_strdup_printf ("%s/cmeta", folder_dir) ;
	unlink (state_file) ;
	g_free (state_file) ;

	rmdir (folder_dir) ;
	g_free (folder_dir) ;

	/*	camel_store_summary_remove_path ( (CamelStoreSummary *)gw_store->summary, folder_name) ;
		camel_store_summary_save ( (CamelStoreSummary *)gw_store->summary) ;*/

	fi = groupwise_build_folder_info(gw_store, NULL, folder_name) ;
	camel_object_trigger_event (CAMEL_OBJECT (gw_store), "folder_deleted", fi);
	camel_folder_info_free (fi) ;
}


char *
container_id_lookup (CamelGroupwiseStorePrivate *priv, const char *folder_name)
{
	return g_hash_table_lookup (priv->name_hash,folder_name) ;
}

EGwConnection *
cnc_lookup (CamelGroupwiseStorePrivate *priv)
{
	return priv->cnc ;
}

const char *
gw_get_path (CamelGroupwiseStore *gw_store, const char *folder_name)
{
	CamelGroupwiseStorePrivate *priv = gw_store->priv ;

	const char *str = g_strdup (folder_name) ;
	gchar *container_id = NULL, *temp_parent = NULL, *temp = NULL ;


	container_id = g_hash_table_lookup (priv->name_hash, folder_name) ;

	if (container_id)
		temp_parent = g_hash_table_lookup (priv->parent_hash, container_id) ;
	else 
		temp_parent = NULL ;
	while (temp_parent) {
		temp = g_hash_table_lookup (priv->id_hash, temp_parent ) ;
		if (temp == NULL) {
			break ;
		}	
		str = g_strconcat ( temp, "/", str, NULL) ;

		temp_parent = g_hash_table_lookup (priv->parent_hash, temp_parent) ;
	} 

	return str ;
}

static void
free_hash (gpointer key, gpointer value, gpointer data)
{
	if (value)
		g_free (value) ;
	if (key)
		g_free (key) ;
}


/*This should be called with connect_lock
 *
 * This function is an implementation based on 'get_folder_counts' in the IMAP
 * provider.
 */
static void
update_folder_counts (CamelGroupwiseStore *gw_store, CamelFolderInfo *fi, CamelException *ex)
{
	GSList *q ;
	CamelFolder *folder ;

	/*non-recursive breadth first search*/
	q = g_slist_append (NULL, fi) ;

	while (q) {
		fi = q->data ;
		q = g_slist_remove_link (q, q) ;

		while (fi) {

			if ( (fi->flags & CAMEL_FOLDER_NOSELECT) == 0) {
				
				/*Update the counts for the selected folder*/
				if (gw_store->current_folder &&
				    strcmp (gw_store->current_folder->full_name, fi->full_name) == 0) {

					CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(gw_store->current_folder))->refresh_info(gw_store->current_folder, ex);
					fi->unread = camel_folder_get_unread_message_count (gw_store->current_folder);
					fi->total = camel_folder_get_message_count(gw_store->current_folder);
				} else {
					/*Update the counts for all the other folders*/
					g_print ("|| GW:Other folder:%s||\n", fi->full_name) ;
					/*TODO: We have to somehow get the folder counts*/
					folder = camel_object_bag_peek(CAMEL_STORE(gw_store)->folders, fi->full_name);
					if (folder) {
						if (fi->unread != camel_folder_get_unread_message_count(folder)) {
							CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(folder))->refresh_info(folder, ex);
							fi->unread = camel_folder_get_unread_message_count(folder);
							fi->total = camel_folder_get_message_count(folder);
						}
						camel_object_unref(folder);
					}

				}
			} else {
				fi->unread = -1;
				fi->total = -1;
				folder = camel_object_bag_peek(CAMEL_STORE(gw_store)->folders, fi->full_name);
				if (folder) {
					if ((fi->flags & CAMEL_STORE_FOLDER_INFO_FAST) == 0)
						/* we use connect lock for everything, so this should be safe */
						CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(folder))->refresh_info(folder, NULL);
					fi->unread = camel_folder_get_unread_message_count(folder);
					fi->total = camel_folder_get_message_count(folder);
					camel_object_unref(folder);
				} else {
/*					char *storage_path, *folder_dir, *path;
					CamelFolderSummary *s;*/

					/* This is a lot of work for one path! */
/*					storage_path = g_strdup_printf("%s/folders", ((CamelGroupwiseStore *)gw_store)->priv->storage_path);
					folder_dir = imap_path_to_physical(storage_path, fi->full_name);
					path = g_strdup_printf("%s/summary", folder_dir);
					s = (CamelFolderSummary *)camel_object_new(camel_imap_summary_get_type());
					camel_folder_summary_set_build_content(s, TRUE);
					camel_folder_summary_set_filename(s, path);
					if (camel_folder_summary_header_load(s) != -1) {
						fi->unread = s->unread_count;
						fi->total = s->saved_count;
					}
					g_free(storage_path);
					g_free(folder_dir);
					g_free(path);

					camel_object_unref(s);*/
					g_print ("||| GW: Have to implement store summary|||\n") ;
				}

			}
			if (fi->child)
				q = g_slist_append (q, fi->child) ;
			fi = fi->next ;
		}
	}
}

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

#include "oc.h"

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
static void	camel_openchange_store_finalize(CamelObject *);

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
static CamelFolderInfo	*openchange_create_folder(CamelStore *, const char *, const char *, CamelException *);
static void		openchange_delete_folder(CamelStore *, const char *, CamelException *);
static void		openchange_rename_folder(CamelStore *, const char *, const char *, CamelException *);
static CamelFolderInfo	*mapi_get_folder_info(CamelStore *, const char *, guint32, CamelException *);
static void		openchange_subscribe_folder(CamelStore *, const char *, CamelException *);
static void		openchange_unsubscribe_folder(CamelStore *, const char *, CamelException *);
static void		openchange_noop(CamelStore *, CamelException *);

static void mapi_folders_sync (CamelMapiStore *store, CamelException *ex);

CamelStore *get_store(void)
{
	return (gl_store);
}

void set_store(CamelStore *store)
{
	gl_store = store;
}



static CamelFolder *openchange_get_inbox(CamelStore *store, CamelException *ex)
{
	//	return camel_openchange_folder_new(store, NULL, 0, ex);
	return NULL;
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
	REACHED;
	CamelServiceClass	*service_class = //(CamelServiceClass *) klass;
		CAMEL_SERVICE_CLASS (klass);
	CamelStoreClass		*store_class = (CamelStoreClass *) klass;
		CAMEL_STORE_CLASS (klass);

/* 	CamelServiceClass *camel_service_class = */
/* 		CAMEL_SERVICE_CLASS (camel_mapi_store_class); */
/* 	CamelStoreClass *camel_store_class = */
/* 		CAMEL_STORE_CLASS (camel_mapi_store_class); */

	parent_class = (CamelOfflineStoreClass *) camel_type_get_global_classfuncs(CAMEL_TYPE_OFFLINE_STORE);

	service_class->construct = mapi_construct;
	service_class->get_name = mapi_get_name;
	service_class->connect = mapi_connect;
	service_class->disconnect = mapi_disconnect;
	service_class->query_auth_types = mapi_query_auth_types;

	store_class->hash_folder_name = mapi_hash_folder_name;
	store_class->compare_folder_name = mapi_compare_folder_name;
	store_class->get_inbox = openchange_get_inbox;
	store_class->get_folder = mapi_get_folder;
	store_class->create_folder = openchange_create_folder;
	store_class->delete_folder = openchange_delete_folder;
	store_class->rename_folder = openchange_rename_folder;
	store_class->get_folder_info = mapi_get_folder_info;
	store_class->subscribe_folder = openchange_subscribe_folder;
	store_class->unsubscribe_folder = openchange_unsubscribe_folder;
	store_class->noop = openchange_noop;
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
				     (CamelObjectFinalizeFunc) camel_openchange_store_finalize);
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
	CamelMapiStorePrivate *priv = g_new0 (CamelMapiStorePrivate, 1);

	store->summary = NULL;

	priv->storage_path = NULL;
	priv->base_url = NULL;
/* 	store->camel_url = NULL; */
/* 	store->fi = NULL; */
/* 	store->trash_name = NULL; */
/* 	store->folders = NULL; */
/* 	store->folders_lock = NULL; */
/* 	store->connect_lock = NULL; */
	store->priv = priv;
}

static void camel_openchange_store_finalize(CamelObject *object)
{
}



/* service methods */
static void mapi_construct(CamelService *service, CamelSession *session,
				 CamelProvider *provider, CamelURL *url,
				 CamelException *ex)
{
	REACHED;
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
	printf("%s(%d):%s:storage_path = %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, priv->storage_path);
	if (!priv->storage_path)
		return;
	
	/*store summary*/
	path = g_alloca (strlen (priv->storage_path) + 32);
	sprintf (path, "%s/.summary", priv->storage_path);

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
/* 	priv->name_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free); */
/* 	priv->id_hash = g_hash_table_new (g_int_hash, g_int_equal); */
/* 	priv->parent_hash = g_hash_table_new (g_int_hash, g_int_equal); */

	store->flags &= ~CAMEL_STORE_VJUNK;
	//store->flags &= ~CAMEL_STORE_VTRASH;
}

static char *mapi_get_name(CamelService *service, gboolean brief)
{
	printf("%s(%d):%s:REACHED \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	if (brief) {
		return g_strdup_printf(_("Exchange MAPI server %s"), service->url->host);
	} else {
		return g_strdup_printf(_("Exchange MAPI for %s on %s"),
				       service->url->user, service->url->host);
	}
}

static gboolean mapi_connect(CamelService *service, CamelException *ex)
{
	//TODO : Connection here ? init mapi ?
	return TRUE;
}

static gboolean mapi_disconnect(CamelService *service, gboolean clean, CamelException *ex)
{
	return TRUE;
}

static GList *mapi_query_auth_types(CamelService *service, CamelException *ex)
{
  return NULL;
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

/* FIXME: testing */
static CamelFolderInfo *create_exchange_folder(CamelStore *store, const char *folder_name)
{
	return NULL;
}
/* FIXME */
static CamelFolderInfo *create_exchange_subfolder(CamelStore *store, const char *parent_name, const char *folder_name)
{
	return NULL;
}

static void openchange_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void openchange_rename_folder(CamelStore *store, const char *old_name, const char *new_name, CamelException *ex)
{

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

	printf("%s(%d):%s:summary count : %d \n", __FILE__, __LINE__, __PRETTY_FUNCTION__ ,camel_store_summary_count((CamelStoreSummary *)mapi_store->summary));
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

	id = folder_mapi_ids_to_uid(exchange_mapi_folder_get_fid (folder));
		
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
	parent = folder_mapi_ids_to_uid(mapi_id_folder);
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

	/*Refresh info*/
	//FIXME : Disable for now . later fix this
/* 	if (store->current_folder  */
/* 	    && !strcmp (store->current_folder->full_name, fi->full_name) */
/* 	    && type != E_GW_CONTAINER_TYPE_INBOX) { */
/* 		CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS (store->current_folder))->refresh_info(store->current_folder, ex); */
/* 	} */
	return fi;
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

	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_AVAIL) {
		if (((CamelService *)store)->status == CAMEL_SERVICE_DISCONNECTED){
			((CamelService *)store)->status = CAMEL_SERVICE_CONNECTING;
			mapi_connect ((CamelService *)store, ex);
		}
	}


	if (!exchange_mapi_connection_exists ()) {
		g_warning ("mapi_folder_sync : No Connection\n");
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
		fid = folder_mapi_ids_to_uid (exchange_mapi_folder_get_fid((ExchangeMAPIFolder *)(temp_list->data)));
		parent_id = folder_mapi_ids_to_uid (exchange_mapi_folder_get_parent_id ((ExchangeMAPIFolder *)(temp_list->data)));

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
	//	e_gw_connection_free_container_list (list);
	count = camel_store_summary_count ((CamelStoreSummary *)store->summary);
	for (i=0;i<count;i++) {
		si = camel_store_summary_index ((CamelStoreSummary *)store->summary, i);
		if (si == NULL)
			continue;

		info = g_hash_table_lookup (present, camel_store_info_path (store->summary, si));
		if (info != NULL) {
			printf("%s(%d):%s:adding : %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, si->path);
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
	
	if (top) {
		top_folder = g_hash_table_lookup (priv->name_hash, top);
		/* 'top' is a valid path, but doesnt have a container id
		 *  return NULL */
/*		if (!top_folder) {
			camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					_("You must be working online to complete this operation"));
			return NULL;
		}*/
	}

/* 	if (top && mapi_is_system_folder (top))  */
/* 		return mapi_build_folder_info (mapi_store, NULL, top ); */

	/*
	 * Thanks to Michael, for his cached folders implementation in IMAP
	 * is used as is here.
	 */
	if (camel_store_summary_count ((CamelStoreSummary *)mapi_store->summary) == 0) {
	/* 	if (mapi_store->list_loaded == 3) { */
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

	if ((camel_store_summary_count((CamelStoreSummary *)mapi_store->summary) > 0) ) {//&& (mapi_store->list_loaded > 1)) {
		/*Load from cache*/
		//		mapi_store->list_loaded -= 1;
		goto end_r;
	}

	mapi_folders_sync (mapi_store, ex);
	if (camel_exception_is_set (ex)) {
		CAMEL_SERVICE_REC_UNLOCK (store, connect_lock);
		return NULL;
	}
	camel_store_summary_touch ((CamelStoreSummary *)mapi_store->summary);
	camel_store_summary_save ((CamelStoreSummary *)mapi_store->summary);

	/*camel_exception_clear (ex);*/
end_r:
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



/*
** this function return the folder list and hierarchy
** if failled : return NULL
*/
static CamelFolderInfo *openchange_get_folder_info1(CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	return NULL;
/* 	static CamelFolderInfo	*fi = NULL; */
/* 	static CamelFolderInfo	*tmp = NULL; */
/* 	CamelURL		*url; */
/* 	mapi_object_t		obj_store; */
/* 	mapi_object_t		obj_inbox; */
/* 	TALLOC_CTX		*mem_ctx; */
/* 	enum MAPISTATUS		retval; */
/* 	uint64_t		id_inbox; */
/* 	char *t; */
/* 	CamelFolderInfo	*folder_info; */

/* 	goto test_summary; */
/* 	if (((CamelOpenchangeStore *)store)->fi == NULL && fi != NULL) */
/* 		((CamelOpenchangeStore *)store)->fi = fi; */
/* 	if (fi) return (fi); */
/* 	oc_thread_connect_lock(); */
/* 	if (m_oc_initialize() == -1) { */
/* 		oc_thread_connect_unlock(); */
/* 		return (NULL); */
/* 	} */

/* 	mem_ctx = talloc_init("oc_get_folder_info"); */
/* 	mapi_object_init(&obj_store); */
/* 	mapi_object_init(&obj_inbox); */
/* 	/\* session::OpenMsgStore() *\/ */
/* 	retval = OpenMsgStore(&obj_store); */
/* 	mapi_errstr("OpenMsgStore", GetLastError()); */
/* 	if (retval != MAPI_E_SUCCESS) goto error; */

/* 	fi = g_malloc0(sizeof(CamelOpenchangeFolderInfo)); */
/* 	tmp = fi; */
/* 	fi->name = g_strdup("INBOXqw"); */
/* 	fi->full_name = g_strdup("INBOX"); */
/* 	printf("%s(%d):%s:reached \n", __FILE__, __LINE__, __PRETTY_FUNCTION__); */
/* 	fi->next = NULL; */
/* 	fi->parent = NULL; */
/* 	fi->child = NULL; */
/* 	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX; */
/* 	((CamelOpenchangeStore *)store)->fi = fi; */
/* 	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderInbox); */
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
/* 	if (retval != MAPI_E_SUCCESS) goto error; */
/* 	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox); */
/* 	mapi_errstr("OpenFolder", GetLastError()); */

/* 	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox); */
/* 	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid); */
/* 	fi->name = folder_info->name; */
/* /\* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); *\/ */

/* 	url = camel_url_copy(((CamelService *)store)->url); */
/* 	fi->uri = camel_url_to_string(url, 0); */
/* 	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name); */
/* /\* 	camel_url_free(url); *\/ */

/* 	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name); */
/* 	camel_url_free(url); */

/* 	/\* count msg *\/ */
/* 	retval = GetFolderItemsCount(&obj_inbox, */
/* 				     &(fi->unread), */
/* 				     &(fi->total)); */
/* 	mapi_object_release(&obj_inbox); */
/* 	mapi_object_init(&obj_inbox); */
/* 	fi->next = g_malloc0(sizeof(CamelOpenchangeFolderInfo)); */
/* 	fi = fi->next; */
/* 	fi->name = g_strdup("TRASH"); */
/* 	fi->full_name = g_strdup("TRASH"); */
/* 	fi->next = NULL; */
/* 	fi->parent = NULL; */
/* 	fi->child = NULL; */
/* 	url = camel_url_copy(((CamelService *)store)->url); */
/* 	camel_url_set_fragment(url, fi->full_name); */
/* 	fi->uri = camel_url_to_string(url, 0); */
/* 	camel_url_free(url); */
/* 	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TRASH | CAMEL_FOLDER_VTRASH; */
/* 	((CamelOpenchangeStore *)store)->fi = fi; */
/* 	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderDeletedItems); */
/* 	mapi_errstr("GetDefaultFolder", GetLastError()); */
/* 	if (retval != MAPI_E_SUCCESS) goto error; */
/* 	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox); */
/* 	mapi_errstr("OpenFolder", GetLastError()); */
/* 	if (retval != MAPI_E_SUCCESS) goto error; */
/* 	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox); */
/* 	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid); */
/* 	fi->name = folder_info->name; */
/* /\* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); *\/ */

/* 	url = camel_url_copy(((CamelService *)store)->url); */
/* 	fi->uri = camel_url_to_string(url, 0); */
/* 	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name); */
/* /\* 	camel_url_free(url); *\/ */

/* 	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name); */
/* 	camel_url_free(url); */
/* 	/\* count msg *\/ */
/* 	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox); */
/* 	retval = GetFolderItemsCount(&obj_inbox, */
/* 				     &(fi->unread), */
/* 				     &(fi->total)); */
/* 	mapi_object_release(&obj_inbox); */
/* 	mapi_object_release(&obj_store); */
/* 	talloc_free(mem_ctx); */
/* 	fi = tmp; */
/* 	((CamelOpenchangeStore *)store)->fi = fi; */
/* 	oc_thread_connect_unlock(); */
/* 	return fi; */
/* error: */
/* 	talloc_free(mem_ctx); */
/* 	return (NULL); */
/* test_summary: */

/* 	if (fi) */
/* 		return (fi); */
/* 	t = (char *)top; */
/* 	if (!t) */
/* 		t = ""; */
/* 	((CamelOpenchangeStore *)store)->fi = (CamelFolderInfo *)oc_store_summary_get_by_fullname(((CamelOpenchangeStore *)store)->summary, t); */
/* 	fi = ((CamelOpenchangeStore *)store)->fi; */
/* 	return (((CamelOpenchangeStore *)store)->fi); */
}

static void openchange_subscribe_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void openchange_unsubscribe_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void openchange_noop(CamelStore *store, CamelException *ex)
{

}




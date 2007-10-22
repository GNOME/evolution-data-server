/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
#define d(x) x

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <libmapi/libmapi.h>
#include <param.h>



static CamelStore *gl_store = NULL;

static CamelOfflineStoreClass *parent_class = NULL;

static void	camel_openchange_store_class_init(CamelOpenchangeStoreClass *);
CamelType	camel_openchange_store_get_type(void);
static void	camel_openchange_store_init(CamelOpenchangeStore *, CamelOpenchangeStoreClass *);
static void	camel_openchange_store_finalize(CamelObject *);

/* service methods */
static void	openchange_construct(CamelService *, CamelSession *,
				     CamelProvider *, CamelURL *,
				     CamelException *);
static char	*openchange_get_name(CamelService *, gboolean );
static gboolean	openchange_connect(CamelService *, CamelException *);
static gboolean	openchange_disconnect(CamelService *, gboolean , CamelException *);
static GList	*openchange_query_auth_types(CamelService *, CamelException *);

/* store methods */
static CamelFolder	*openchange_get_folder(CamelStore *, const char *, guint32, CamelException *);
static CamelFolderInfo	*openchange_create_folder(CamelStore *, const char *, const char *, CamelException *);
static void		openchange_delete_folder(CamelStore *, const char *, CamelException *);
static void		openchange_rename_folder(CamelStore *, const char *, const char *, CamelException *);
static CamelFolderInfo	*openchange_get_folder_info(CamelStore *, const char *, guint32, CamelException *);
static void		openchange_subscribe_folder(CamelStore *, const char *, CamelException *);
static void		openchange_unsubscribe_folder(CamelStore *, const char *, CamelException *);
static void		openchange_noop(CamelStore *, CamelException *);


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
	return camel_openchange_folder_new(store, NULL, 0, ex);
}

static guint openchange_hash_folder_name(gconstpointer key)
{
	if (g_ascii_strcasecmp(key, "INBOX") == 0) {
		return g_str_hash("INBOX");
	} else {
		return g_str_hash(key);
	}
}

static int openchange_compare_folder_name(gconstpointer a, gconstpointer b)
{
	gconstpointer	aname = a; 
	gconstpointer	bname = b;
  
	if (g_ascii_strcasecmp(a, "INBOX") == 0) {
		aname = "INBOX";
	}
	if (g_ascii_strcasecmp(b, "INBOX") == 0) {
		bname = "INBOX";
	}
  
	return g_str_equal(aname, bname);
}

static void camel_openchange_store_class_init(CamelOpenchangeStoreClass *klass)
{
	CamelServiceClass	*service_class = (CamelServiceClass *) klass;
	CamelStoreClass		*store_class = (CamelStoreClass *) klass;

	parent_class = (CamelOfflineStoreClass *) camel_type_get_global_classfuncs(CAMEL_TYPE_OFFLINE_STORE);
	service_class->construct = openchange_construct;
	service_class->get_name = openchange_get_name;
	service_class->connect = openchange_connect;
	service_class->disconnect = openchange_disconnect;
	service_class->query_auth_types = openchange_query_auth_types;
	store_class->hash_folder_name = openchange_hash_folder_name;
	store_class->compare_folder_name = openchange_compare_folder_name;

	store_class->get_inbox = openchange_get_inbox;
	store_class->get_folder = openchange_get_folder;
	store_class->create_folder = openchange_create_folder;
	store_class->delete_folder = openchange_delete_folder;
	store_class->rename_folder = openchange_rename_folder;
	store_class->get_folder_info = openchange_get_folder_info;
	store_class->subscribe_folder = openchange_subscribe_folder;
	store_class->unsubscribe_folder = openchange_unsubscribe_folder;
	store_class->noop = openchange_noop;
}

CamelType camel_openchange_store_get_type(void)
{
	static CamelType	type = 0;
  
  if (!type) {
	  type = camel_type_register(camel_offline_store_get_type (),
				     "CamelOpenchangeStores",
				     sizeof (CamelOpenchangeStore),
				     sizeof (CamelOpenchangeStoreClass),
				     (CamelObjectClassInitFunc) camel_openchange_store_class_init,
				     NULL,
				     (CamelObjectInitFunc) camel_openchange_store_init,
				     (CamelObjectFinalizeFunc) camel_openchange_store_finalize);
  }

  return type;
}

/*
** store is already initilyse to NULL or 0 value
** klass already have a parent_class
** nothing must be doing here
*/
static void camel_openchange_store_init(CamelOpenchangeStore *store, CamelOpenchangeStoreClass *klass)
{
	store->storage_path = NULL;
	store->base_url = NULL;
	store->camel_url = NULL;
	store->fi = NULL;
	store->trash_name = NULL;
	store->folders = NULL;
	store->folders_lock = NULL;
	store->connect_lock = NULL;
	store->summary = NULL;
}

static void camel_openchange_store_finalize(CamelObject *object)
{
}



/* service methods */
static void openchange_construct(CamelService *service, CamelSession *session,
				 CamelProvider *provider, CamelURL *url,
				 CamelException *ex)
{
	CamelOpenchangeStore	*store = (CamelOpenchangeStore *) service;
	char			*buf = NULL;
	CamelURL		*url2;

	store->camel_url = camel_url_copy(url);
/* 	store->summary = oc_store_summary_new(camel_url_copy(url)); */
	CAMEL_SERVICE_CLASS (parent_class)->construct(service, session, provider, url, ex);
	if (camel_exception_is_set(ex)) return;

/* 	if (camel_url_get_param(url, "use_lsub")) { */
/* 		((CamelStore *) store)->flags |= CAMEL_STORE_SUBSCRIPTIONS; */
/* 	} */

	store->storage_path = camel_session_get_storage_path(session, service, ex);

/* 	service->url->host = g_strdup("localhost"); */
/* 	openchange_get_folder_info((CamelStore *)store, "/", 0, ex); */

/* 	((CamelStore *)store)->flags &= ~CAMEL_STORE_VTRASH; */
/* 	/\* setup/load the summary *\/ */
	buf = g_malloc0(strlen(store->storage_path) + 32);
	sprintf(buf, "%s/.summary", store->storage_path);
	url2 = camel_url_copy(((CamelService *)store)->url);
	store->summary = oc_store_summary_new(url2);
	oc_store_summary_set_filename(store->summary, buf);
	oc_store_summary_update_info(store->summary);
	
/* 	buf = camel_url_to_string(service->url, CAMEL_URL_HIDE_ALL); */
/* 	url = camel_url_new(buf, NULL); */
	g_free(buf);
/* 	camel_url_free(url); */
/* 	oc_store_summary_load ((CamelStoreSummary *) store->summary); */
}

static char *openchange_get_name(CamelService *service, gboolean brief)
{
	if (brief) {
		return g_strdup_printf(_("Openchange server %s"), service->url->host);
	} else {
		return g_strdup_printf(_("openchange service for %s on %s"),
				       service->url->user, service->url->host);
	}
}

static gboolean openchange_connect(CamelService *service, CamelException *ex)
{
	return TRUE;
}

static gboolean openchange_disconnect(CamelService *service, gboolean clean, CamelException *ex)
{
	return TRUE;
}

static GList *openchange_query_auth_types(CamelService *service, CamelException *ex)
{
  return NULL;
}

static CamelFolder *openchange_get_folder(CamelStore *store, const char *folder_name, guint32 flags, CamelException *ex)
{
	if (!(((CamelOpenchangeStore *)store)->fi)) {
		oc_store_summary_update_info(((CamelOpenchangeStore *)store)->summary);
		openchange_get_folder_info(store, NULL, 0, ex);
	}
	return camel_openchange_folder_new(store, folder_name, flags, ex);
}

/* FIXME: testing */
static CamelFolderInfo *create_exchange_folder(CamelStore *store, const char *folder_name)
{
	CamelOpenchangeFolderInfo *new;
	CamelFolderInfo *std_info;
	CamelOpenchangeStore *oc_store = (CamelOpenchangeStore*)store;
	CamelURL *url;

	new = g_malloc0(1 * sizeof(CamelOpenchangeFolderInfo));
	std_info = (CamelFolderInfo*)new;
/* 	printf("DEBUG: alloc : done\n"); */

	/* setting name and full_name */
	std_info->name = g_strdup(folder_name);
	std_info->full_name = g_strdup(folder_name);
/* 	printf("DEBUG: name : done\n"); */
	
	/* setting uri */
	url = camel_url_copy(oc_store->camel_url);
	camel_url_set_path(url, g_strdup(std_info->full_name));
	std_info->uri = camel_url_to_string(url, CAMEL_URL_HIDE_PARAMS);
/* 	printf("DEBUG: uri : done\n"); */

	/* setting update list */
	std_info->next = oc_store->fi;
	oc_store->fi = std_info;
/* 	printf("DEBUG: list : done\n"); */

	/* FIXME including exchange creator */
/* 	oc_thread_connect_lock(); */
/* 	{ */
/* 		enum MAPISTATUS		retval; */
/* 		TALLOC_CTX		*mem_ctx; */
/* 		BOOL			ret = True; */
/* 		mapi_object_t		obj_store; */
/* 		mapi_object_t		obj_inbox; */
/* 		mapi_id_t		id_inbox; */
/* 		struct mapi_session	*session; */
		
		
/* 		/\* init torture *\/ */
/* 		mem_ctx = talloc_init("torture_rpc_mapi_folder"); */
		
/* 		/\* init objects *\/ */
/* 		mapi_object_init(&obj_store); */
/* 		mapi_object_init(&obj_inbox); */
		
/* 		/\* session::OpenMsgStore() *\/ */
/* 		retval = OpenMsgStore(&obj_store); */
/* 		mapi_errstr("OpenMsgStore", GetLastError()); */
/* 		if (retval != MAPI_E_SUCCESS) return False; */
/* 		mapi_object_debug(&obj_store); */
		
/* 		/\* id_inbox = store->GeInboxFolder() *\/ */
/* /\* 		retval = GetReceiveFolder(&obj_store, &id_inbox); *\/ */
/* /\* 		mapi_errstr("GetReceiveFolder", GetLastError()); *\/ */
/* 		if (retval != MAPI_E_SUCCESS) return False; */
/* 		{ */
/* 			retval = CreateFolder(&obj_store, folder_name, "test", &obj_inbox); */
/* 			mapi_errstr("CreateFolder", GetLastError()); */
/* 			if (retval != MAPI_E_SUCCESS) return NULL; */
/* 		} */
		
/* 		/\* objects->Release() */
/* 		 *\/ */
/* 		mapi_object_release(&obj_inbox); */
/* 		mapi_object_release(&obj_store); */
		
/* 		talloc_free(mem_ctx); */
			
		
/* 	} */
/* 	oc_thread_connect_unlock(); */

	/* FIXME */
	new->fid = "test";
	new->fid = std_info->full_name;

	/* save it in our summary */
	oc_store_summary_add_info(oc_store->summary, new);

	std_info->flags = CAMEL_STORE_READ | CAMEL_STORE_WRITE;

/* 	printf("DEBUG new->flags : %d\n", std_info->flags); */

	return std_info;
}
/* FIXME */
static CamelFolderInfo *create_exchange_subfolder(CamelStore *store, const char *parent_name, const char *folder_name)
{
	CamelOpenchangeFolderInfo *new;
	CamelFolderInfo *std_info;
	CamelOpenchangeStore *oc_store = (CamelOpenchangeStore*)store;
	CamelURL *url;

	new = g_malloc0(1 * sizeof(CamelOpenchangeFolderInfo));
	std_info = (CamelFolderInfo*)new;
/* 	printf("DEBUG: alloc : done\n"); */

	/* setting name and full_name */
	std_info->name = g_strdup(folder_name);
	std_info->full_name = g_strdup(folder_name);
/* 	printf("DEBUG: name : done\n"); */
	
	/* setting uri */
	url = camel_url_copy(oc_store->camel_url);
	camel_url_set_path(url, g_strdup(std_info->full_name));
	std_info->uri = camel_url_to_string(url, CAMEL_URL_HIDE_PARAMS);
/* 	printf("DEBUG: uri : done\n"); */

	/* setting update list */
	/* putting folder in list like that */
	std_info->next = oc_store->fi;
	oc_store->fi = std_info;
/* 	printf("DEBUG: list : done\n"); */

	/* FIXME including exchange creator */
	oc_thread_connect_lock();
	{
		/* FIXME */
	}
	oc_thread_connect_unlock();

	/* FIXME */
	new->fid = "test";
	new->fid = std_info->full_name;

	/* save it in our summary */
	oc_store_summary_add_info(oc_store->summary, new);

	std_info->flags = CAMEL_STORE_READ | CAMEL_STORE_WRITE;

/* 	printf("DEBUG new->flags : %d\n", std_info->flags); */

	return std_info;
}

static CamelFolderInfo *openchange_create_folder(CamelStore *store, const char *parent_name, const char *folder_name, CamelException *ex)
{
/* 	printf("parent_name:: %p +%s+ folder_name : %s\n", parent_name, parent_name, folder_name); */
/* 	OC_DEBUG(folder_name); */
/* 	OC_DEBUG(parent_name); */
/* 	printf("url : = %p\n", ((CamelOpenchangeStore *)store)->camel_url); */
/* 	if (((CamelOpenchangeStore *)store)->camel_url) */
/* 		printf("url : = %p %s \n", ((CamelOpenchangeStore *)store)->camel_url, camel_url_to_string(((CamelOpenchangeStore *)store)->camel_url, 0)); */
/* 	if (!store || !folder_name || (strlen(folder_name) == 0)) */
/* 		return (NULL); */
	if (!parent_name || (strlen(parent_name) == 0))
		return (create_exchange_folder(store, folder_name));
/* 	printf("hum...\n"); */
	return (create_exchange_subfolder(store, parent_name, folder_name));
}

static void openchange_delete_folder(CamelStore *store, const char *folder_name, CamelException *ex)
{

}

static void openchange_rename_folder(CamelStore *store, const char *old_name, const char *new_name, CamelException *ex)
{

}

/*
** this function return the folder list and hierarchy
** if failled : return NULL
*/
static CamelFolderInfo *openchange_get_folder_info(CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	static CamelFolderInfo	*fi = NULL;
	static CamelFolderInfo	*tmp = NULL;
	CamelURL		*url;
	mapi_object_t		obj_store;
	mapi_object_t		obj_inbox;
	TALLOC_CTX		*mem_ctx;
	enum MAPISTATUS		retval;
	uint64_t		id_inbox;
	char *t;
	CamelFolderInfo	*folder_info;

	goto test_summary;
	if (((CamelOpenchangeStore *)store)->fi == NULL && fi != NULL)
		((CamelOpenchangeStore *)store)->fi = fi;
	if (fi) return (fi);
	oc_thread_connect_lock();
	if (m_oc_initialize() == -1) {
		oc_thread_connect_unlock();
		return (NULL);
	}

	mem_ctx = talloc_init("oc_get_folder_info");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_inbox);
	/* session::OpenMsgStore() */
	retval = OpenMsgStore(&obj_store);
	mapi_errstr("OpenMsgStore", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;

	fi = g_malloc0(sizeof(CamelOpenchangeFolderInfo));
	tmp = fi;
	fi->name = g_strdup("INBOX");
	fi->full_name = g_strdup("INBOX");
	fi->next = NULL;
	fi->parent = NULL;
	fi->child = NULL;
	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_INBOX;
	((CamelOpenchangeStore *)store)->fi = fi;
	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderInbox);
	mapi_errstr("GetDefaultFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox);
	mapi_errstr("OpenFolder", GetLastError());

	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid);
	fi->name = folder_info->name;
/* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); */

	url = camel_url_copy(((CamelService *)store)->url);
	fi->uri = camel_url_to_string(url, 0);
	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name);
/* 	camel_url_free(url); */

	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name);
	camel_url_free(url);

	/* count msg */
	retval = GetFolderItemsCount(&obj_inbox,
				     &(fi->unread),
				     &(fi->total));
	mapi_object_release(&obj_inbox);
	mapi_object_init(&obj_inbox);
	fi->next = g_malloc0(sizeof(CamelOpenchangeFolderInfo));
	fi = fi->next;
	fi->name = g_strdup("OUTBOX");
	fi->full_name = g_strdup("OUTBOX");
	fi->next = NULL;
	fi->parent = NULL;
	fi->child = NULL;
	url = camel_url_copy(((CamelService *)store)->url);
	camel_url_set_fragment(url, fi->full_name);
	fi->uri = camel_url_to_string(url, 0);
	camel_url_free(url);
	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_OUTBOX;
	((CamelOpenchangeStore *)store)->fi = fi;
	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderOutbox);
	mapi_errstr("GetDefaultFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox);
	mapi_errstr("OpenFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid);
	fi->name = folder_info->name;
/* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); */

	url = camel_url_copy(((CamelService *)store)->url);
	fi->uri = camel_url_to_string(url, 0);
	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name);
/* 	camel_url_free(url); */

	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name);
	camel_url_free(url);
	/* count msg */
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	retval = GetFolderItemsCount(&obj_inbox,
				     &(fi->unread),
				     &(fi->total));
	
	mapi_object_release(&obj_inbox);
	mapi_object_init(&obj_inbox);
	fi->next = g_malloc0(sizeof(CamelOpenchangeFolderInfo));
	fi = fi->next;
	fi->name = g_strdup("SENT");
	fi->full_name = g_strdup("SENT");
	fi->next = NULL;
	fi->parent = NULL;
	fi->child = NULL;
	url = camel_url_copy(((CamelService *)store)->url);
	camel_url_set_fragment(url, fi->full_name);
	fi->uri = camel_url_to_string(url, 0);
	camel_url_free(url);
	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_SENT;
	((CamelOpenchangeStore *)store)->fi = fi;
	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderSentMail);
	mapi_errstr("GetDefaultFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox);
	mapi_errstr("OpenFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid);
	fi->name = folder_info->name;
/* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); */

	url = camel_url_copy(((CamelService *)store)->url);
	fi->uri = camel_url_to_string(url, 0);
	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name);
/* 	camel_url_free(url); */

	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name);
	camel_url_free(url);
	/* count msg */
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	retval = GetFolderItemsCount(&obj_inbox,
				     &(fi->unread),
				     &(fi->total));
	
	mapi_object_release(&obj_inbox);
	mapi_object_init(&obj_inbox);
	fi->next = g_malloc0(sizeof(CamelOpenchangeFolderInfo));
	fi = fi->next;
	fi->name = g_strdup("TRASH");
	fi->full_name = g_strdup("TRASH");
	fi->next = NULL;
	fi->parent = NULL;
	fi->child = NULL;
	url = camel_url_copy(((CamelService *)store)->url);
	camel_url_set_fragment(url, fi->full_name);
	fi->uri = camel_url_to_string(url, 0);
	camel_url_free(url);
	fi->flags = CAMEL_FOLDER_SYSTEM | CAMEL_FOLDER_TYPE_TRASH | CAMEL_FOLDER_VTRASH;
	((CamelOpenchangeStore *)store)->fi = fi;
	retval = GetDefaultFolder(&obj_store, &id_inbox, olFolderDeletedItems);
	mapi_errstr("GetDefaultFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	retval = OpenFolder(&obj_store, id_inbox, &obj_inbox);
	mapi_errstr("OpenFolder", GetLastError());
	if (retval != MAPI_E_SUCCESS) goto error;
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	folder_info = (CamelFolderInfo *)oc_store_summary_get_by_fid(((CamelOpenchangeStore *)store)->summary, ((CamelOpenchangeFolderInfo *)fi)->fid);
	fi->name = folder_info->name;
/* 	fi->full_name = camel_url_encode(folder_info->full_name, 0); */

	url = camel_url_copy(((CamelService *)store)->url);
	fi->uri = camel_url_to_string(url, 0);
	fi->uri = g_strdup_printf("%s#%s", fi->uri, fi->full_name);
/* 	camel_url_free(url); */

	printf("DEBUG:: uri : %s name :%s\n", fi->uri, fi->name);
	camel_url_free(url);
	/* count msg */
	((CamelOpenchangeFolderInfo *)fi)->fid = folder_mapi_ids_to_uid(id_inbox);
	retval = GetFolderItemsCount(&obj_inbox,
				     &(fi->unread),
				     &(fi->total));
	mapi_object_release(&obj_inbox);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	fi = tmp;
	((CamelOpenchangeStore *)store)->fi = fi;
	oc_thread_connect_unlock();
	return fi;
error:
	talloc_free(mem_ctx);
	return (NULL);
test_summary:

	if (fi)
		return (fi);
	t = (char *)top;
	if (!t)
		t = "";
	((CamelOpenchangeStore *)store)->fi = (CamelFolderInfo *)oc_store_summary_get_by_fullname(((CamelOpenchangeStore *)store)->summary, t);
	fi = ((CamelOpenchangeStore *)store)->fi;
	return (((CamelOpenchangeStore *)store)->fi);
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




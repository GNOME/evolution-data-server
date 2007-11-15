/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Srinivasa Ragavan <sragavan@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include "exchange-mapi-connection.h"
#include "exchange-mapi-folder.h"
#include <param.h>

#define DEFAULT_PROF_PATH ".evolution/mapi-profiles.ldb"
#define d(x) x

static struct mapi_session *global_mapi_session= NULL;
static GStaticRecMutex connect_lock = G_STATIC_REC_MUTEX_INIT;


#define LOCK()		printf("%s(%d):%s: lock(connect_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);;g_static_rec_mutex_lock(&connect_lock)
#define UNLOCK()	printf("%s(%d):%s: unlock(connect_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_unlock(&connect_lock)
#define LOGALL() 	lp_set_cmdline(global_loadparm, "log level", "10"); global_mapi_ctx->dumpdata = TRUE;
#define LOGNONE()       lp_set_cmdline(global_loadparm, "log level", "0"); global_mapi_ctx->dumpdata = FALSE;
#define ENABLE_VERBOSE_LOG() 	global_mapi_ctx->dumpdata = TRUE;
#define ENABLE_VERBOSE_LOG()

static struct mapi_session *
mapi_profile_load(const char *profname, const char *password)
{
	enum MAPISTATUS	retval;
	enum MAPISTATUS status;
	gchar *profpath = NULL;
	struct mapi_session *session = NULL;
	char *profile = profname;
	/* Check if the session is already there before calling this. This cant/wont check that. */

	d(printf("Loading profile with %s\n", profname));
	
	profpath = g_build_filename (g_getenv("HOME"), DEFAULT_PROF_PATH, NULL);
	if (!g_file_test (profpath, G_FILE_TEST_EXISTS)) {
		g_warning ("Database not found\n");
		return NULL;
	}

	MAPIUninitialize ();

	if (MAPIInitialize(profpath) != MAPI_E_SUCCESS){
		g_free(profpath);
		profpath = NULL;
		status = GetLastError();
		if (status == MAPI_E_SESSION_LIMIT){
			d(printf("Already connect - Still connected"));
			mapi_errstr("MAPIInitialize", GetLastError());
			return NULL;
		}
		else {
			g_warning ("Load profile: Generic error : %d\n", status);
			return NULL;
		}
	}

	ENABLE_VERBOSE_LOG ();
	if (!profile) {
		if ((retval = GetDefaultProfile(&profile)) != MAPI_E_SUCCESS) {
/* 			mapi_errstr("GetDefaultProfile", GetLastError()); */
			return -1;
		}

	}
	if (MapiLogonEx(&session, profile, password) == -1){
		retval = GetLastError();
		mapi_errstr("Error ", retval);
		if (retval == MAPI_E_NETWORK_ERROR){
			g_warning ("Network error\n");
			return NULL;
		}
		if (retval == MAPI_E_LOGON_FAILED){
			g_warning ("LOGIN Failed\n");
			return NULL;
		}
		g_warning ("Generic error\n");
		return NULL;
	}
	
	g_free (profpath);
	
	return session;
  
}

gboolean 
exchange_mapi_connection_new (const char *profile, const char *password)
{
	LOCK ();
	if (global_mapi_session) {
		UNLOCK ();
		return TRUE;
	}
	global_mapi_session = mapi_profile_load (profile, password);
	UNLOCK ();

	if (!global_mapi_session)
		return FALSE;
	
	printf("Succccccccccccccccccccccccces\n");
	return TRUE;
}

gboolean
exchange_mapi_connection_exists ()
{
	return global_mapi_session != NULL;
}

// FIXME: May be we need to support Restrictions/Filters here. May be after libmapi-0.7.

gboolean
exchange_mapi_connection_fetch_items (uint32_t olFolder, struct mapi_SRestriction *res, FetchItemsCallback cb, mapi_id_t fid, gpointer data)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	uint32_t count;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;
	struct mapi_SPropValue_array properties_array;
	TALLOC_CTX *mem_ctx;
	int i;

	printf("Fetching folder %016llx\n", fid);
	
	LOCK ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	printf("Opening folder %llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
		UNLOCK ();
		return FALSE;
	}

	mapi_object_init(&obj_table);
	retval = GetContentsTable(&obj_folder, &obj_table);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-getcontentstable failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x8,
					  PR_FID,
					  PR_MID,
					  PR_INST_ID,
					  PR_INSTANCE_NUM,
					  PR_SUBJECT,
					  PR_MESSAGE_CLASS,
					  PR_RULE_MSG_PROVIDER,
					  PR_RULE_MSG_NAME);
	retval = SetColumns(&obj_table, SPropTagArray);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-setcolumns failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	if (res) {
		/* Applying any restriction that are set. */
		retval = Restrict(&obj_table, res);
		if (retval != MAPI_E_SUCCESS) {
			g_warning ("Failed while setting restrictions\n");
			mapi_errstr(__PRETTY_FUNCTION__, GetLastError());					
			UNLOCK ();
			return FALSE;
		}
	}

	retval = GetRowCount(&obj_table, &count);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-getrowcount failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());
		UNLOCK ();
		return FALSE;
	}

	retval = QueryRows(&obj_table, count, TBL_ADVANCE, &SRowSet);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-queryrows failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	for (i = 0; i < SRowSet.cRows; i++) {
		mapi_object_init(&obj_message);
		const mapi_id_t *pfid;
		const mapi_id_t	*pmid;

		pfid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_FID);
		pmid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_MID);		
		retval = OpenMessage(&obj_folder, 
				     SRowSet.aRow[i].lpProps[0].value.d,
				     SRowSet.aRow[i].lpProps[1].value.d,
				     &obj_message, 0);
		//FIXME: Verify this
		//printf(" %016llx %016llx %016llx %016llx %016llx\n", *pfid, *pmid, SRowSet.aRow[i].lpProps[0].value.d, SRowSet.aRow[i].lpProps[1].value.d, fid);
		if (retval != MAPI_E_NOT_FOUND) {
			retval = GetPropsAll(&obj_message, &properties_array);
			if (retval == MAPI_E_SUCCESS) {

				mapi_SPropValue_array_named(&obj_message, 
							    &properties_array);
				if (!cb (&properties_array, *pfid, *pmid, data)) {
					printf("Breaking from fetching items\n");
					break;
				}

				mapi_object_release(&obj_message);
			}
		}
	}

	mapi_object_release(&obj_table);
	mapi_object_release(&obj_folder);

	UNLOCK ();
	return TRUE;
}
gpointer
exchange_mapi_connection_fetch_item (uint32_t olFolder, mapi_id_t fid, mapi_id_t mid, FetchItemCallback cb)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;
	struct mapi_SPropValue_array properties_array;
	TALLOC_CTX *mem_ctx;
	int i;
	gpointer retobj = NULL;


	LOCK ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return NULL;
	}

	printf("Opening folder %llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return NULL;
	}
	mapi_object_init(&obj_message);
	retval = OpenMessage(&obj_folder,
			     fid,
			     mid,
			     &obj_message, 0);
	if (retval != MAPI_E_NOT_FOUND) {
		retval = GetPropsAll(&obj_message, &properties_array);
		if (retval == MAPI_E_SUCCESS) {
			
			mapi_SPropValue_array_named(&obj_message, 
						    &properties_array);
			retobj = cb (&properties_array, fid, mid);

			mapi_object_release(&obj_message);
		}
	}

	mapi_object_release(&obj_folder);
	

	UNLOCK ();
	
	return retobj;
}

struct folder_data {
	mapi_id_t id;
};

gboolean
exchange_mapi_remove_items (uint32_t olFolder, mapi_id_t fid, GSList *mids)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;
	struct mapi_SPropValue_array properties_array;
	TALLOC_CTX *mem_ctx;
	uint32_t i=0;
	gpointer retobj = NULL;
	mapi_id_t *id_messages;
	GSList *tmp = mids;

	LOCK ();

	LOGALL ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_message);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	printf("Opening folder %016llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	id_messages = talloc_array(mem_ctx, uint64_t, g_slist_length (mids)+1);
	for (; tmp; tmp=tmp->next, i++) {
		struct folder_data *data = tmp->data;
		id_messages [i] = data->id;
	}

	retval = DeleteMessage(&obj_folder, id_messages, i);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items Delete Message %d %d failed: %d\n", i, g_slist_length(mids), retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
		UNLOCK ();
		return FALSE;
	}

	mapi_object_release(&obj_folder);
	
	LOGNONE();
	UNLOCK ();
	
	return TRUE;	
}

gboolean
exchange_mapi_remove_folder (uint32_t olFolder, mapi_id_t fid)
{
	mapi_object_t obj_store, obj_tstore;	
	mapi_object_t obj_folder;
	mapi_object_t obj_top;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;
	struct mapi_SPropValue_array properties_array;
	TALLOC_CTX *mem_ctx;
	uint32_t i=0;
	gpointer retobj = NULL;
	mapi_id_t id_top, pfid;
	ExchangeMAPIFolder *folder;
	
	LOCK ();

	LOGALL ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_tstore);	
	mapi_object_init(&obj_top);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove folder openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}
	
	printf("Opening folder %016llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}
	
	retval = EmptyFolder(&obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items emptyfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}

	folder = exchange_mapi_folder_get_folder (fid);
	if (!folder) {
		g_warning (" Unable to get the folder from the list\n");
		LOGNONE ();
		UNLOCK ();
		return FALSE;		
	}
	
	retval = OpenMsgStore(&obj_tstore);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove folder openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}
	
	printf("Opening folder %016llx\n", folder->parent_folder_id);
	/* We now open the folder */
	retval = OpenFolder(&obj_tstore, folder->parent_folder_id, &obj_top);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}

	
	retval = DeleteFolder(&obj_top, fid);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("remove items Deletefolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}
	
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	
	LOGNONE();
	UNLOCK ();
	
	return TRUE;	
}

mapi_id_t
exchange_mapi_create_item (uint32_t olFolder, mapi_id_t fid, BuildNameID build_name_id, gpointer ni_data, BuildProps build_props, gpointer p_data)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SPropValue	*props=NULL;
	struct mapi_nameid	*nameid;
	struct SPropTagArray	*SPropTagArray;
	TALLOC_CTX *mem_ctx;
	int propslen;
	mapi_id_t mid=0;
	
	LOCK ();

	LOGALL ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_message);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}

	printf("Opening folder %016llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}

	retval = CreateMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item CreateMessage failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}
	mapi_object_debug (&obj_message);
	nameid = mapi_nameid_new(mem_ctx);
	if (!build_name_id (nameid, ni_data)) {
		g_warning ("create item build name id failed: %d\n", retval);
		LOGNONE ();
		UNLOCK ();
		return 0;		
	}

	SPropTagArray = talloc_zero(mem_ctx, struct SPropTagArray);
	retval = GetIDsFromNames(&obj_folder, nameid->count,
				 nameid->nameid, 0, &SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item GetIDsFromNames failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}
	mapi_nameid_SPropTagArray(nameid, SPropTagArray);
	MAPIFreeBuffer(nameid);

	propslen = build_props (&props, SPropTagArray, p_data);
	if (propslen <1) {
		g_warning ("create item build props failed: %d\n", retval);
		LOGNONE ();
		UNLOCK ();
		return 0;		
	}

	retval = SetProps(&obj_message, props, propslen);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item SetProps failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;				
	}

	retval = SaveChangesMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create item SaveChangesMessage failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
		LOGNONE ();
		UNLOCK ();
		return 0;						
	}

	mid = mapi_object_get_id (&obj_message);
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_folder);
	
	LOGNONE ();
	UNLOCK ();
	return mid;
}

gboolean
exchange_mapi_modify_item (uint32_t olFolder, mapi_id_t fid, mapi_id_t mid, BuildNameID build_name_id, gpointer ni_data, BuildProps build_props, gpointer p_data)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SPropValue	*props=NULL;
	struct mapi_nameid	*nameid;
	struct SPropTagArray	*SPropTagArray;
	TALLOC_CTX *mem_ctx;
	int propslen;
	
	LOCK ();

	LOGALL ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_message);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify item openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}

	printf("Opening folder %016llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify item openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}


	retval = OpenMessage(&obj_folder,
			     fid,
			     mid,
			     &obj_message, MAPI_MODIFY);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify item open message failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return FALSE;
	}

	nameid = mapi_nameid_new(mem_ctx);
	if (!build_name_id (nameid, ni_data)) {
		g_warning ("modify item build name id failed: %d\n", retval);
		LOGNONE ();
		UNLOCK ();
		return 0;		
	}

	SPropTagArray = talloc_zero(mem_ctx, struct SPropTagArray);
	retval = GetIDsFromNames(&obj_folder, nameid->count,
				 nameid->nameid, 0, &SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify  item GetIDsFromNames failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}
	mapi_nameid_SPropTagArray(nameid, SPropTagArray);
	MAPIFreeBuffer(nameid);

	propslen = build_props (&props, SPropTagArray, p_data);
	if (propslen <1) {
		g_warning ("modify item build props failed: %d\n", retval);
		LOGNONE ();
		UNLOCK ();
		return 0;		
	}

	retval = SetProps(&obj_message, props, propslen);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify item SetProps failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;				
	}

	retval = SaveChangesMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("modify item SaveChangesMessage failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
		LOGNONE ();
		UNLOCK ();
		return 0;						
	}

	mapi_object_release(&obj_message);
	mapi_object_release(&obj_folder);
	
	LOGNONE ();
	UNLOCK ();
	return TRUE;
}

static char *utf8tolinux(TALLOC_CTX *mem_ctx, const char *wstring)
{
	char		*newstr;

	newstr = windows_to_utf8(mem_ctx, wstring);
	return newstr;
}


static const char *
get_container_class(TALLOC_CTX *mem_ctx, mapi_object_t *parent, mapi_id_t folder_id)
{
	enum MAPISTATUS		retval;
	mapi_object_t		obj_folder;
	struct SPropTagArray	*SPropTagArray;
	struct SPropValue	*lpProps;
	uint32_t		count;

	mapi_object_init(&obj_folder);
	retval = OpenFolder(parent, folder_id, &obj_folder);
	if (retval != MAPI_E_SUCCESS) return false;

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, PR_CONTAINER_CLASS);
	retval = GetProps(&obj_folder, SPropTagArray, &lpProps, &count);
	MAPIFreeBuffer(SPropTagArray);
	if ((lpProps[0].ulPropTag != PR_CONTAINER_CLASS) || (retval != MAPI_E_SUCCESS)) {
		errno = 0;
		return IPF_NOTE;
	}
	return lpProps[0].value.lpszA;
}



static gboolean
get_child_folders(TALLOC_CTX *mem_ctx, mapi_object_t *parent, const char *parent_name, mapi_id_t folder_id, int count, GSList **mapi_folders)
{
	enum MAPISTATUS		retval;
	bool			ret;
	mapi_object_t		obj_folder;
	mapi_object_t		obj_htable;
	struct SPropTagArray	*SPropTagArray;
	struct SRowSet		rowset;
	const char	       	*name;
	const char 		*class;
	char			*newname;
	const uint32_t		*child, *unread, *total;
	uint32_t		index;
	const uint64_t		*fid;
	int			i;

	mapi_object_init(&obj_folder);
	retval = OpenFolder(parent, folder_id, &obj_folder);
	if (retval != MAPI_E_SUCCESS) 
		return FALSE;

	mapi_object_init(&obj_htable);
	retval = GetHierarchyTable(&obj_folder, &obj_htable);
	if (retval != MAPI_E_SUCCESS) 
		return FALSE;

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x5,
					  PR_DISPLAY_NAME,
					  PR_FID,
					  PR_CONTENT_UNREAD,
					  PR_CONTENT_COUNT,
					  PR_FOLDER_CHILD_COUNT);
	retval = SetColumns(&obj_htable, SPropTagArray);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS)
		return FALSE;
	
	while ((retval = QueryRows(&obj_htable, 0x32, TBL_ADVANCE, &rowset) != MAPI_E_NOT_FOUND) && rowset.cRows) {
		for (index = 0; index < rowset.cRows; index++) {
			ExchangeMAPIFolder *folder;
			fid = (const uint64_t *)find_SPropValue_data(&rowset.aRow[index], PR_FID);
			name = (const char *)find_SPropValue_data(&rowset.aRow[index], PR_DISPLAY_NAME);
			unread = (const uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_CONTENT_UNREAD);
			total = (const uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_CONTENT_COUNT);
			child = (const uint32_t *)find_SPropValue_data(&rowset.aRow[index], PR_FOLDER_CHILD_COUNT);
			class = get_container_class(mem_ctx, parent, *fid);
			newname = utf8tolinux(mem_ctx, name);
			printf("|---+ %-15s : (Container class: %s %016llx) UnRead : %d Total : %d\n", newname, class, *fid, *unread, *total);
			folder = exchange_mapi_folder_new (newname, parent_name, class, *fid, folder_id, *child, *unread, *total);
			*mapi_folders = g_slist_prepend (*mapi_folders, folder);
			if (*child)
				get_child_folders(mem_ctx, &obj_folder, newname, *fid, count + 1, mapi_folders);
			MAPIFreeBuffer(newname);

			
		}
	}
	return FALSE;
}

gboolean 
exchange_mapi_get_folders_list (GSList **mapi_folders)
{
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	enum MAPISTATUS			retval;
	mapi_id_t			id_mailbox;
	struct SPropTagArray		*SPropTagArray;
	struct SPropValue		*lpProps = NULL;
	uint32_t			cValues;
	const char			*mailbox_name;
	char				*utf8_mailbox_name;
	ExchangeMAPIFolder *folder;

	LOCK ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_store);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		UNLOCK ();
		return FALSE;
	}

	/* Retrieve the mailbox folder name */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, PR_DISPLAY_NAME);
	retval = GetProps(&obj_store, SPropTagArray, &lpProps, &cValues);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		UNLOCK ();
		return FALSE;
	}

	if (lpProps[0].value.lpszA) {
		mailbox_name = lpProps[0].value.lpszA;
	} else {
		UNLOCK ();
		return FALSE;
	}	

	/* Prepare the directory listing */
	retval = GetDefaultFolder(&obj_store, &id_mailbox, olFolderTopInformationStore);
	//	retval = GetDefaultFolder(&obj_store, &id_mailbox, olFolderInbox);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}
	utf8_mailbox_name = utf8tolinux(mem_ctx, mailbox_name);

	/* FIXME: May have to get the child folders count? Do we need/use it? */
	folder = exchange_mapi_folder_new (utf8_mailbox_name, NULL, IPF_NOTE, id_mailbox, 0, 0, 0 ,0); 

	*mapi_folders = g_slist_prepend (*mapi_folders, folder);
	get_child_folders (mem_ctx, &obj_store, utf8_mailbox_name, id_mailbox, 0, mapi_folders);

	MAPIFreeBuffer(utf8_mailbox_name);

	UNLOCK ();
	return TRUE;

}


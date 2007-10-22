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

#define DEFAULT_PROF_PATH ".evolution/mapi-profiles.ldb"
#define d(x) x

static struct mapi_session *global_mapi_session= NULL;
static GStaticRecMutex connect_lock = G_STATIC_REC_MUTEX_INIT;


#define LOCK()		printf("%s(%d):%s: lock \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);;g_static_rec_mutex_lock(&connect_lock)
#define UNLOCK()	printf("%s(%d):%s: unlock \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_unlock(&connect_lock)

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
		UNLOCK ();
		return FALSE;
	}

	printf("Opening folder %llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openfolder failed: %d\n", retval);
		UNLOCK ();
		return FALSE;
	}

	mapi_object_init(&obj_table);
	retval = GetContentsTable(&obj_folder, &obj_table);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-getcontentstable failed: %d\n", retval);
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
		UNLOCK ();
		return FALSE;
	}

	if (res) {
		/* Applying any restriction that are set. */
		retval = Restrict(&obj_table, res);
		if (retval != MAPI_E_SUCCESS) {
			g_warning ("Failed while setting restrictions\n");
			UNLOCK ();
			return FALSE;
		}
	}
	retval = QueryRows(&obj_table, 0x32, TBL_ADVANCE, &SRowSet);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-queryrows failed: %d\n", retval);
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
		printf(" %016llx %016llx %016llx %016llx %016llx\n", *pfid, *pmid, SRowSet.aRow[i].lpProps[0].value.d, SRowSet.aRow[i].lpProps[1].value.d, fid);
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
		UNLOCK ();
		return NULL;
	}

	printf("Opening folder %llx\n", fid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("startbookview-openfolder failed: %d\n", retval);
		UNLOCK ();
		return NULL;
	}

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



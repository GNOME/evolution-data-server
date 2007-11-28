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

#include <camel/camel-data-wrapper.h>
#include <camel/camel-exception.h>
#include <camel/camel-mime-filter-crlf.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-stream-mem.h>

#define CN_MSG_PROPS 2
#define	STREAM_SIZE	0x4000
#define DEFAULT_PROF_PATH ".evolution/mapi-profiles.ldb"
#define d(x) x

static struct mapi_session *global_mapi_session= NULL;
static GStaticRecMutex connect_lock = G_STATIC_REC_MUTEX_INIT;

#define LOCK()		printf("%s(%d):%s: lock(connect_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);;g_static_rec_mutex_lock(&connect_lock)
#define UNLOCK()	printf("%s(%d):%s: unlock(connect_lock) \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_unlock(&connect_lock)
#define LOGALL() 	lp_set_cmdline(global_loadparm, "log level", "10"); global_mapi_ctx->dumpdata = TRUE;
#define LOGNONE()       lp_set_cmdline(global_loadparm, "log level", "0"); global_mapi_ctx->dumpdata = FALSE;
//#define ENABLE_VERBOSE_LOG() 	global_mapi_ctx->dumpdata = TRUE;
#define ENABLE_VERBOSE_LOG()

/* Specifies READ/WRITE sizes to be used while handling attachment streams */
#define ATTACH_MAX_READ_SIZE  0x1000
#define ATTACH_MAX_WRITE_SIZE 0x1000

static struct mapi_session *
mapi_profile_load(const char *profname, const char *password)
{
	enum MAPISTATUS	retval;
	enum MAPISTATUS status;
	gchar *profpath = NULL;
	struct mapi_session *session = NULL;
	char *profile = profname;

	d(printf("Loading profile with %s\n", profname));
	
	profpath = g_build_filename (g_getenv("HOME"), DEFAULT_PROF_PATH, NULL);
	if (!g_file_test (profpath, G_FILE_TEST_EXISTS)) {
		g_warning ("Mapi profile database @ %s not found\n", profpath);
		g_free (profpath);
		return NULL;
	}

	MAPIUninitialize ();

	if (MAPIInitialize(profpath) != MAPI_E_SUCCESS){
		g_free(profpath);
		status = GetLastError();
		if (status == MAPI_E_SESSION_LIMIT){
			d(printf("%s(%d):%s:Already connected \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));
			mapi_errstr("MAPIInitialize", GetLastError());
			return NULL;
		}
		else {
			g_warning ("mapi_profile_load : Generic error : %d\n", status);
			return NULL;
		}
	}

	g_free (profpath);

	ENABLE_VERBOSE_LOG ();
	if (!profile) {
		if ((retval = GetDefaultProfile(&profile)) != MAPI_E_SUCCESS) {
			mapi_errstr("GetDefaultProfile", GetLastError());
			MAPIUninitialize ();
			return NULL;
		}

	}
	if (MapiLogonEx(&session, profile, password) != MAPI_E_SUCCESS){
		retval = GetLastError();
		mapi_errstr("MapiLogonEx ", retval);
		g_warning ("mapi_profile_load failed.\n");

		MAPIUninitialize ();
		return NULL;
	}
	
	return session;
}

gboolean 
exchange_mapi_connection_new (const char *profile, const char *password)
{
	LOCK ();
	if (global_mapi_session) {
		d(printf("Already logged\n"));
		UNLOCK ();
		return TRUE;
	}

	global_mapi_session = mapi_profile_load (profile, password);

	UNLOCK ();

	if (!global_mapi_session) {
		g_warning ("Login failed\n");
		return FALSE;
	}
	
	d(printf("%s(%d):%s:Connected \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));
	return TRUE;
}

gboolean
exchange_mapi_connection_exists ()
{
	return global_mapi_session != NULL;
}

void
exchange_mapi_connection_close ()
{
	global_mapi_session = NULL;
	MAPIUninitialize ();	
}

/* Returns TRUE if all attachments were written succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_set_attachments (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message, GSList *attach_list)
{
	const uint32_t 	cn_props_attach = 4;
	GSList 		*l;
	enum MAPISTATUS	retval;
	gboolean 	status = TRUE;

	for (l = attach_list; l; l = l->next) {
		ExchangeMAPIAttachment 	*attachment = (ExchangeMAPIAttachment *) (l->data);
		int32_t 		flag;
		uint32_t 		total_written;
		gboolean 		done = FALSE;
		struct SPropValue 	*props_attach;
		mapi_object_t		obj_attach;
		mapi_object_t		obj_stream;

		props_attach = g_new (struct SPropValue, cn_props_attach);
		mapi_object_init(&obj_attach);
		mapi_object_init(&obj_stream);

		/* CreateAttach */
		retval = CreateAttach(obj_message, &obj_attach);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("CreateAttach", GetLastError());
			goto cleanup;
		}

		flag = ATTACH_BY_VALUE; 
		set_SPropValue_proptag(&props_attach[0], PR_ATTACH_METHOD, (const void *) (&flag));

		/* MSDN Documentation: When the supplied offset is -1 (0xFFFFFFFF), the 
		 * attachment is not rendered using the PR_RENDERING_POSITION property. 
		 * All values other than -1 indicate the position within PR_BODY at which 
		 * the attachment is to be rendered. 
		 */
		flag = -1;
		set_SPropValue_proptag(&props_attach[1], PR_RENDERING_POSITION, (const void *) (&flag));

		set_SPropValue_proptag(&props_attach[2], PR_ATTACH_FILENAME, (const void *) attachment->filename);
		set_SPropValue_proptag(&props_attach[3], PR_ATTACH_LONG_FILENAME, (const void *) attachment->filename);

		/* SetProps */
		retval = SetProps(&obj_attach, props_attach, cn_props_attach);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("SetProps", GetLastError());
			goto cleanup;
		}

		/* OpenStream on CreateAttach handle */
		retval = OpenStream(&obj_attach, PR_ATTACH_DATA_BIN, 2, &obj_stream);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenStream", GetLastError());
			goto cleanup;
		}

		total_written = 0;
		/* Write attachment */
		while (!done) {
			uint16_t 	cn_written = 0;
			DATA_BLOB 	blob;

			blob.length = (attachment->value->len - total_written) < ATTACH_MAX_WRITE_SIZE ? 
					(attachment->value->len - total_written) : ATTACH_MAX_WRITE_SIZE;
			blob.data = (attachment->value->data) + total_written;

			retval = WriteStream(&obj_stream,
					     &blob,
					     &cn_written);

			if ((retval != MAPI_E_SUCCESS) || (cn_written == 0)) {
				mapi_errstr("WriteStream", GetLastError());
				done = TRUE;
			} else {
				total_written += cn_written;
				if (total_written >= attachment->value->len)
					done = TRUE;
			}
		}

		/* message->SaveChanges() */
		retval = SaveChanges(obj_message, &obj_attach, KEEP_OPEN_READWRITE);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("SaveChanges", GetLastError());
//			goto cleanup;
		}

	cleanup:
		if (retval != MAPI_E_SUCCESS) 
			status = FALSE;
		mapi_object_release(&obj_stream);
		mapi_object_release(&obj_attach);
		g_free (props_attach);
	}

	return status;
}

/* Returns TRUE if all attachments were read succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_get_attachments (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message, GSList **attach_list)
{
	enum MAPISTATUS		retval;
	mapi_object_t 		obj_tb_attach;
	struct SPropTagArray	*proptags;
	struct SRowSet		rows_attach;
	uint32_t		attach_count;
	uint32_t		i_row_attach;
	gboolean 		status = TRUE;

	/* do we need MIME tag, MIME sequence etc ? */
	proptags = set_SPropTagArray(mem_ctx, 0x7, 
				     PR_ATTACH_NUM, 
				     PR_INSTANCE_KEY, 
				     PR_RECORD_KEY, 
				     PR_RENDERING_POSITION,
				     PR_ATTACH_FILENAME, 
				     PR_ATTACH_LONG_FILENAME,  
				     PR_ATTACH_SIZE);

	mapi_object_init(&obj_tb_attach);

	/* open attachment table */
	retval = GetAttachmentTable(obj_message, &obj_tb_attach);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetAttachmentTable", GetLastError());
		goto cleanup;
	}

	retval = SetColumns(&obj_tb_attach, proptags);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SetColumns", GetLastError());
		goto cleanup;
	}

	retval = GetRowCount(&obj_tb_attach, &attach_count);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetRowCount", GetLastError());
		goto cleanup;
	}

	retval = QueryRows(&obj_tb_attach, attach_count, TBL_ADVANCE, &rows_attach);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("QueryRows", GetLastError());
		goto cleanup;
	}

	/* foreach attachment, open by PR_ATTACH_NUM */
	for (i_row_attach = 0; i_row_attach < rows_attach.cRows; i_row_attach++) {
		const uint32_t	*num_attach;
		mapi_object_t	obj_attach;
		mapi_object_t 	obj_stream;
		uint32_t 	cn_read = 0;
		uint32_t 	off_data = 0;
		gboolean 	done = FALSE;
		uint8_t 	*buf_data = NULL;
		const uint32_t 	*sz_data;

		mapi_object_init(&obj_attach);
		mapi_object_init(&obj_stream);

		num_attach = (const uint32_t *) get_SPropValue_SRow_data(&rows_attach.aRow[i_row_attach], PR_ATTACH_NUM);

		retval = OpenAttach(obj_message, *num_attach, &obj_attach);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenAttach", GetLastError());
			goto loop_cleanup;
		}

		/* get a stream on PR_ATTACH_DATA_BIN */
		retval = OpenStream(&obj_attach, PR_ATTACH_DATA_BIN, 0, &obj_stream);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenStream", GetLastError());
			goto loop_cleanup;
		}

		/* Alloc buffer */
		sz_data = (const uint32_t *) get_SPropValue_SRow_data(&rows_attach.aRow[i_row_attach], PR_ATTACH_SIZE);
		buf_data = talloc_size(mem_ctx, *sz_data);
		if (buf_data == 0)
			goto loop_cleanup;

		/* Read attachment from stream */
		while (!done) {
			retval = ReadStream(&obj_stream,
					    (buf_data) + off_data,
					    ATTACH_MAX_READ_SIZE,
					    &cn_read);
			if ((retval != MAPI_E_SUCCESS) || (cn_read == 0)) {
				mapi_errstr("ReadStream", GetLastError());
				done = TRUE;
			} else {
				off_data += cn_read;
				if (off_data >= *sz_data)
					done = TRUE;
			}
		}

		/* FIXME: should we utf8tolinux (mem_ctx, buf_data) ??*/

		if (retval == MAPI_E_SUCCESS) {
			ExchangeMAPIAttachment 	*attachment = g_new0 (ExchangeMAPIAttachment, 1);

			attachment->value = g_byte_array_sized_new (off_data);
			attachment->value = g_byte_array_append (attachment->value, buf_data, off_data);

			attachment->filename = (const char *) get_SPropValue_SRow_data(&rows_attach.aRow[i_row_attach], PR_ATTACH_LONG_FILENAME);
			if (!(attachment->filename && *attachment->filename))
				attachment->filename = (const char *) get_SPropValue_SRow_data(&rows_attach.aRow[i_row_attach], PR_ATTACH_FILENAME);

			*attach_list = g_slist_append (*attach_list, attachment);
		}
		talloc_free (buf_data);

	loop_cleanup:
		if (retval != MAPI_E_SUCCESS)
			status = FALSE;
		mapi_object_release(&obj_stream);
		mapi_object_release(&obj_attach);
	}

cleanup:
	if (retval != MAPI_E_SUCCESS)
		status = FALSE;
	mapi_object_release(&obj_tb_attach);
	MAPIFreeBuffer(proptags);

	return status;
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

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x9,
					  PR_FID,
					  PR_MID,
					  PR_INST_ID,
					  PR_INSTANCE_NUM,
					  PR_SUBJECT,
					  PR_MESSAGE_CLASS,
					  PR_HASATTACH,
					/* FIXME: is this tag fit to check if a recipient table exists or not ? */
//					  PR_DISCLOSURE_OF_RECIPIENTS,
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
		const bool *has_attach = NULL;
		const bool *disclose_recipients = NULL;
		GSList *attach_list = NULL;
		GSList *recip_list = NULL;

		pfid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_FID);
		pmid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_MID);

		has_attach = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_HASATTACH);
//		disclose_recipients = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_DISCLOSURE_OF_RECIPIENTS);

		retval = OpenMessage(&obj_folder, *pfid, *pmid, &obj_message, 0);

		if (has_attach && *has_attach) {
			exchange_mapi_util_get_attachments (mem_ctx, &obj_message, &attach_list);
		}

		if (disclose_recipients && *disclose_recipients) {
		}

		//FIXME: Verify this
		//printf(" %016llx %016llx %016llx %016llx %016llx\n", *pfid, *pmid, SRowSet.aRow[i].lpProps[0].value.d, SRowSet.aRow[i].lpProps[1].value.d, fid);
		if (retval != MAPI_E_NOT_FOUND) {
			retval = GetPropsAll(&obj_message, &properties_array);
			if (retval == MAPI_E_SUCCESS) {

				mapi_SPropValue_array_named(&obj_message, 
							    &properties_array);
				if (!cb (&properties_array, *pfid, *pmid, recip_list, attach_list, data)) {
					printf("Breaking from fetching items\n");
					break;
				}

				mapi_object_release(&obj_message);
			}
		}
		/* should I ?? */
		if (attach_list) {
			GSList *l;
			for (l = attach_list; l; l = l->next) {
				ExchangeMAPIAttachment *attachment = (ExchangeMAPIAttachment *) (l->data);
				g_byte_array_free (attachment->value, TRUE);
				attachment->value = NULL;
			}
			g_slist_free (attach_list);
			attach_list = NULL;
		}

		if (recip_list) {
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
exchange_mapi_create_folder (uint32_t olFolder, mapi_id_t pfid, char *name)
{
	mapi_object_t obj_store;
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
	mapi_id_t fid;
	ExchangeMAPIFolder *folder;
	struct SPropValue vals[1];
	const char *type;

	LOCK ();

	LOGALL ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_top);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("createfolder openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}

	printf("Opening folder %016llx\n", pfid);
	/* We now open the folder */
	retval = OpenFolder(&obj_store, pfid, &obj_top);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create folder openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}
	
	retval = CreateFolder(&obj_top, name, "Created using Evolution/libmapi", &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create folder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}

	fid = mapi_object_get_id (&obj_folder);
	switch (olFolder) {
	case olFolderInbox:
		type = IPF_NOTE;
		break;
	case olFolderCalendar:
		type = IPF_APPOINTMENT;
		break;
	case olFolderContacts:
		type = IPF_CONTACT;
		break;
	case olFolderTasks:
		type = IPF_TASK;
		break;
	case olFolderNotes:
		type = IPF_STICKYNOTE;
		break;
	default:
		type = IPF_NOTE;
	}

	vals[0].value.lpszA = type;
	vals[0].ulPropTag = PR_CONTAINER_CLASS;
	retval = SetProps(&obj_folder, vals, 1);

	if (retval != MAPI_E_SUCCESS) {
		g_warning ("create folder set props failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		LOGNONE ();
		UNLOCK ();
		return 0;
	}

	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	
	LOGNONE();
	UNLOCK ();
	
	printf("Got %016llx\n", fid);
	return fid;	
}


mapi_id_t
exchange_mapi_create_item (uint32_t olFolder, mapi_id_t fid, BuildNameID build_name_id, gpointer ni_data, BuildProps build_props, gpointer p_data, GSList *recipients, GSList *attachments)
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

	if (fid == 0 ){ /*fid not present then we'll use olFolder. Document this in API doc.*/
		retval = GetDefaultFolder(&obj_store, &fid, olFolder);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("GetDefaultFolder", GetLastError());				
			LOGNONE ();
			UNLOCK ();
			return 0;
		}
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

	if (attachments) {
		exchange_mapi_util_set_attachments (mem_ctx, &obj_message, attachments);
	}

	if (recipients) {
		//exchange_mapi_util_set_attachments (mem_ctx, &obj_message, attachments);
	}

	mapi_object_debug (&obj_message);

	if (build_name_id) {
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
	}

	if (build_props) {
		propslen = build_props (&props, SPropTagArray, p_data);

		if (propslen <1) {
			g_warning ("create item build props failed: %d\n", retval);
			LOGNONE ();
			UNLOCK ();
			return 0;
		}

		retval = SetProps(&obj_message, props, propslen);

		/* FixME */ 
		//		MAPIFreeBuffer(SPropTagArray);
		if (retval != MAPI_E_SUCCESS) {
			g_warning ("create item SetProps failed: %d\n", retval);
			mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
			LOGNONE ();
			UNLOCK ();
			return 0;
		}

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

	*mapi_folders = g_slist_reverse (*mapi_folders);
	return TRUE;

}

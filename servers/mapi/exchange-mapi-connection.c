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
#include "exchange-mapi-utils.h"
#include <param.h>

#define CN_MSG_PROPS 2
#define	STREAM_SIZE	0x4000
#define DEFAULT_PROF_PATH ".evolution/mapi-profiles.ldb"
#define d(x) x
#define DATATEST 1

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

/* Specifies READ/WRITE sizes to be used while handling normal streams (struct SBinary_short) */
#define STREAM_MAX_READ_SIZE  0x1000
#define STREAM_MAX_WRITE_SIZE 0x1000

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

static gboolean 
exchange_mapi_util_read_generic_stream (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message, uint32_t proptag, GSList **stream_list)
{
	enum MAPISTATUS			retval;
	DATA_BLOB 			body;
	struct SPropTagArray 		*SPropTagArray;
	struct SPropValue 		*lpProps;
	uint32_t			count;
	struct SRow			aRow;
	const struct SBinary_short 	*bin = NULL;

	/* sanity */
	g_return_val_if_fail (obj_message, FALSE);
	g_return_val_if_fail (((proptag & 0xFFFF) == PT_BINARY), FALSE);

	/* if compressed RTF stream, then return */
	g_return_val_if_fail (proptag != PR_RTF_COMPRESSED, FALSE);

	/* initialize body DATA_BLOB */
	body.length = 0;
	body.data = talloc_zero(mem_ctx, uint8_t);

	/* Build the array of properties we want to fetch */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, proptag);
	retval = GetProps(obj_message, SPropTagArray, &lpProps, &count);
	MAPIFreeBuffer(SPropTagArray);

	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetProps", GetLastError());
		return FALSE;
	}

	/* Build a SRow structure */
	aRow.ulAdrEntryPad = 0;
	aRow.cValues = count;
	aRow.lpProps = lpProps;

	bin = (const struct SBinary_short *) find_SPropValue_data(&aRow, proptag);
	if (bin && bin->lpb) {
		body.data = talloc_memdup(mem_ctx, bin->lpb, bin->cb);
		body.length = bin->cb;
	} else {
		mapi_object_t 	obj_stream;
		uint32_t 	cn_read;
		uint8_t		buf[STREAM_MAX_READ_SIZE];

		mapi_object_init(&obj_stream);

		/* get a stream on specified proptag */
		retval = OpenStream(obj_message, proptag, 0, &obj_stream);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenStream", GetLastError());
			goto cleanup;
		}

		/* read from the stream */
		do {
			retval = ReadStream(&obj_stream, buf, STREAM_MAX_READ_SIZE, &cn_read);
			if (retval != MAPI_E_SUCCESS) {
				cn_read = 0;
				mapi_errstr("ReadStream", GetLastError());
			} else if (cn_read) {
				body.data = talloc_realloc(mem_ctx, body.data, uint8_t,
							    body.length + cn_read);
				memcpy(&(body.data[body.length]), buf, cn_read);
				body.length += cn_read;
			}
		} while (cn_read);
	cleanup: 
		mapi_object_release(&obj_stream);
	}

	if (retval == MAPI_E_SUCCESS && body.length) {
		ExchangeMAPIStream 	*stream = g_new0 (ExchangeMAPIStream, 1);

		stream->value = g_byte_array_sized_new (body.length);
		stream->value = g_byte_array_append (stream->value, body.data, body.length);

		stream->proptag = proptag;

		*stream_list = g_slist_append (*stream_list, stream);
	}

	if (body.length)
		talloc_free (body.data);

	return (retval == MAPI_E_SUCCESS);
}

/*
 * Fetch the body given PR_MSG_EDITOR_FORMAT property value
 */
static gboolean
exchange_mapi_util_read_body_stream (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message, GSList **stream_list)
{
	enum MAPISTATUS			retval;
	struct SPropTagArray		*SPropTagArray;
	struct SPropValue		*lpProps;
	struct SRow			aRow;
	uint32_t			count;
	/* common email fields */
	DATA_BLOB			body;
	const uint32_t			*editor;
	mapi_object_t			obj_stream;
	const char			*data = NULL;
	const bool 			*rtf_in_sync;
	uint32_t 			dflt;
	uint32_t 			proptag = 0;

	/* sanity check */
	g_return_val_if_fail (obj_message, FALSE);

	/* Build the array of properties we want to fetch */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x8,
					  PR_MSG_EDITOR_FORMAT,
					  PR_BODY,
					  PR_BODY_UNICODE,
					  PR_BODY_HTML, 
					  PR_BODY_HTML_UNICODE, 
					  PR_HTML,
					  PR_RTF_COMPRESSED,
					  PR_RTF_IN_SYNC);

	lpProps = talloc_zero(mem_ctx, struct SPropValue);
	retval = GetProps(obj_message, SPropTagArray, &lpProps, &count);
	MAPIFreeBuffer(SPropTagArray);

	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetProps", GetLastError());
		return FALSE;
	}

	/* Build a SRow structure */
	aRow.ulAdrEntryPad = 0;
	aRow.cValues = count;
	aRow.lpProps = lpProps;

	editor = (const uint32_t *) find_SPropValue_data(&aRow, PR_MSG_EDITOR_FORMAT);
	/* if PR_MSG_EDITOR_FORMAT doesn't exist, set it to PLAINTEXT */
	if (!editor) {
		dflt = EDITOR_FORMAT_PLAINTEXT;
		editor = &dflt;
	}

	/* initialize body DATA_BLOB */
	body.data = NULL;
	body.length = 0;

	retval = -1;
	switch (*editor) {
		case EDITOR_FORMAT_PLAINTEXT:
			if ((data = (const char *) find_SPropValue_data (&aRow, PR_BODY)) != NULL)
				proptag = PR_BODY;
			else if ((data = (const char *) find_SPropValue_data (&aRow, PR_BODY_UNICODE)) != NULL)
				proptag = PR_BODY_UNICODE;
			if (data) {
				body.data = talloc_memdup(mem_ctx, data, strlen(data));
				body.length = strlen(data);
				retval = MAPI_E_SUCCESS;
			} 
			break;
		case EDITOR_FORMAT_HTML: 
			if ((data = (const char *) find_SPropValue_data (&aRow, PR_BODY_HTML)) != NULL)
				proptag = PR_BODY_HTML;
			else if ((data = (const char *) find_SPropValue_data (&aRow, PR_BODY_HTML_UNICODE)) != NULL)
				proptag = PR_BODY_HTML_UNICODE;
			if (data) {
				body.data = talloc_memdup(mem_ctx, data, strlen(data));
				body.length = strlen(data);
				retval = MAPI_E_SUCCESS;
			} else if (exchange_mapi_util_read_generic_stream (mem_ctx, obj_message, PR_HTML, stream_list)) {
				retval = MAPI_E_SUCCESS;
			}
			break;
		case EDITOR_FORMAT_RTF: 
			rtf_in_sync = (const bool *)find_SPropValue_data (&aRow, PR_RTF_IN_SYNC);
//			if (!(rtf_in_sync && *rtf_in_sync)) {
				mapi_object_init(&obj_stream);

				retval = OpenStream(obj_message, PR_RTF_COMPRESSED, 0, &obj_stream);
				if (retval != MAPI_E_SUCCESS) {
					mapi_errstr("OpenStream", GetLastError());
					mapi_object_release(&obj_stream);
					break;
				}

				retval = WrapCompressedRTFStream(&obj_stream, &body);
				if (retval != MAPI_E_SUCCESS)
					mapi_errstr("WrapCompressedRTFStream", GetLastError());

				proptag = PR_RTF_COMPRESSED;

				mapi_object_release(&obj_stream);
//			}
			break;
		default: 
			break;
	}

	if (retval == MAPI_E_SUCCESS && proptag) {
		ExchangeMAPIStream 	*stream = g_new0 (ExchangeMAPIStream, 1);

		stream->value = g_byte_array_sized_new (body.length);
		stream->value = g_byte_array_append (stream->value, body.data, body.length);

		stream->proptag = proptag;

		*stream_list = g_slist_append (*stream_list, stream);
	}

	if (body.length) 
		talloc_free (body.data);

	return (retval == MAPI_E_SUCCESS);
}

static gboolean
exchange_mapi_util_delete_attachments (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message)
{
	/* FIXME: write the code */
	return TRUE;
}

/* Returns TRUE if all attachments were written succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_set_attachments (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message, GSList *attach_list, gboolean remove_existing)
{
	const uint32_t 	cn_props_attach = 4;
	GSList 		*l;
	enum MAPISTATUS	retval;
	gboolean 	status = TRUE;

	if (remove_existing)
		exchange_mapi_util_delete_attachments (mem_ctx, obj_message);

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

struct SPropTagArray *
exchange_mapi_util_SPropTagArray_new (uint32_t prop_nb, ...)
{
	struct SPropTagArray	*SPropTag;
	va_list			ap;
	uint32_t		i;
	uint32_t		*aulPropTag;

	printf("%s(%d):%s:prop_nb : %d \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, prop_nb);
	aulPropTag = g_new0(uint32_t, prop_nb);

	va_start(ap, prop_nb);
	for (i = 0; i < prop_nb; i++) {
		aulPropTag[i] = va_arg(ap, int);
		printf("%s(%d):%s:aulPropTag[i] = %d \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, aulPropTag[i]);
	}
	va_end(ap);

	SPropTag = g_new0 (struct SPropTagArray, 1);
	SPropTag->aulPropTag = aulPropTag;
	SPropTag->cValues = prop_nb;

	return SPropTag;
}

void 
exchange_mapi_util_SPropTagArray_free (struct SPropTagArray * SPropTagArray)
{
	g_return_if_fail (SPropTagArray != NULL);

	g_free (SPropTagArray->aulPropTag);
	g_free (SPropTagArray);
}

// FIXME: May be we need to support Restrictions/Filters here. May be after libmapi-0.7.

gboolean
exchange_mapi_connection_fetch_items (mapi_id_t fid, struct SPropTagArray *GetPropsTagArray, struct mapi_SRestriction *res,
				      BuildPropTagArray bpta_cb, FetchItemsCallback cb,  gpointer data)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	uint32_t count;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;

	TALLOC_CTX *mem_ctx;
	int i;

	printf("Fetching folder %016llX\n", fid);
	
	LOCK ();
	mem_ctx = talloc_init("Evolution");
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_store);
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("fetch items-openmsgstore failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());
		UNLOCK ();
		return FALSE;
	}

	printf("Opening folder %016llX\n", fid);
	/* We now open the folder */
	OpenFolder(&obj_store, fid, &obj_folder);
	retval = GetLastError();
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("fetch items-openfolder failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());		
		UNLOCK ();
		return FALSE;
	}

	mapi_object_init(&obj_table);
	GetContentsTable(&obj_folder, &obj_table);
	retval = GetLastError();
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("fetch items-getcontentstable failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	if (!bpta_cb) {
		SPropTagArray = set_SPropTagArray(mem_ctx, 0x9,
						  PR_FID,
						  PR_MID,
						  PR_INST_ID,
						  PR_INSTANCE_NUM,
						  PR_SUBJECT,
						  PR_MESSAGE_CLASS,
						  PR_HASATTACH,
						  /* FIXME: is this tag fit to check if a recipient table exists or not ? */
//					          PR_DISCLOSURE_OF_RECIPIENTS,
						  PR_RULE_MSG_PROVIDER,
						  PR_RULE_MSG_NAME);
	} else {
		SPropTagArray = bpta_cb (mem_ctx);
	}
	
	retval = SetColumns(&obj_table, SPropTagArray);
	MAPIFreeBuffer(SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("fetch items-setcolumns failed: %d\n", retval);
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
		g_warning ("fetch items-getrowcount failed: %d\n", retval);
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());
		UNLOCK ();
		return FALSE;
	}

	retval = QueryRows(&obj_table, count, TBL_ADVANCE, &SRowSet);
	if (retval != MAPI_E_SUCCESS) {
		g_warning ("fetch items-queryrows failed: %d\n", retval);
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
		GSList *stream_list = NULL;

		struct SPropTagArray *SPropTagArray1;
		uint32_t mycount;

		struct mapi_SPropValue_array properties_array;
		uint32_t prop_count;

		pfid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_FID);
		pmid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_MID);

		has_attach = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_HASATTACH);
		/* disclose_recipients = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_DISCLOSURE_OF_RECIPIENTS); */

		retval = OpenMessage(&obj_folder, *pfid, *pmid, &obj_message, 0);
		retval = GetLastError ();

		//FIXME: Verify this
		//printf(" %016llX %016llX %016llX %016llX %016llX\n", *pfid, *pmid, SRowSet.aRow[i].lpProps[0].value.d, SRowSet.aRow[i].lpProps[1].value.d, fid);
		if (retval == MAPI_E_SUCCESS) {

			struct SPropValue *lpProps;
			if (has_attach && *has_attach)
				exchange_mapi_util_get_attachments (mem_ctx, &obj_message, &attach_list);

			if (disclose_recipients && *disclose_recipients) {
				//TODO : RecipientTable handling. 
			}

			/* get the main body stream no matter what */
			exchange_mapi_util_read_body_stream (mem_ctx, &obj_message, &stream_list);

			if (GetPropsTagArray) {
				int i=0;

				lpProps = talloc_zero(mem_ctx, struct SPropValue);

				retval = GetProps (&obj_message, GetPropsTagArray, &lpProps, &prop_count);

				/* Conversion from SPropValue to mapi_SPropValue. (no padding here) */
				properties_array.cValues = prop_count;
				properties_array.lpProps = g_new0 (struct mapi_SPropValue, prop_count);
				for (i=0; i < prop_count; i++)
					cast_mapi_SPropValue(&properties_array.lpProps[i], &lpProps[i]);					

			} else {
				 GetPropsAll (&obj_message, &properties_array);
				 retval = GetLastError ();
			}

			if (retval == MAPI_E_SUCCESS) {
				int z;

				mapi_SPropValue_array_named(&obj_message, 
							    &properties_array);

				/* just to get all the other streams */
				for (z=0; z < properties_array.cValues; z++)
					if ((properties_array.lpProps[z].ulPropTag & 0xFFFF) == PT_BINARY)
						exchange_mapi_util_read_generic_stream (mem_ctx, &obj_message, 
											properties_array.lpProps[z].ulPropTag, &stream_list);

				if (!cb (&properties_array, *pfid, *pmid, recip_list, attach_list, data)) {
					printf("Breaking from fetching items\n");
					break;
				}

				mapi_object_release(&obj_message);
			}
		} else 
			mapi_errstr("OpenMessage", GetLastError());


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

		if (stream_list) {
			GSList *l;
			for (l = stream_list; l; l = l->next) {
				ExchangeMAPIStream *stream = (ExchangeMAPIStream *) (l->data);
				g_byte_array_free (stream->value, TRUE);
				stream->value = NULL;
			}
			g_slist_free (stream_list);
			stream_list = NULL;
		}
	}

	mapi_object_release(&obj_table);
	mapi_object_release(&obj_folder);

	UNLOCK ();
	return TRUE;
}

gpointer
exchange_mapi_connection_fetch_item (mapi_id_t fid, mapi_id_t mid, struct SPropTagArray *GetPropsTagArray, FetchItemCallback cb)
{
	mapi_object_t obj_store;	
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	enum MAPISTATUS retval;
	mapi_object_t obj_message;
	struct SRowSet SRowSet;
	struct SPropTagArray *SPropTagArray;
	struct mapi_SPropValue_array properties_array;
	struct SPropValue *lpProps;

	TALLOC_CTX *mem_ctx;
	uint32_t prop_count;
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

	printf("Opening folder %016llX\n", fid);
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
		
		if (GetPropsTagArray) {
			int i=0;

			struct SRow aRow;
			
			lpProps = talloc_zero(mem_ctx, struct SPropValue);
			
			retval = GetProps (&obj_message, GetPropsTagArray, &lpProps, &prop_count);

			/* Conversion from SPropValue to mapi_SPropValue. (no padding here) */
			properties_array.cValues = prop_count;
			properties_array.lpProps = g_new0 (struct mapi_SPropValue, prop_count);

			for (i=0; i < prop_count; i++)
				cast_mapi_SPropValue(&properties_array.lpProps[i], &lpProps[i]);					

		} else {
			GetPropsAll (&obj_message, &properties_array);
			retval = GetLastError ();
		}
		
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

	printf("Opening folder %016llX\n", fid);
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

/* FIXME: param olFolder is never used in the routine. Remove it and cleanup at the backends */
gboolean
exchange_mapi_remove_folder (uint32_t olFolder, mapi_id_t fid)
{
	enum MAPISTATUS retval;
	mapi_object_t obj_store;
	mapi_object_t obj_top;
	mapi_object_t obj_folder;
	ExchangeMAPIFolder *folder;
	gboolean result = FALSE;

	d(printf("%s(%d): Entering %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	folder = exchange_mapi_folder_get_folder (fid);
	g_return_val_if_fail (folder != NULL, FALSE);

	LOCK ();
	LOGALL ();
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_top);
	mapi_object_init(&obj_folder);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder to be removed */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* FIXME: If the folder has sub-folders, open each of them in turn, empty them and delete them.
	 * Note that this has to be done recursively, for the sub-folders as well. 
	 */

	/* Empty the contents of the folder */
	retval = EmptyFolder(&obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("EmptyFolder", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the top/parent folder */
	retval = OpenFolder(&obj_store, folder->parent_folder_id, &obj_top);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Attempt to delete the folder */
	retval = DeleteFolder(&obj_top, fid);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("DeleteFolder", GetLastError());
		goto cleanup;
	}

	result = TRUE;
	printf("Folder with id %016llX deleted\n", fid);

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	mapi_object_release(&obj_store);
	LOGNONE();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return result;
}

mapi_id_t 
exchange_mapi_create_folder (uint32_t olFolder, mapi_id_t pfid, const char *name)
{
	enum MAPISTATUS retval;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_top;
	struct SPropValue vals[1];
	const char *type;
	mapi_id_t fid = 0;

	d(printf("%s(%d): Entering %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	LOCK ();
	LOGALL ();
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_top);
	mapi_object_init(&obj_folder);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* We now open the top/parent folder */
	retval = OpenFolder(&obj_store, pfid, &obj_top);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}
	
	/* Attempt to create the folder */
	retval = CreateFolder(&obj_top, name, "Created using Evolution/libmapi", &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("CreateFolder", GetLastError());
		goto cleanup;
	}

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
		mapi_errstr("SetProps", GetLastError());
		goto cleanup;
	}

	fid = mapi_object_get_id (&obj_folder);
	printf("Folder created with id %016llX\n", fid);

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	mapi_object_release(&obj_store);
	LOGNONE();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	/* Shouldn't we return (ExchangeMAPIFolder *) instead of a plain fid ? */
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

	printf("Opening folder %016llX\n", fid);
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
		exchange_mapi_util_set_attachments (mem_ctx, &obj_message, attachments, FALSE);
	}

	if (recipients) {
		//exchange_mapi_util_set_attachments (mem_ctx, &obj_message, attachments, FALSE);
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

	printf("Opening folder %016llX\n", fid);
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
			printf("|---+ %-15s : (Container class: %s %016llX) UnRead : %d Total : %d\n", newname, class, *fid, *unread, *total);
			folder = exchange_mapi_folder_new (newname, parent_name, class, MAPI_PERSONAL_FOLDER, *fid, folder_id, *child, *unread, *total);
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
	folder = exchange_mapi_folder_new (utf8_mailbox_name, NULL, IPF_NOTE, MAPI_PERSONAL_FOLDER, id_mailbox, 0, 0, 0 ,0); 

	*mapi_folders = g_slist_prepend (*mapi_folders, folder);
	get_child_folders (mem_ctx, &obj_store, utf8_mailbox_name, id_mailbox, 0, mapi_folders);

	MAPIFreeBuffer(utf8_mailbox_name);

	UNLOCK ();

	*mapi_folders = g_slist_reverse (*mapi_folders);

	return TRUE;

}

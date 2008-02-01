/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors:
 *  	Srinivasa Ragavan <sragavan@novell.com>
 *  	Suman Manjunath <msuman@novell.com>
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

#define LOCK() 		g_message("%s(%d): %s: lock(connect_lock)", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_lock(&connect_lock)
#define UNLOCK() 	g_message("%s(%d): %s: unlock(connect_lock)", __FILE__, __LINE__, __PRETTY_FUNCTION__);g_static_rec_mutex_unlock(&connect_lock)
#define LOGALL() 	lp_set_cmdline(global_loadparm, "log level", "10"); global_mapi_ctx->dumpdata = TRUE;
#define LOGNONE() 	lp_set_cmdline(global_loadparm, "log level", "0"); global_mapi_ctx->dumpdata = FALSE;
//#define ENABLE_VERBOSE_LOG() 	global_mapi_ctx->dumpdata = TRUE;
#define ENABLE_VERBOSE_LOG()

/* Specifies READ/WRITE sizes to be used while handling attachment streams */
#define ATTACH_MAX_READ_SIZE  0x1000
#define ATTACH_MAX_WRITE_SIZE 0x1000

/* Specifies READ/WRITE sizes to be used while handling normal streams (struct SBinary_short) */
#define STREAM_MAX_READ_SIZE  0x1000
#define STREAM_MAX_WRITE_SIZE 0x1000

static void
exchange_mapi_set_recipients (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message , GSList *recipients);

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
exchange_mapi_util_read_generic_stream (mapi_object_t *obj_message, uint32_t proptag, GSList **stream_list)
{
	enum MAPISTATUS			retval;
	TALLOC_CTX 			*mem_ctx;
	DATA_BLOB 			body;
	struct SPropTagArray 		*SPropTagArray;
	struct SPropValue 		*lpProps;
	uint32_t			count, i;
	const struct SBinary_short 	*bin;
	struct mapi_SPropValue_array 	properties_array;

	/* sanity */
	g_return_val_if_fail (obj_message, FALSE);
	g_return_val_if_fail (((proptag & 0xFFFF) == PT_BINARY), FALSE);

	/* if compressed RTF stream, then return */
	g_return_val_if_fail (proptag != PR_RTF_COMPRESSED, FALSE);

	mem_ctx = talloc_init ("ExchangeMAPI_ReadGenericStream");

	/* initialize body DATA_BLOB */
	body.length = 0;
	body.data = talloc_zero(mem_ctx, uint8_t);

	/* Build the array of properties we want to fetch */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x1, proptag);
	retval = GetProps(obj_message, SPropTagArray, &lpProps, &count);
	MAPIFreeBuffer(SPropTagArray);

	if (retval != MAPI_E_SUCCESS || count != 0x1) {
		mapi_errstr("GetProps", GetLastError());
		return FALSE;
	}

	/* Build a mapi_SPropValue_array structure */
	properties_array.cValues = count;
	properties_array.lpProps = talloc_array (mem_ctx, struct mapi_SPropValue, count);
//	for (i=0; i < count; i++)
		cast_mapi_SPropValue(&properties_array.lpProps[0], &lpProps[0]);

	bin = (const struct SBinary_short *) find_mapi_SPropValue_data(&properties_array, proptag);
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

		/* This call is needed in case the read stream was a named prop. */
		mapi_SPropValue_array_named (obj_message, &properties_array);

		stream->value = g_byte_array_sized_new (body.length);
		stream->value = g_byte_array_append (stream->value, body.data, body.length);

		stream->proptag = properties_array.lpProps[0].ulPropTag;

		*stream_list = g_slist_append (*stream_list, stream);
	}

	talloc_free (mem_ctx);

	return (retval == MAPI_E_SUCCESS);
}

/*
 * Fetch the body given PR_MSG_EDITOR_FORMAT property value
 */
static gboolean
exchange_mapi_util_read_body_stream (mapi_object_t *obj_message, GSList **stream_list)
{
	enum MAPISTATUS			retval;
	TALLOC_CTX 			*mem_ctx;
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

	mem_ctx = talloc_init ("ExchangeMAPI_ReadBodyStream");

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
			} else if (exchange_mapi_util_read_generic_stream (obj_message, PR_HTML, stream_list)) {
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

	talloc_free (mem_ctx);

	return (retval == MAPI_E_SUCCESS);
}

static gboolean
exchange_mapi_util_delete_attachments (mapi_object_t *obj_message)
{
	enum MAPISTATUS		retval;
	TALLOC_CTX 		*mem_ctx;
	mapi_object_t 		obj_tb_attach;
	struct SPropTagArray	*proptags;
	struct SRowSet		rows_attach;
	uint32_t		attach_count;
	uint32_t		i_row_attach;
	gboolean 		status = TRUE;

	/* FIXME: remove this line once you upgrade to libmapi rev 327 or higher */
	return TRUE;
	/* also uncomment the line with the DeleteAttach call */

	mem_ctx = talloc_init ("ExchangeMAPI_DeleteAttachments");

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

	/* foreach attachment, delete by PR_ATTACH_NUM */
	for (i_row_attach = 0; i_row_attach < rows_attach.cRows; i_row_attach++) {
		const uint32_t	*num_attach;

		num_attach = (const uint32_t *) get_SPropValue_SRow_data(&rows_attach.aRow[i_row_attach], PR_ATTACH_NUM);

//		retval = DeleteAttach(obj_message, *num_attach);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("DeleteAttach", GetLastError());
			goto loop_cleanup;
		}

	loop_cleanup:
		if (retval != MAPI_E_SUCCESS)
			status = FALSE;
	}

cleanup:
	if (retval != MAPI_E_SUCCESS)
		status = FALSE;
	mapi_object_release(&obj_tb_attach);
	talloc_free (mem_ctx);

	return status;
}

/* Returns TRUE if all attachments were written succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_set_attachments (mapi_object_t *obj_message, GSList *attach_list, gboolean remove_existing)
{
	TALLOC_CTX 	*mem_ctx;
	const uint32_t 	cn_props_attach = 4;
	GSList 		*l;
	enum MAPISTATUS	retval;
	gboolean 	status = TRUE;

	if (remove_existing)
		exchange_mapi_util_delete_attachments (obj_message);

	mem_ctx = talloc_init ("ExchangeMAPI_SetAttachments");

	for (l = attach_list; l; l = l->next) {
		ExchangeMAPIAttachment 	*attachment = (ExchangeMAPIAttachment *) (l->data);
		int32_t 		flag;
		uint32_t 		total_written;
		gboolean 		done = FALSE;
		struct SPropValue 	*props_attach;
		mapi_object_t		obj_attach;
		mapi_object_t		obj_stream;

		props_attach = talloc_array (mem_ctx, struct SPropValue, cn_props_attach);
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
		talloc_free (props_attach);
	}

	talloc_free (mem_ctx);

	return status;
}

/* Returns TRUE if all attachments were read succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_get_attachments (mapi_object_t *obj_message, GSList **attach_list)
{
	enum MAPISTATUS		retval;
	TALLOC_CTX 		*mem_ctx;
	mapi_object_t 		obj_tb_attach;
	struct SPropTagArray	*proptags;
	struct SRowSet		rows_attach;
	uint32_t		attach_count;
	uint32_t		i_row_attach;
	gboolean 		status = TRUE;

	mem_ctx = talloc_init ("ExchangeMAPI_GetAttachments");

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
	talloc_free (mem_ctx);

	return status;
}

/* Returns TRUE if all recipients were read succcesfully, else returns FALSE */
static gboolean
exchange_mapi_util_get_recipients (mapi_object_t *obj_message, GSList **recip_list)
{
	enum MAPISTATUS		retval;
//	TALLOC_CTX 		*mem_ctx;
	struct SPropTagArray	proptags;
	struct SRowSet		rows_recip;
	uint32_t		i_row_recip;
	gboolean 		status = TRUE;

	/* FIXME: remove this line once you upgrade to libmapi rev 340 or higher */
	return TRUE;
	/* also uncomment the line with the GetRecipientTable call */

//	mem_ctx = talloc_init ("ExchangeMAPI_GetRecipients");

	/* fetch recipient table */
//	retval = GetRecipientTable(obj_message, &rows_recip, &proptags);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetRecipientTable", GetLastError());
		goto cleanup;
	}

	for (i_row_recip = 0; i_row_recip < rows_recip.cRows; i_row_recip++) {
		if (retval == MAPI_E_SUCCESS) {
			ExchangeMAPIRecipient 	*recipient = g_new0 (ExchangeMAPIRecipient, 1);
			const uint32_t *ui32;

			/* FIXME: fallback on EX address type */
			recipient->email_id = (const char *) find_SPropValue_data (&(rows_recip.aRow[i_row_recip]), PR_SMTP_ADDRESS_UNICODE);
			recipient->email_type = "SMTP";
			/* FIXME: fallback on other usable props */
			recipient->name = (const char *) find_SPropValue_data(&rows_recip.aRow[i_row_recip], PR_RECIPIENT_DISPLAY_NAME_UNICODE);
			ui32 = (const uint32_t *) find_SPropValue_data(&rows_recip.aRow[i_row_recip], PR_RECIPIENTS_FLAGS);
			recipient->flags = *ui32;
			ui32 = (const uint32_t *) find_SPropValue_data(&rows_recip.aRow[i_row_recip], PR_RECIPIENT_TYPE);
			recipient->type = *ui32;

			*recip_list = g_slist_append (*recip_list, recipient);
		}
	}

cleanup:
	if (retval != MAPI_E_SUCCESS)
		status = FALSE;
//	talloc_free (mem_ctx);

	return status;
}

// FIXME: May be we need to support Restrictions/Filters here. May be after libmapi-0.7.
gboolean
exchange_mapi_connection_fetch_items   (mapi_id_t fid, 
					const uint32_t *GetPropsList, const uint16_t cn_props, 
					BuildNameID build_name_id, 
					struct mapi_SRestriction *res,
					FetchItemsCallback cb, 
					gpointer data)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_table;
	struct SPropTagArray *SPropTagArray, *GetPropsTagArray;
	struct SRowSet SRowSet;
	uint32_t count, i;
	gboolean result = FALSE;

	d(printf("%s(%d): Entering %s: folder-id %016llX \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, fid));

	LOCK ();
	mem_ctx = talloc_init("ExchangeMAPI_FetchItems");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_table);

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Get a handle on the container */
	retval = GetContentsTable(&obj_folder, &obj_table);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetContentsTable", GetLastError());
		goto cleanup;
	}

	GetPropsTagArray = talloc_zero(mem_ctx, struct SPropTagArray);
	GetPropsTagArray->cValues = 0;

	SPropTagArray = set_SPropTagArray(mem_ctx, 0xA,
					  PR_FID,
					  PR_MID,
					  PR_INST_ID,
					  PR_INSTANCE_NUM,
					  PR_SUBJECT,
					  PR_MESSAGE_CLASS,
					  PR_LAST_MODIFICATION_TIME,
					  PR_HASATTACH,
					  /* FIXME: is this tag fit to check if a recipient table exists or not ? */
//				          PR_DISCLOSURE_OF_RECIPIENTS,
					  PR_RULE_MSG_PROVIDER,
					  PR_RULE_MSG_NAME);

	/* Set primary columns to be fetched */
	retval = SetColumns(&obj_table, SPropTagArray);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SetColumns", GetLastError());
		goto cleanup;
	}

	if (res) {
		/* Applying any restriction that are set. */
		retval = Restrict(&obj_table, res);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("Restrict", GetLastError());
			goto cleanup;
		}
	}

	/* Number of items in the container */
	retval = GetRowCount(&obj_table, &count);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("GetRowCount", GetLastError());
		goto cleanup;
	}

	/* Fill the table columns with data from the rows */
	retval = QueryRows(&obj_table, count, TBL_ADVANCE, &SRowSet);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("QueryRows", GetLastError());
		goto cleanup;
	}

	if ((GetPropsList && (cn_props > 0)) || build_name_id) {
		struct SPropTagArray *NamedPropsTagArray;
		uint32_t m, n=0;
		struct mapi_nameid *nameid;

		nameid = mapi_nameid_new(mem_ctx);
		NamedPropsTagArray = talloc_zero(mem_ctx, struct SPropTagArray);

		NamedPropsTagArray->cValues = 0;
		/* Add named props using callback */
		if (build_name_id) {
			if (!build_name_id (nameid, data)) {
				g_warning ("%s(%d): (%s): Could not build named props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
				goto GetProps_cleanup;
			}

			retval = mapi_nameid_GetIDsFromNames(nameid, &obj_folder, NamedPropsTagArray);
			if (retval != MAPI_E_SUCCESS) {
				mapi_errstr("mapi_nameid_GetIDsFromNames", GetLastError());
				goto GetProps_cleanup;
			}
		}

		GetPropsTagArray->cValues = (cn_props + NamedPropsTagArray->cValues);
		GetPropsTagArray->aulPropTag = talloc_array(mem_ctx, uint32_t, (cn_props + NamedPropsTagArray->cValues));

		for (m = 0; m < NamedPropsTagArray->cValues; m++, n++)
			GetPropsTagArray->aulPropTag[n] = NamedPropsTagArray->aulPropTag[m];

		for (m = 0; m < cn_props; m++, n++)
			GetPropsTagArray->aulPropTag[n] = GetPropsList[m];

	GetProps_cleanup:
			MAPIFreeBuffer (NamedPropsTagArray);
			talloc_free (nameid);
	}

	for (i = 0; i < SRowSet.cRows; i++) {
		mapi_object_t obj_message;
		struct mapi_SPropValue_array properties_array;
		const mapi_id_t *pfid;
		const mapi_id_t	*pmid;
		const bool *has_attach = NULL;
		const bool *disclose_recipients = NULL;
		GSList *attach_list = NULL;
		GSList *recip_list = NULL;
		GSList *stream_list = NULL;

		mapi_object_init(&obj_message);

		pfid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_FID);
		pmid = (const uint64_t *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_MID);

		has_attach = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_HASATTACH);
		/* disclose_recipients = (const bool *) get_SPropValue_SRow_data(&SRowSet.aRow[i], PR_DISCLOSURE_OF_RECIPIENTS); */

		retval = OpenMessage(&obj_folder, *pfid, *pmid, &obj_message, 0);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenMessage", GetLastError());
			goto loop_cleanup;
		}

		if (has_attach && *has_attach)
			exchange_mapi_util_get_attachments (&obj_message, &attach_list);

		exchange_mapi_util_get_recipients (&obj_message, &recip_list);

		/* get the main body stream no matter what */
		exchange_mapi_util_read_body_stream (&obj_message, &stream_list);

		if (GetPropsTagArray->cValues) {
			struct SPropValue *lpProps;
			uint32_t prop_count = 0, k;

			lpProps = talloc_zero(mem_ctx, struct SPropValue);
			retval = GetProps (&obj_message, GetPropsTagArray, &lpProps, &prop_count);

			/* Conversion from SPropValue to mapi_SPropValue. (no padding here) */
			properties_array.cValues = prop_count;
			properties_array.lpProps = talloc_array (mem_ctx, struct mapi_SPropValue, prop_count);
			for (k=0; k < prop_count; k++)
				cast_mapi_SPropValue(&properties_array.lpProps[k], &lpProps[k]);

			MAPIFreeBuffer(lpProps);
		} else
			retval = GetPropsAll (&obj_message, &properties_array);

		if (retval == MAPI_E_SUCCESS) {
			uint32_t z;

			/* just to get all the other streams */
			for (z=0; z < properties_array.cValues; z++)
				if ((properties_array.lpProps[z].ulPropTag & 0xFFFF) == PT_BINARY) 
					exchange_mapi_util_read_generic_stream (&obj_message, properties_array.lpProps[z].ulPropTag, &stream_list);

			mapi_SPropValue_array_named(&obj_message, &properties_array);

			if (!cb (&properties_array, *pfid, *pmid, stream_list, recip_list, attach_list, data)) {
				g_warning ("%s(%d): %s: Callback failed for message-id %016llX \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, *pmid);
			}
		}

		if (GetPropsTagArray->cValues) 
			talloc_free (properties_array.lpProps);

	loop_cleanup:
		mapi_object_release(&obj_message);

		/* should I ?? */
		if (attach_list)
			exchange_mapi_util_free_attachment_list (&attach_list);

		if (recip_list) 
			exchange_mapi_util_free_recipient_list (&recip_list);

		if (stream_list) 
			exchange_mapi_util_free_stream_list (&stream_list);
	}

	result = TRUE;

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_table);
	mapi_object_release(&obj_store);
	talloc_free (mem_ctx);
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return result;
}

gpointer
exchange_mapi_connection_fetch_item (mapi_id_t fid, mapi_id_t mid, 
				     const uint32_t *GetPropsList, const uint16_t cn_props, 
				     BuildNameID build_name_id, 
				     FetchItemCallback cb, 
				     gpointer data)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_message;
	struct mapi_SPropValue_array properties_array;
	struct SPropTagArray *GetPropsTagArray;
	GSList *attach_list = NULL;
	GSList *recip_list = NULL;
	GSList *stream_list = NULL;
	gpointer retobj = NULL;

	d(printf("%s(%d): Entering %s: folder-id %016llX message-id %016llX \n", __FILE__, __LINE__, __PRETTY_FUNCTION__, fid, mid));

	LOCK ();
	mem_ctx = talloc_init("ExchangeMAPI_FetchItem");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_message);

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	GetPropsTagArray = talloc_zero(mem_ctx, struct SPropTagArray);
	GetPropsTagArray->cValues = 0;

	if ((GetPropsList && (cn_props > 0)) || build_name_id) {
		struct SPropTagArray *NamedPropsTagArray;
		uint32_t m, n=0;
		struct mapi_nameid *nameid;

		nameid = mapi_nameid_new(mem_ctx);
		NamedPropsTagArray = talloc_zero(mem_ctx, struct SPropTagArray);

		NamedPropsTagArray->cValues = 0;
		/* Add named props using callback */
		if (build_name_id) {
			if (!build_name_id (nameid, data)) {
				g_warning ("%s(%d): (%s): Could not build named props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
				goto GetProps_cleanup;
			}

			retval = mapi_nameid_GetIDsFromNames(nameid, &obj_folder, NamedPropsTagArray);
			if (retval != MAPI_E_SUCCESS) {
				mapi_errstr("mapi_nameid_GetIDsFromNames", GetLastError());
				goto GetProps_cleanup;
			}
		}

		GetPropsTagArray->cValues = (cn_props + NamedPropsTagArray->cValues);
		GetPropsTagArray->aulPropTag = talloc_array(mem_ctx, uint32_t, (cn_props + NamedPropsTagArray->cValues));

		for (m = 0; m < NamedPropsTagArray->cValues; m++, n++)
			GetPropsTagArray->aulPropTag[n] = NamedPropsTagArray->aulPropTag[m];

		for (m = 0; m < cn_props; m++, n++)
			GetPropsTagArray->aulPropTag[n] = GetPropsList[m];

	GetProps_cleanup:
			MAPIFreeBuffer (NamedPropsTagArray);
			talloc_free (nameid);
	}

	/* Open the item */
	retval = OpenMessage(&obj_folder, fid, mid, &obj_message, 0x0);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMessage", GetLastError());
		goto cleanup;
	}

	/* Fetch attachments */
	exchange_mapi_util_get_attachments (&obj_message, &attach_list);

	/* Fetch recipients */
	exchange_mapi_util_get_recipients (&obj_message, &recip_list);

	/* get the main body stream no matter what */
	exchange_mapi_util_read_body_stream (&obj_message, &stream_list);

	if (GetPropsTagArray->cValues) {
		struct SPropValue *lpProps;
		uint32_t prop_count = 0, k;

		lpProps = talloc_zero(mem_ctx, struct SPropValue);
		retval = GetProps (&obj_message, GetPropsTagArray, &lpProps, &prop_count);

		/* Conversion from SPropValue to mapi_SPropValue. (no padding here) */
		properties_array.cValues = prop_count;
		properties_array.lpProps = talloc_array (mem_ctx, struct mapi_SPropValue, prop_count);
		for (k=0; k < prop_count; k++)
			cast_mapi_SPropValue(&properties_array.lpProps[k], &lpProps[k]);

		MAPIFreeBuffer(lpProps);
	} else
		retval = GetPropsAll (&obj_message, &properties_array);

	if (retval == MAPI_E_SUCCESS) {
		uint32_t z;

		/* just to get all the other streams */
		for (z=0; z < properties_array.cValues; z++)
			if ((properties_array.lpProps[z].ulPropTag & 0xFFFF) == PT_BINARY)
				exchange_mapi_util_read_generic_stream (&obj_message, properties_array.lpProps[z].ulPropTag, &stream_list);

		mapi_SPropValue_array_named(&obj_message, &properties_array);

		retobj = cb (&properties_array, fid, mid, stream_list, recip_list, attach_list);
	}

//	if (GetPropsTagArray->cValues) 
//		talloc_free (properties_array.lpProps);

	/* should I ?? */
	if (attach_list)
		exchange_mapi_util_free_attachment_list (&attach_list);

	if (recip_list) 
		exchange_mapi_util_free_recipient_list (&recip_list);

	if (stream_list) 
		exchange_mapi_util_free_stream_list (&stream_list);

cleanup:
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free (mem_ctx);
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return retobj;
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
//	LOGALL ();
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
/* libmapi revision 317 */
//	retval = CreateFolder(&obj_top, FOLDER_GENERIC, name, "Created using Evolution/libmapi", OPEN_IF_EXISTS, &obj_folder);
/* libmapi 0.6 */
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
	printf("Folder %s created with id %016llX\n", name, fid);

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	mapi_object_release(&obj_store);
//	LOGNONE();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	/* Shouldn't we return (ExchangeMAPIFolder *) instead of a plain fid ? */
	return fid;
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

	/* FIXME: If the folder has sub-folders, open each of them in turn, empty them and delete them.
	 * Note that this has to be done recursively, for the sub-folders as well. 
	 */

	/* Attempt to open the folder to be removed */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Empty the contents of the folder */
	retval = EmptyFolder(&obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("EmptyFolder", GetLastError());
		goto cleanup;
	}

	printf("Folder with id %016llX was emptied\n", fid);

	/* Attempt to open the top/parent folder */
	retval = OpenFolder(&obj_store, folder->parent_folder_id, &obj_top);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Call DeleteFolder on the folder to be removed */
	retval = DeleteFolder(&obj_top, fid);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("DeleteFolder", GetLastError());
		goto cleanup;
	}

	printf("Folder with id %016llX was deleted\n", fid);

	result = TRUE;

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_top);
	mapi_object_release(&obj_store);
	LOGNONE();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return result;
}

/*FixME : Why are we still having olFolder in our APIs ?? - Johnny */
gboolean 
exchange_mapi_rename_folder (uint32_t olFolder, mapi_id_t fid, const char *new_name)
{
	enum MAPISTATUS retval;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	ExchangeMAPIFolder *folder;
	struct SPropValue *props = NULL;
	TALLOC_CTX *mem_ctx;
	gboolean result = FALSE;

	mem_ctx = talloc_init("ExchangeMAPI_RenameFolder");

	folder = exchange_mapi_folder_get_folder (fid);

	g_return_val_if_fail (folder != NULL, FALSE);

	LOCK ();

	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);

	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Open the folder to be renamed */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	props = talloc_zero(mem_ctx, struct SPropValue);
	set_SPropValue_proptag (props, PR_DISPLAY_NAME, new_name );

	retval = SetProps(&obj_folder, props, 1);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SetProps", GetLastError());
		goto cleanup;
	}

	result = TRUE;

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	UNLOCK ();

	return result;
}

mapi_id_t
exchange_mapi_create_item (uint32_t olFolder, mapi_id_t fid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data, 
			   GSList *recipients, GSList *attachments)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_message;
	struct mapi_nameid *nameid;
	struct SPropTagArray *SPropTagArray;
	struct SPropValue *props = NULL;
	gint propslen = 0;
	mapi_id_t mid = 0;

	d(printf("%s(%d): Entering %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	LOCK ();
//	LOGALL ();
	mem_ctx = talloc_init("ExchangeMAPI_CreateItem");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_message);

	nameid = mapi_nameid_new(mem_ctx);
	SPropTagArray = talloc_zero(mem_ctx, struct SPropTagArray);

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* If fid not present then we'll use olFolder. Document this in API doc. */
	if (fid == 0) {
		retval = GetDefaultFolder(&obj_store, &fid, olFolder);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("GetDefaultFolder", GetLastError());
			goto cleanup;
		}
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Create the item */
	retval = CreateMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("CreateMessage", GetLastError());
		goto cleanup;
	}

//	d(mapi_object_debug (&obj_message));

	/* Add named props using callback */
	if (build_name_id) {
		if (!build_name_id (nameid, ni_data)) {
			g_warning ("%s(%d): (%s): Could not build named props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
			goto cleanup;
		}

		retval = mapi_nameid_GetIDsFromNames(nameid, &obj_folder, SPropTagArray);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("mapi_nameid_GetIDsFromNames", GetLastError());
			goto cleanup;
		}
	}

	/* Add regular props using callback */
	if (build_props) {
		propslen = build_props (&props, SPropTagArray, p_data);
		if (propslen < 1) {
			g_warning ("%s(%d): (%s): Could not build props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
			goto cleanup;
		}
	}

	/* set properties for the item */
	retval = SetProps(&obj_message, props, propslen);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SetProps", GetLastError());
		goto cleanup;
	}

	/* Set attachments if any */
	if (attachments) {
		exchange_mapi_util_set_attachments (&obj_message, attachments, FALSE);
	}

	/* Set recipients if any */
	if (recipients) {
		exchange_mapi_set_recipients (mem_ctx, &obj_message, recipients);
	}

	/* Finally, save all changes */
	retval = SaveChangesMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SaveChangesMessage", GetLastError());
		goto cleanup;
	}

	if (recipients) {
		/* Mark message as ready to be sent */
		retval = SubmitMessage(&obj_message);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("SubmitMessage", GetLastError());
			goto cleanup;
		}
	}

	mid = mapi_object_get_id (&obj_message);

cleanup:
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
//	LOGNONE ();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return mid;
}

gboolean
exchange_mapi_modify_item (uint32_t olFolder, mapi_id_t fid, mapi_id_t mid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data, 
			   GSList *recipients, GSList *attachments)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_message;
	struct mapi_nameid *nameid;
	struct SPropTagArray *SPropTagArray;
	struct SPropValue *props = NULL;
	gint propslen = 0;
	gboolean result = FALSE;

	d(printf("%s(%d): Entering %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	LOCK ();
	LOGALL ();
	mem_ctx = talloc_init("ExchangeMAPI_ModifyItem");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);
	mapi_object_init(&obj_message);

	nameid = mapi_nameid_new(mem_ctx);
	SPropTagArray = talloc_zero(mem_ctx, struct SPropTagArray);

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Open the item to be modified */
	retval = OpenMessage(&obj_folder, fid, mid, &obj_message, MAPI_MODIFY);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMessage", GetLastError());
		goto cleanup;
	}

//	d(mapi_object_debug (&obj_message));

	/* Add named props using callback */
	if (build_name_id) {
		if (!build_name_id (nameid, ni_data)) {
			g_warning ("%s(%d): (%s): Could not build named props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
			goto cleanup;
		}

		retval = mapi_nameid_GetIDsFromNames(nameid, &obj_folder, SPropTagArray);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("mapi_nameid_GetIDsFromNames", GetLastError());
			goto cleanup;
		}
	}

	/* Add regular props using callback */
	if (build_props) {
		propslen = build_props (&props, SPropTagArray, p_data);
		if (propslen < 1) {
			g_warning ("%s(%d): (%s): Could not build props \n", __FILE__, __LINE__, __PRETTY_FUNCTION__);
			goto cleanup;
		}
	}

	/* set properties for the item */
	retval = SetProps(&obj_message, props, propslen);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SetProps", GetLastError());
		goto cleanup;
	}

	/* Set attachments if any */
	if (attachments) {
		exchange_mapi_util_set_attachments (&obj_message, attachments, TRUE);
	}

	/* Set recipients if any */
	if (recipients) {
		//exchange_mapi_util_set_attachments (&obj_message, attachments, TRUE);
	}
 
	/* Finally, save all changes */
	retval = SaveChangesMessage(&obj_folder, &obj_message);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("SaveChangesMessage", GetLastError());
		goto cleanup;
	}

	if (recipients) {
		/* Mark message as ready to be sent */
		retval = SubmitMessage(&obj_message);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("SubmitMessage", GetLastError());
			goto cleanup;
		}
	}

	result = TRUE;

cleanup:
	mapi_object_release(&obj_message);
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	LOGNONE ();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return result;
}

gboolean
exchange_mapi_set_flags (uint32_t olFolder, mapi_id_t fid, GSList *mid_list, uint32_t flag)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	mapi_object_t obj_message;
	struct SPropValue *props = NULL;
	gint propslen = 0;
	gboolean result = FALSE;
	GSList *l;

	LOCK ();

	mem_ctx = talloc_init("ExchangeMAPI_ModifyItem");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	for (l = mid_list; l != NULL; l = g_slist_next (l)) {
		mapi_object_init(&obj_message);
		mapi_id_t mid = *((mapi_id_t *)l->data);

		retval = OpenMessage(&obj_folder, fid, mid, &obj_message, MAPI_MODIFY);

		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("OpenMessage", GetLastError());
			goto cleanup;
		}

		retval = SetReadFlags(&obj_folder, &obj_message, flag);
		if (retval != MAPI_E_SUCCESS) {
			mapi_errstr("SetReadFlags", GetLastError());
			goto cleanup;
		}

		mapi_object_release(&obj_message);
	}

	result = TRUE;

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	UNLOCK ();

	return result;
}

/* FIXME: param olFolder is never used in the routine. Remove it and cleanup at the backends */
gboolean
exchange_mapi_remove_items (uint32_t olFolder, mapi_id_t fid, GSList *mids)
{
	enum MAPISTATUS retval;
	TALLOC_CTX *mem_ctx;
	mapi_object_t obj_store;
	mapi_object_t obj_folder;
	uint32_t i;
	mapi_id_t *id_messages;
	GSList *tmp = mids;
	gboolean result = FALSE;

	d(printf("%s(%d): Entering %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	LOCK ();
	//	LOGALL ();
	mem_ctx = talloc_init("ExchangeMAPI_RemoveItems");
	mapi_object_init(&obj_store);
	mapi_object_init(&obj_folder);

	id_messages = talloc_array(mem_ctx, mapi_id_t, g_slist_length (mids)+1);
	for (i=0; tmp; tmp=tmp->next, i++) {
		struct id_list *data = tmp->data;
		id_messages[i] = data->id;
	}

	/* Open the message store */
	retval = OpenMsgStore(&obj_store);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenMsgStore", GetLastError());
		goto cleanup;
	}

	/* Attempt to open the folder */
	retval = OpenFolder(&obj_store, fid, &obj_folder);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("OpenFolder", GetLastError());
		goto cleanup;
	}

	/* Delete the messages from the folder */
	retval = DeleteMessage(&obj_folder, id_messages, i);
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr("DeleteMessage", GetLastError());
		goto cleanup;
	}

	result = TRUE;

cleanup:
	mapi_object_release(&obj_folder);
	mapi_object_release(&obj_store);
	talloc_free(mem_ctx);
	//	LOGNONE();
	UNLOCK ();

	d(printf("%s(%d): Leaving %s \n", __FILE__, __LINE__, __PRETTY_FUNCTION__));

	return result;
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
			newname = utf8tolinux(name);
			printf("|---+ %-15s : (Container class: %s %016llX) UnRead : %d Total : %d\n", newname, class, *fid, *unread, *total);
			folder = exchange_mapi_folder_new (newname, parent_name, class, MAPI_PERSONAL_FOLDER, *fid, folder_id, *child, *unread, *total);
			*mapi_folders = g_slist_prepend (*mapi_folders, folder);
			if (*child)
				get_child_folders(mem_ctx, &obj_folder, newname, *fid, count + 1, mapi_folders);
			g_free (newname);

			
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
	utf8_mailbox_name = utf8tolinux(mailbox_name);

	/* FIXME: May have to get the child folders count? Do we need/use it? */
	folder = exchange_mapi_folder_new (utf8_mailbox_name, NULL, IPF_NOTE, MAPI_PERSONAL_FOLDER, id_mailbox, 0, 0, 0 ,0); 

	*mapi_folders = g_slist_prepend (*mapi_folders, folder);
	get_child_folders (mem_ctx, &obj_store, utf8_mailbox_name, id_mailbox, 0, mapi_folders);

	g_free(utf8_mailbox_name);

	UNLOCK ();

	*mapi_folders = g_slist_reverse (*mapi_folders);

	return TRUE;

}

static char**
mapi_parse_recipients(TALLOC_CTX *mem_ctx, const char *recipients)
{
	char		**usernames;
	char		*tmp = NULL;
	uint32_t	j = 0;

	/* no recipients */
	if (!recipients) {
		return NULL;
	}

	if ((tmp = strtok((char *)recipients, ",")) == NULL) {
		DEBUG(2, ("Invalid recipient string format\n"));
		return NULL;
	}
	
	usernames = talloc_array(mem_ctx, char *, 2);
	usernames[0] = talloc_strdup(mem_ctx, tmp);
	
	for (j = 1; (tmp = strtok(NULL, ",")) != NULL; j++) {
		usernames = talloc_realloc(mem_ctx, usernames, char *, j+2);
		usernames[j] = talloc_strdup(mem_ctx, tmp);
	}
	usernames[j] = 0;

	return (usernames);
}

static char**
mapi_collate_recipients(TALLOC_CTX *mem_ctx, char **usernames, const char *recipient)
{
	guint count = 0;
	/* no recipients */
	if (!recipient) 
		return NULL;

	for (count = 0; usernames  && usernames[count]; count++);
	usernames = talloc_realloc(mem_ctx, usernames, char *, count+2);
	usernames[count] = talloc_strdup(mem_ctx, recipient);

	usernames[++count] = 0;

	return (usernames);
}

/**
 * We set external recipients at the end of aRow
 */
static bool set_external_recipients(TALLOC_CTX *mem_ctx, struct SRowSet *SRowSet, const char *username, enum ulRecipClass RecipClass)
{
	uint32_t		last;
	struct SPropValue	SPropValue;

	SRowSet->aRow = talloc_realloc(mem_ctx, SRowSet->aRow, struct SRow, SRowSet->cRows + 2);
	last = SRowSet->cRows;
	SRowSet->aRow[last].cValues = 0;
	SRowSet->aRow[last].lpProps = talloc_zero(mem_ctx, struct SPropValue);
	
	/* PR_OBJECT_TYPE */
	SPropValue.ulPropTag = PR_OBJECT_TYPE;
	SPropValue.value.l = MAPI_MAILUSER;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_DISPLAY_TYPE */
	SPropValue.ulPropTag = PR_DISPLAY_TYPE;
	SPropValue.value.l = 0;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_GIVEN_NAME */
	SPropValue.ulPropTag = PR_GIVEN_NAME;
	SPropValue.value.lpszA = username;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_DISPLAY_NAME */
	SPropValue.ulPropTag = PR_DISPLAY_NAME;
	SPropValue.value.lpszA = username;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_7BIT_DISPLAY_NAME */
	SPropValue.ulPropTag = PR_7BIT_DISPLAY_NAME;
	SPropValue.value.lpszA = username;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_SMTP_ADDRESS */
	SPropValue.ulPropTag = PR_SMTP_ADDRESS;
	SPropValue.value.lpszA = username;
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	/* PR_ADDRTYPE */
	SPropValue.ulPropTag = PR_ADDRTYPE;
	SPropValue.value.lpszA = "SMTP";
	SRow_addprop(&(SRowSet->aRow[last]), SPropValue);

	SetRecipientType(&(SRowSet->aRow[last]), RecipClass);

	SRowSet->cRows += 1;
	return true;
}



static gboolean
mapi_set_usernames_RecipientType(TALLOC_CTX *mem_ctx, uint32_t *index, struct SRowSet *rowset, 
				 char **usernames, struct FlagList *flaglist, enum ulRecipClass RecipClass)
{
	uint32_t	i;
	uint32_t	count = *index;
	static uint32_t	counter = 0;

	if (count == 0) counter = 0;
	if (!usernames) return FALSE;

#if 0 
	for (i = 0; usernames[i]; i++) {
		/*FixMe*/
		if (flaglist->ulFlags[count] == MAPI_UNRESOLVED) {
			set_external_recipients(mem_ctx, rowset, usernames[i], RecipClass);
			printf("%s(%d):%s: MAPI_UNRESOLVED : %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__,usernames[i] );
		}

		if (flaglist->ulFlags[count] == MAPI_RESOLVED) {
			SetRecipientType(&(rowset->aRow[counter]), RecipClass);
			printf("%s(%d):%s: MAPI_RESOLVED : %s\n", __FILE__, __LINE__, __PRETTY_FUNCTION__,usernames[i] );
			counter++;
		}
		count++;
	}
#endif 

#if 1 //Working Code
	for (i = 0; usernames[i]; i++) {
		/*FixMe*/
		set_external_recipients(mem_ctx, rowset, usernames[i], RecipClass);

		count++;
	}
#endif 	
	*index = count;
	
	return TRUE;
}

static char**
mapi_collapse_recipients(TALLOC_CTX *mem_ctx, char **mapi_to, char **mapi_cc, char **mapi_bcc)
{
	uint32_t	count;
	uint32_t       	i;
	char		**usernames;

	if (!mapi_to && !mapi_cc && !mapi_bcc) return NULL;

	count = 0;
	for (i = 0; mapi_to  && mapi_to[i];  i++,  count++);
	for (i = 0; mapi_cc  && mapi_cc[i];  i++,  count++);
	for (i = 0; mapi_bcc && mapi_bcc[i]; i++, count++);

	usernames = talloc_array(mem_ctx, char *, count + 1);
	count = 0;

	for (i = 0; mapi_to && mapi_to[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_to[i]);
	}

	for (i = 0; mapi_cc && mapi_cc[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_cc[i]);
	}

	for (i = 0; mapi_bcc && mapi_bcc[i]; i++, count++) {
		usernames[count] = talloc_strdup(mem_ctx, mapi_bcc[i]);
	}

	usernames[count++] = 0;

	return usernames;
}

#define DEBUG_SET_RECTP 1

static void
exchange_mapi_set_recipients (TALLOC_CTX *mem_ctx, mapi_object_t *obj_message , GSList *recipients)
{
	struct SPropTagArray *SPropTagArray;
	struct SPropValue SPropValue;
	struct SRowSet *SRowSet = NULL;
	struct FlagList	*flaglist = NULL;
	enum MAPISTATUS	retval;

	char **usernames = NULL;
	char **usernames_to;
	char **usernames_cc;
	char **usernames_bcc;
	uint32_t index = 0;
	GSList *recipient_list = recipients;

	/* message->ModifyRecipients() */
	SPropTagArray = set_SPropTagArray(mem_ctx, 0x6,
					  PR_OBJECT_TYPE,
					  PR_DISPLAY_TYPE,
					  PR_7BIT_DISPLAY_NAME,
					  PR_DISPLAY_NAME,
					  PR_SMTP_ADDRESS,
					  PR_GIVEN_NAME);

	//Recipient table
	usernames_to = talloc_array(mem_ctx, char *, 1);
	usernames_to[0] = 0;

	usernames_cc = talloc_array(mem_ctx, char *, 1);
	usernames_cc[0] = 0;

	usernames_bcc = talloc_array(mem_ctx, char *, 1);
	usernames_bcc[0] = 0;

	for (;recipient_list; recipient_list = g_slist_next (recipient_list)) {
		
		gchar *recipient_email = ((ExchangeMAPIRecipient *)recipient_list->data)->email_id;
		ExchangeMAPIRecipientType recipient_type = ((ExchangeMAPIRecipient *)recipient_list->data)->type;

		switch (recipient_type) {
		case RECIPIENT_TO :
			usernames_to = mapi_collate_recipients(mem_ctx, usernames_to, recipient_email);
			break;
		case RECIPIENT_CC :
			usernames_cc = mapi_collate_recipients(mem_ctx, usernames_cc, recipient_email);
			break;
		case RECIPIENT_BCC :
			usernames_bcc = mapi_collate_recipients(mem_ctx, usernames_bcc, recipient_email);
			break;
		default:
			g_warning ("exchange_mapi_set_recipients : Unknown Recipient type");
		}
	}

	usernames = mapi_collapse_recipients(mem_ctx, usernames_to, usernames_cc, usernames_bcc);

	retval = ResolveNames((const char **)usernames, SPropTagArray, &SRowSet, &flaglist, 0);
	mapi_errstr("ResolveNames", GetLastError());
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

	/* Bug Bug  */
/* 	if (!SRowSet) */
		SRowSet = talloc_zero(mem_ctx, struct SRowSet);

	mapi_set_usernames_RecipientType(mem_ctx, &index, SRowSet, usernames_to,  flaglist, MAPI_TO);
	mapi_set_usernames_RecipientType(mem_ctx, &index, SRowSet, usernames_cc,  flaglist, MAPI_CC);
	mapi_set_usernames_RecipientType(mem_ctx, &index, SRowSet, usernames_bcc, flaglist, MAPI_BCC);

	/* FIXME no saving mail */
	if (index == 0) {
		printf("no valid recipients set\n");
		return -1;
	}

	SPropValue.ulPropTag = PR_SEND_INTERNET_ENCODING;
	SPropValue.value.l = 0;
	SRowSet_propcpy(mem_ctx, SRowSet, SPropValue);

#ifdef DEBUG_SET_RECTP
	mapidump_SRowSet (SRowSet, "\t**" );
#endif
	LOGALL();
	retval = ModifyRecipients (obj_message, SRowSet);
	LOGNONE();

	mapi_errstr("ModifyRecpients", GetLastError());
	if (retval != MAPI_E_SUCCESS) {
		mapi_errstr(__PRETTY_FUNCTION__, GetLastError());				
		UNLOCK ();
		return FALSE;
	}

}






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

#ifndef EXCHANGE_MAPI_CONNECTION_H
#define EXCHANGE_MAPI_CONNECTION_H 

#include <libmapi/libmapi.h>

typedef enum {
	RECIPIENT_ORIG = 0x0,
	RECIPIENT_TO   = 0x1,
	RECIPIENT_CC   = 0x2,
	RECIPIENT_BCC  = 0x3
} ExchangeMAPIRecipientType;

typedef enum _ExchangeMapiOptions {
	MAPI_OPTIONS_FETCH_ATTACHMENTS = 1<<0,
	MAPI_OPTIONS_FETCH_RECIPIENTS = 1<<1,
	MAPI_OPTIONS_FETCH_BODY_STREAM = 1<<2,
	MAPI_OPTIONS_FETCH_GENERIC_STREAMS = 1<<3
} ExchangeMapiOptions;

#define MAPI_OPTIONS_FETCH_ALL MAPI_OPTIONS_FETCH_ATTACHMENTS | \
	                       MAPI_OPTIONS_FETCH_RECIPIENTS | \
	                       MAPI_OPTIONS_FETCH_BODY_STREAM | \
	                       MAPI_OPTIONS_FETCH_GENERIC_STREAMS

/* FIXME: need to accomodate rendering position */
typedef struct {
	GByteArray *value;
	const gchar *filename;
	const gchar *mime_type;
} ExchangeMAPIAttachment;

typedef struct {
	GByteArray *value;
	uint32_t proptag;
} ExchangeMAPIStream;

typedef struct {
	GByteArray *value;
	uint32_t proptag;
	uint32_t editor_format;
} ExchangeMAPIBodyStream;

typedef struct {
	const char *email_id;
	const char *email_type;
	const char *name;
	uint32_t trackstatus;
	guint32 flags;
	ExchangeMAPIRecipientType type;
} ExchangeMAPIRecipient;

struct id_list {
	mapi_id_t id;
};

typedef gboolean (*FetchItemsCallback) 	(struct mapi_SPropValue_array *, const mapi_id_t fid, const mapi_id_t mid, 
					GSList *streams, GSList *recipients, GSList *attachments, gpointer data);
typedef gpointer (*FetchItemCallback) 	(struct mapi_SPropValue_array *, const mapi_id_t fid, const mapi_id_t mid, 
					GSList *streams, GSList *recipients, GSList *attachments);
typedef gboolean (*BuildNameID) 	(struct mapi_nameid *nameid, gpointer data);
typedef int 	 (*BuildProps) 		(struct SPropValue **, struct SPropTagArray *, gpointer data);

gboolean 
exchange_mapi_connection_new (const char *profile, const char *password);

void
exchange_mapi_connection_close (void);

gboolean
exchange_mapi_connection_exists (void);

gpointer
exchange_mapi_connection_fetch_item (mapi_id_t fid, mapi_id_t mid, 
				     const uint32_t *GetPropsList, const uint16_t cn_props, 
				     BuildNameID build_name_id, FetchItemCallback cb, 
				     gpointer data, guint32 options);
gboolean
exchange_mapi_connection_fetch_items (mapi_id_t fid, 
				      const uint32_t *GetPropsList, const uint16_t cn_props, BuildNameID build_name_id,  
				      struct mapi_SRestriction *res, 
				      FetchItemsCallback cb, gpointer data, guint32 options);

mapi_id_t 
exchange_mapi_create_folder (uint32_t olFolder, mapi_id_t pfid, const char *name);
gboolean 
exchange_mapi_remove_folder (uint32_t olFolder, mapi_id_t fid);
gboolean 
exchange_mapi_rename_folder (mapi_id_t fid, const char *new_name);

mapi_id_t
exchange_mapi_create_item (uint32_t olFolder, mapi_id_t fid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data, 
			   GSList *recipients, GSList *attachments);
gboolean
exchange_mapi_modify_item (uint32_t olFolder, mapi_id_t fid, mapi_id_t mid, 
			   BuildNameID build_name_id, gpointer ni_data, 
			   BuildProps build_props, gpointer p_data,
			   GSList *recipients, GSList *attachments);

gboolean
exchange_mapi_set_flags (uint32_t olFolder, mapi_id_t fid, GSList *mid_list, uint32_t flag);

gboolean
exchange_mapi_remove_items (uint32_t olFolder, mapi_id_t fid, GSList *mids);

gboolean exchange_mapi_get_folders_list (GSList **mapi_folders); 
gboolean exchange_mapi_get_pf_folders_list (GSList **mapi_folders); 

#endif

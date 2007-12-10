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

#ifndef EXCHANGE_MAPI_CONNECTION_H
#define EXCHANGE_MAPI_CONNECTION_H 

#include <libmapi/libmapi.h>

/* FIXME: need to accomodate rendering position */
typedef struct {
	GByteArray *value;
	const char *filename;
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
} ExchangeMAPIRecipient;


typedef gboolean (*FetchItemsCallback) (struct mapi_SPropValue_array *, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data);
typedef gpointer  (*FetchItemCallback) (struct mapi_SPropValue_array *, const mapi_id_t fid, const mapi_id_t mid);
typedef gboolean  (*BuildNameID) (struct mapi_nameid	*nameid, gpointer data);
typedef int  (*BuildProps) (struct SPropValue **, struct SPropTagArray *, gpointer data);
typedef struct SPropTagArray *  (*BuildPropTagArray) (TALLOC_CTX *ctx);
gboolean 
exchange_mapi_connection_new (const char *profile, const char *password);

void
exchange_mapi_connection_close (void);

gboolean
exchange_mapi_connection_exists (void);

gpointer
exchange_mapi_connection_fetch_item (mapi_id_t fid, mapi_id_t mid, 
				     struct SPropTagArray *GetPropsTagArray, FetchItemCallback cb);

gboolean
exchange_mapi_connection_fetch_items (mapi_id_t fid, struct SPropTagArray *GetPropsTagArray, 
				      struct mapi_SRestriction *res, BuildPropTagArray bpta_cb, 
				      FetchItemsCallback cb, gpointer data);

mapi_id_t 
exchange_mapi_create_folder (uint32_t olFolder, mapi_id_t pfid, const char *name);
gboolean 
exchange_mapi_remove_folder (uint32_t olFolder, mapi_id_t fid);

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
exchange_mapi_remove_items (uint32_t olFolder, mapi_id_t fid, GSList *mids);

gboolean exchange_mapi_get_folders_list (GSList **mapi_folders); 

#endif

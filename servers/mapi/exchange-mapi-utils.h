/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
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



#ifndef EXCHANGE_MAPI_UTILS_H
#define EXCHANGE_MAPI_UTILS_H 

#include <libmapi/libmapi.h>
#include <glib.h>
#include "exchange-mapi-connection.h"

gchar *
utf8tolinux (const char *wstring);

gchar *
exchange_mapi_util_mapi_id_to_string (mapi_id_t id);
gboolean 
exchange_mapi_util_mapi_id_from_string (const char *str, mapi_id_t *id);

gchar *
exchange_mapi_util_mapi_ids_to_uid (mapi_id_t fid, mapi_id_t mid);
gboolean 
exchange_mapi_util_mapi_ids_from_uid (const char *str, mapi_id_t *fid, mapi_id_t *mid);

void *
exchange_mapi_util_find_row_propval (struct SRow *aRow, uint32_t proptag);
void *
exchange_mapi_util_find_array_propval (struct mapi_SPropValue_array *properties, uint32_t proptag);

ExchangeMAPIStream *
exchange_mapi_util_find_stream (GSList *stream_list, uint32_t proptag);

void 
exchange_mapi_util_free_attachment_list (GSList **attach_list);
void 
exchange_mapi_util_free_recipient_list (GSList **recip_list);
void 
exchange_mapi_util_free_stream_list (GSList **stream_list);

const gchar *
exchange_mapi_util_ex_to_smtp (const gchar *ex_address);

void
exchange_mapi_debug_property_dump (struct mapi_SPropValue_array *properties);

#endif

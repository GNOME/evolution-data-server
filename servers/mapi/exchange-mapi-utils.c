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



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef G_OS_WIN32
/* Undef the similar macro from pthread.h, it doesn't check if
 * gmtime() returns NULL.
 */
#undef gmtime_r

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#include "exchange-mapi-utils.h"


/* Converts a string from Windows-UTF8 to classic-UTF8.
 * NOTE: If the returned value is non-NULL, the caller has to free the newly
 * allocated string using g_free()
 */
gchar *
utf8tolinux (const char *wstring)
{
	TALLOC_CTX 	*mem_ctx;
	gchar		*newstr, *retval = NULL;

	g_return_val_if_fail (wstring != NULL, NULL);

	mem_ctx = talloc_init ("ExchangeMAPI_utf8tolinux");

	newstr = windows_to_utf8(mem_ctx, wstring);

	if (g_utf8_validate (newstr, -1, NULL)) 
		retval = g_strdup (newstr);

	talloc_free (mem_ctx);

	return retval;
}

inline gchar *
exchange_mapi_util_mapi_id_to_string (mapi_id_t id)
{
	return g_strdup_printf ("%016llX", id);
}

inline gboolean 
exchange_mapi_util_mapi_id_from_string (const char *str, mapi_id_t *id)
{
	gint n = 0;

	if (str && *str)
		n = sscanf (str, "%016llX", id);

	return (n == 1);
}

/* NOTE: We use the UID as a combination of the folder-id and the message-id. 
 * Specifically, it is in this format: ("%016llX%016llX", fid, mid).
 */
inline gchar *
exchange_mapi_util_mapi_ids_to_uid (mapi_id_t fid, mapi_id_t mid)
{
	return g_strdup_printf ("%016llX%016llX", fid, mid);
}

inline gboolean 
exchange_mapi_util_mapi_ids_from_uid (const char *str, mapi_id_t *fid, mapi_id_t *mid)
{
	gint n = 0;

	if (str && *str)
		n = sscanf (str, "%016llX%016llX", fid, mid);

	return (n == 2);
}

/*
 * Retrieve the property value for a given SRow and property tag.  
 *
 * If the property type is a string: fetch PT_STRING8 then PT_UNICODE
 * in case the desired property is not available in first choice.
 *
 * Fetch property normally for any others properties
 */
/* NOTE: For now, since this function has special significance only for
 * 'string' type properties, callers should (preferably) use it for fetching 
 * such properties alone. If callers are sure that proptag would, for instance, 
 * return an 'int' or a 'systime', they should prefer find_SPropValue_data.
 */
void *
exchange_mapi_util_find_row_propval (struct SRow *aRow, uint32_t proptag)
{
	if (((proptag & 0xFFFF) == PT_STRING8) ||
	    ((proptag & 0xFFFF) == PT_UNICODE)) {
		const char 	*str;

		proptag = (proptag & 0xFFFF0000) | PT_STRING8;
		str = (const char *)find_SPropValue_data(aRow, proptag);
		if (str) 
			return (void *)str;

		proptag = (proptag & 0xFFFF0000) | PT_UNICODE;
		str = (const char *)find_SPropValue_data(aRow, proptag);
		return (void *)str;
	} 

	/* NOTE: Similar generalizations (if any) for other property types 
	 * can be made here. 
	 */

	return (void *)find_SPropValue_data(aRow, proptag);
}

/*
 * Retrieve the property value for a given mapi_SPropValue_array and property tag.  
 *
 * If the property type is a string: fetch PT_STRING8 then PT_UNICODE
 * in case the desired property is not available in first choice.
 *
 * Fetch property normally for any others properties
 */
/* NOTE: For now, since this function has special significance only for
 * 'string' type properties, callers should (preferably) use it for fetching 
 * such properties alone. If callers are sure that proptag would, for instance, 
 * return an 'int' or a 'systime', they should prefer find_mapi_SPropValue_data.
 */
void *
exchange_mapi_util_find_array_propval (struct mapi_SPropValue_array *properties, uint32_t proptag)
{
	if (((proptag & 0xFFFF) == PT_STRING8) ||
	    ((proptag & 0xFFFF) == PT_UNICODE)) {
		const char 	*str;

		proptag = (proptag & 0xFFFF0000) | PT_STRING8;
		str = (const char *)find_mapi_SPropValue_data(properties, proptag);
		if (str) 
			return (void *)str;

		proptag = (proptag & 0xFFFF0000) | PT_UNICODE;
		str = (const char *)find_mapi_SPropValue_data(properties, proptag);
		return (void *)str;
	} 

	/* NOTE: Similar generalizations (if any) for other property types 
	 * can be made here. 
	 */

	return (void *)find_mapi_SPropValue_data(properties, proptag);
}

ExchangeMAPIStream *
exchange_mapi_util_find_stream (GSList *stream_list, uint32_t proptag)
{
	GSList *l = stream_list;

	for (; l != NULL; l = l->next) {
		ExchangeMAPIStream *stream = (ExchangeMAPIStream *) (l->data);
		if (stream->proptag == proptag)
			return stream;
	}

	return NULL;
}

void 
exchange_mapi_util_free_attachment_list (GSList **attach_list)
{
	GSList *l = *attach_list;

	if(!l)
		return;

	for (; l != NULL; l = l->next) {
		ExchangeMAPIAttachment *attachment = (ExchangeMAPIAttachment *) (l->data);
		g_byte_array_free (attachment->value, TRUE);
		attachment->value = NULL;
		g_free (attachment);
		attachment = NULL;
	}
	g_slist_free (l);
	l = NULL;
}

void 
exchange_mapi_util_free_recipient_list (GSList **recip_list)
{
	GSList *l = *recip_list;

	if(!l)
		return;

	for (; l != NULL; l = l->next) {
		ExchangeMAPIRecipient *recipient = (ExchangeMAPIRecipient *) (l->data);
		g_free (recipient);
		recipient = NULL;
	}
	g_slist_free (l);
	l = NULL;
}

void 
exchange_mapi_util_free_stream_list (GSList **stream_list)
{
	GSList *l = *stream_list;

	if(!l)
		return;

	for (; l != NULL; l = l->next) {
		ExchangeMAPIStream *stream = (ExchangeMAPIStream *) (l->data);
		g_byte_array_free (stream->value, TRUE);
		stream->value = NULL;
		g_free (stream);
		stream = NULL;
	}
	g_slist_free (l);
	l = NULL;
}

const gchar *
exchange_mapi_util_ex_to_smtp (const gchar *ex_address)
{
	enum MAPISTATUS 	retval;
	TALLOC_CTX 		*mem_ctx;
	struct SPropTagArray	*SPropTagArray;
	struct SRowSet 		*SRowSet = NULL;
	struct FlagList		*flaglist = NULL;
	const gchar 		*str_array[2];
	const gchar 		*smtp_addr = NULL;

	g_return_val_if_fail (ex_address != NULL, NULL);

	str_array[0] = ex_address;
	str_array[1] = NULL;

	mem_ctx = talloc_init("ExchangeMAPI_EXtoSMTP");

	SPropTagArray = set_SPropTagArray(mem_ctx, 0x2,
					  PR_SMTP_ADDRESS,
					  PR_SMTP_ADDRESS_UNICODE);

	retval = ResolveNames((const char **)str_array, SPropTagArray, &SRowSet, &flaglist, 0);
	if (retval != MAPI_E_SUCCESS)
		retval = ResolveNames((const char **)str_array, SPropTagArray, &SRowSet, &flaglist, MAPI_UNICODE);

	if (retval == MAPI_E_SUCCESS && SRowSet && SRowSet->cRows == 1) {
		smtp_addr = (const char *) find_SPropValue_data(SRowSet->aRow, PR_SMTP_ADDRESS);
		if (!smtp_addr)
			smtp_addr = (const char *) find_SPropValue_data(SRowSet->aRow, PR_SMTP_ADDRESS_UNICODE);
	}

	talloc_free (mem_ctx);

	return smtp_addr;
}

void
exchange_mapi_debug_property_dump (struct mapi_SPropValue_array *properties)
{
	gint i = 0;

	for (i = 0; i < properties->cValues; i++) { 
		for (i = 0; i < properties->cValues; i++) {
			struct mapi_SPropValue *lpProp = &properties->lpProps[i];
			const char *tmp =  get_proptag_name (lpProp->ulPropTag);
			char t_str[26];
			if (tmp && *tmp)
				printf("\n%s \t",tmp);
			else
				printf("\n%x \t", lpProp->ulPropTag);
			switch(lpProp->ulPropTag & 0xFFFF) {
			case PT_BOOLEAN:
				printf(" (bool) - %d", lpProp->value.b);
				break;
			case PT_I2:
				printf(" (uint16_t) - %d", lpProp->value.i);
				break;
			case PT_LONG:
				printf(" (long) - %u", lpProp->value.l);
				break;
			case PT_DOUBLE:
				printf (" (double) -  %lf", lpProp->value.dbl);
				break;
			case PT_I8:
				printf (" (int) - %lld", lpProp->value.d);
				break;
			case PT_SYSTIME: {
					struct timeval t;
					struct tm tm;
					get_mapi_SPropValue_array_date_timeval (&t, properties, lpProp->ulPropTag);
					gmtime_r (&(t.tv_sec), &tm);
					strftime (t_str, 26, "%Y-%m-%dT%H:%M:%SZ", &tm);
				}
				printf (" (struct FILETIME *) - %p\t (struct timeval) %s\t", &lpProp->value.ft, t_str);
				break;
			case PT_ERROR:
				printf (" (error) - %p", lpProp->value.err);
				break;
			case PT_STRING8:
				printf(" (string) - %s", lpProp->value.lpszA ? lpProp->value.lpszA : "null" );
				break;
			case PT_UNICODE:
				printf(" (unicodestring) - %s", lpProp->value.lpszW ? lpProp->value.lpszW : "null");
				break;
			case PT_BINARY:
//				printf(" (struct SBinary_short *) - %p", &lpProp->value.bin);
				break;
			case PT_MV_STRING8:
 				printf(" (struct mapi_SLPSTRArray *) - %p", &lpProp->value.MVszA);
				break;
			default:
				printf(" - NONE NULL");
			}
		}
	}
}

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



#include "e-cal-backend-mapi.h"
#include "e-cal-backend-mapi-constants.h"
#include "e-cal-backend-mapi-tz-utils.h"

#define d(x) 

#define MAPPING_SEPARATOR "~~~"

static GStaticRecMutex mutex = G_STATIC_REC_MUTEX_INIT;

static GHashTable *mapi_to_ical = NULL;
static GHashTable *ical_to_mapi = NULL;

static const gchar *lru_mapi_id = NULL;
static const gchar *lru_ical_id = NULL;

const gchar *
e_cal_backend_mapi_tz_util_get_mapi_equivalent (const gchar *ical_tzid)
{
	g_return_val_if_fail ((ical_tzid && *ical_tzid), NULL);

	g_static_rec_mutex_lock(&mutex);
	if (!(mapi_to_ical && ical_to_mapi)) {
		g_static_rec_mutex_unlock(&mutex);
		return NULL;
	}

	d(g_message("%s(%d): %s of '%s' ", __FILE__, __LINE__, __PRETTY_FUNCTION__, ical_tzid));

	if (lru_ical_id && !g_ascii_strcasecmp (ical_tzid, lru_ical_id)) {
		g_static_rec_mutex_unlock(&mutex);
		return lru_mapi_id;
	}

	lru_mapi_id = lru_ical_id = NULL;
	if ((lru_mapi_id = g_hash_table_lookup (ical_to_mapi, ical_tzid)) != NULL)
		lru_ical_id = ical_tzid;

	g_static_rec_mutex_unlock(&mutex);

	return lru_mapi_id;
}

const gchar *
e_cal_backend_mapi_tz_util_get_ical_equivalent (const gchar *mapi_tzid)
{
	g_return_val_if_fail ((mapi_tzid && *mapi_tzid), NULL);

	g_static_rec_mutex_lock(&mutex);
	if (!(mapi_to_ical && ical_to_mapi)) {
		g_static_rec_mutex_unlock(&mutex);
		return NULL;
	}

	d(g_message("%s(%d): %s of '%s' ", __FILE__, __LINE__, __PRETTY_FUNCTION__, mapi_tzid));

	if (lru_mapi_id && !g_ascii_strcasecmp (mapi_tzid, lru_mapi_id)) {
		g_static_rec_mutex_unlock(&mutex);
		return lru_ical_id;
	}

	lru_ical_id = lru_mapi_id = NULL;
	if ((lru_ical_id = g_hash_table_lookup (mapi_to_ical, mapi_tzid)) != NULL)
		lru_mapi_id = mapi_tzid;

	g_static_rec_mutex_unlock(&mutex);

	return lru_ical_id;
}

void
e_cal_backend_mapi_tz_util_destroy ()
{
	g_static_rec_mutex_lock(&mutex);
	if (!(mapi_to_ical && ical_to_mapi)) {
		g_static_rec_mutex_unlock(&mutex);
		return;
	}

	g_hash_table_destroy (mapi_to_ical);
	g_hash_table_destroy (ical_to_mapi);

	/* Reset all the values */
	mapi_to_ical = NULL;
	ical_to_mapi = NULL;

	lru_mapi_id = NULL;
	lru_ical_id = NULL;

	g_static_rec_mutex_unlock(&mutex);
}

static void 
file_contents_to_hashtable (const char *contents, GHashTable *table) 
{
	gchar **array = NULL;
	guint len = 0, i;

	array = g_strsplit (contents, "\n", -1);
	len = g_strv_length (array);

	for (i=0; i < len-1; ++i) {
		gchar **mapping = g_strsplit (array[i], MAPPING_SEPARATOR, -1);
		if (g_strv_length (mapping) == 2) 
			g_hash_table_insert (table, g_strdup (mapping[0]), g_strdup (mapping[1]));
		g_strfreev (mapping);
	}

	g_strfreev (array);
}

gboolean
e_cal_backend_mapi_tz_util_populate ()
{
	gchar *mtoi_fn = NULL, *itom_fn = NULL;
	GMappedFile *mtoi_mf = NULL, *itom_mf = NULL;

	g_static_rec_mutex_lock(&mutex);
	if (mapi_to_ical && ical_to_mapi) {
		g_static_rec_mutex_unlock(&mutex);
		return TRUE;
	}

	mtoi_fn = g_build_filename (MAPI_DATADIR, "tz-mapi-to-ical", NULL);
	itom_fn = g_build_filename (MAPI_DATADIR, "tz-ical-to-mapi", NULL);

	mtoi_mf = g_mapped_file_new (mtoi_fn, FALSE, NULL);
	itom_mf = g_mapped_file_new (itom_fn, FALSE, NULL);

	g_free (mtoi_fn);
	g_free (itom_fn);

	if (!(mtoi_mf && itom_mf)) {
		g_warning ("Could not map Exchange MAPI timezone files.");

		if (mtoi_mf)
			g_mapped_file_free (mtoi_mf);
		if (itom_mf)
			g_mapped_file_free (itom_mf);

		g_static_rec_mutex_unlock(&mutex);
		return FALSE;
	}

	mapi_to_ical = g_hash_table_new_full   ((GHashFunc) g_str_hash, 
						(GEqualFunc) g_str_equal, 
						(GDestroyNotify) g_free, 
						(GDestroyNotify) g_free);

	file_contents_to_hashtable (g_mapped_file_get_contents (mtoi_mf), mapi_to_ical);

	ical_to_mapi = g_hash_table_new_full   ((GHashFunc) g_str_hash, 
						(GEqualFunc) g_str_equal, 
						(GDestroyNotify) g_free, 
						(GDestroyNotify) g_free);

	file_contents_to_hashtable (g_mapped_file_get_contents (itom_mf), ical_to_mapi);

	if (!(g_hash_table_size (mapi_to_ical) && g_hash_table_size (ical_to_mapi))) {
		g_warning ("Exchange MAPI timezone files are not valid.");

		e_cal_backend_mapi_tz_util_destroy ();

		g_mapped_file_free (mtoi_mf);
		g_mapped_file_free (itom_mf);

		g_static_rec_mutex_unlock(&mutex);
		return FALSE;
	} 

	g_mapped_file_free (mtoi_mf);
	g_mapped_file_free (itom_mf);

	g_static_rec_mutex_unlock(&mutex);
	return TRUE;
}

static void 
e_cal_backend_mapi_tz_util_dump_ical_tzs ()
{
	guint i;
	icalarray *zones;
	GList *l, *list_items = NULL;
	
	/* Get the array of builtin timezones. */
	zones = icaltimezone_get_builtin_timezones ();

	g_message("%s(%d): %s: ", __FILE__, __LINE__, __PRETTY_FUNCTION__);
	for (i = 0; i < zones->num_elements; i++) {
		icaltimezone *zone;
		char *tzid = NULL;

		zone = icalarray_element_at (zones, i);

		tzid = icaltimezone_get_tzid (zone);

		list_items = g_list_prepend (list_items, tzid);
	}

	list_items = g_list_sort (list_items, (GCompareFunc) g_ascii_strcasecmp);

	/* Put the "UTC" entry at the top of the combo's list. */
	list_items = g_list_prepend (list_items, "UTC");

	for (l = list_items, i = 0; l != NULL; l = l->next, ++i) 
		g_print ("[%3d]\t%s\n", (i+1), (gchar *)(l->data));

//	icaltimezone_free_builtin_timezones ();

	g_list_free (list_items);
}

void
e_cal_backend_mapi_tz_util_dump ()
{
	guint i;
	GList *keys, *values, *l, *m;

	g_static_rec_mutex_lock(&mutex);

	e_cal_backend_mapi_tz_util_dump_ical_tzs ();

	if (!(mapi_to_ical && ical_to_mapi)) {
		g_static_rec_mutex_unlock(&mutex);
		return;
	}

	g_message("%s(%d): %s: ", __FILE__, __LINE__, __PRETTY_FUNCTION__);

	g_message ("Dumping #table mapi_to_ical");
	keys = g_hash_table_get_keys (mapi_to_ical);
	values = g_hash_table_get_values (mapi_to_ical);
	l = g_list_first (keys);
	m = g_list_first (values);
	for (i=0; l && m; ++i, l=l->next, m=m->next)
		g_print ("[%3d]\t%s\t%s\t%s\n", (i+1), (gchar *)(l->data), MAPPING_SEPARATOR, (gchar *)(m->data));
	g_message ("Dumping differences in #tables");
	l = g_list_first (keys);
	m = g_list_first (values);
	for (i=0; l && m; ++i, l=l->next, m=m->next)
		if (g_ascii_strcasecmp ((gchar *)(l->data), (gchar *) g_hash_table_lookup (ical_to_mapi, (m->data))))
			g_print ("[%3d] Possible mis-match for %s\n", (i+1), (gchar *)(l->data));
	g_list_free (keys);
	g_list_free (values);

	g_message ("Dumping #table ical_to_mapi");
	keys = g_hash_table_get_keys (ical_to_mapi);
	values = g_hash_table_get_values (ical_to_mapi);
	l = g_list_first (keys);
	m = g_list_first (values);
	for (i=0; l && m; ++i, l=l->next, m=m->next)
		g_print ("[%3d]\t%s\t%s\t%s\n", (i+1), (gchar *)(l->data), MAPPING_SEPARATOR, (gchar *)(m->data));
	g_list_free (keys);
	g_list_free (values);

	g_static_rec_mutex_unlock(&mutex);
}

#if 0
const WORD TZRULE_FLAG_RECUR_CURRENT_TZREG  = 0x0001; // see dispidApptTZDefRecur
const WORD TZRULE_FLAG_EFFECTIVE_TZREG      = 0x0002;

// Allocates return value with new.
// clean up with delete[].
TZDEFINITION* BinToTZDEFINITION(ULONG cbDef, LPBYTE lpbDef)
{
    if (!lpbDef) return NULL;

    // Update this if parsing code is changed!
    // this checks the size up to the flags member
    if (cbDef < 2*sizeof(BYTE) + 2*sizeof(WORD)) return NULL;

    TZDEFINITION tzDef;
    TZRULE* lpRules = NULL;
    LPBYTE lpPtr = lpbDef;
    WORD cchKeyName = 0;
    WCHAR* szKeyName = NULL;
    WORD i = 0;

    BYTE bMajorVersion = *((BYTE*)lpPtr);
    lpPtr += sizeof(BYTE);
    BYTE bMinorVersion = *((BYTE*)lpPtr);
    lpPtr += sizeof(BYTE);

    // We only understand TZ_BIN_VERSION_MAJOR
    if (TZ_BIN_VERSION_MAJOR != bMajorVersion) return NULL;

    // We only understand if >= TZ_BIN_VERSION_MINOR
    if (TZ_BIN_VERSION_MINOR > bMinorVersion) return NULL;

    WORD cbHeader = *((WORD*)lpPtr);
    lpPtr += sizeof(WORD);

    tzDef.wFlags = *((WORD*)lpPtr);
    lpPtr += sizeof(WORD);

    if (TZDEFINITION_FLAG_VALID_GUID & tzDef.wFlags)
    {
        if (lpbDef + cbDef - lpPtr < sizeof(GUID)) return NULL;
        tzDef.guidTZID = *((GUID*)lpPtr);
        lpPtr += sizeof(GUID);
    }

    if (TZDEFINITION_FLAG_VALID_KEYNAME & tzDef.wFlags)
    {
        if (lpbDef + cbDef - lpPtr < sizeof(WORD)) return NULL;
        cchKeyName = *((WORD*)lpPtr);
        lpPtr += sizeof(WORD);
        if (cchKeyName)
        {
            if (lpbDef + cbDef - lpPtr < (BYTE)sizeof(WORD)*cchKeyName) return NULL;
            szKeyName = (WCHAR*)lpPtr;
            lpPtr += cchKeyName*sizeof(WORD);
        }
    }

    if (lpbDef+ cbDef - lpPtr < sizeof(WORD)) return NULL;
    tzDef.cRules = *((WORD*)lpPtr);
    lpPtr += sizeof(WORD);

    /* FIXME: parse rules */
    if (tzDef.cRules) tzDef.cRules = 0;
#if 0
    if (tzDef.cRules)
    {
        lpRules = new TZRULE[tzDef.cRules];
        if (!lpRules) return NULL;

        LPBYTE lpNextRule = lpPtr;
        BOOL bRuleOK = false;
		
        for (i = 0;i < tzDef.cRules;i++)
        {
            bRuleOK = false;
            lpPtr = lpNextRule;
			
            if (lpbDef + cbDef - lpPtr < 
                2*sizeof(BYTE) + 2*sizeof(WORD) + 3*sizeof(long) + 2*sizeof(SYSTEMTIME)) return NULL;
            bRuleOK = true;
            BYTE bRuleMajorVersion = *((BYTE*)lpPtr);
            lpPtr += sizeof(BYTE);
            BYTE bRuleMinorVersion = *((BYTE*)lpPtr);
            lpPtr += sizeof(BYTE);
			
            // We only understand TZ_BIN_VERSION_MAJOR
            if (TZ_BIN_VERSION_MAJOR != bRuleMajorVersion) return NULL;
			
            // We only understand if >= TZ_BIN_VERSION_MINOR
            if (TZ_BIN_VERSION_MINOR > bRuleMinorVersion) return NULL;
			
            WORD cbRule = *((WORD*)lpPtr);
            lpPtr += sizeof(WORD);
			
            lpNextRule = lpPtr + cbRule;
			
            lpRules[i].wFlags = *((WORD*)lpPtr);
            lpPtr += sizeof(WORD);
			
            lpRules[i].stStart = *((SYSTEMTIME*)lpPtr);
            lpPtr += sizeof(SYSTEMTIME);
			
            lpRules[i].TZReg.lBias = *((long*)lpPtr);
            lpPtr += sizeof(long);
            lpRules[i].TZReg.lStandardBias = *((long*)lpPtr);
            lpPtr += sizeof(long);
            lpRules[i].TZReg.lDaylightBias = *((long*)lpPtr);
            lpPtr += sizeof(long);
			
            lpRules[i].TZReg.stStandardDate = *((SYSTEMTIME*)lpPtr);
            lpPtr += sizeof(SYSTEMTIME);
            lpRules[i].TZReg.stDaylightDate = *((SYSTEMTIME*)lpPtr);
            lpPtr += sizeof(SYSTEMTIME);
        }
        if (!bRuleOK)
        {
            delete[] lpRules;
            return NULL;			
        }
    }
#endif 
    // Now we've read everything - allocate a structure and copy it in
    size_t cbTZDef = sizeof(TZDEFINITION) +
        sizeof(WCHAR)*(cchKeyName+1) +
        sizeof(TZRULE)*tzDef.cRules;

    TZDEFINITION* ptzDef = (TZDEFINITION*) malloc (cbTZDef);
    
    if (ptzDef)
    {
        // Copy main struct over
        *ptzDef = tzDef;
        lpPtr = (LPBYTE) ptzDef;
        lpPtr += sizeof(TZDEFINITION);

        if (szKeyName)
        {
            ptzDef->pwszKeyName = (WCHAR*)lpPtr;
            memcpy(lpPtr,szKeyName,cchKeyName*sizeof(WCHAR));
            ptzDef->pwszKeyName[cchKeyName] = 0;
            lpPtr += (cchKeyName+1)*sizeof(WCHAR);
        }

        if (ptzDef -> cRules)
        {
            ptzDef -> rgRules = (TZRULE*)lpPtr;
            for (i = 0;i < ptzDef -> cRules;i++)
            {
                ptzDef -> rgRules[i] = lpRules[i];
            }
        }
    }
//    delete[] lpRules;

   free (ptzDef);
   ptzDef = NULL;

    return ptzDef;
}
#endif

#define TZDEFINITION_FLAG_VALID_GUID     0x0001 // the guid is valid
#define TZDEFINITION_FLAG_VALID_KEYNAME  0x0002 // the keyname is valid
#define TZ_MAX_RULES          1024 
#define TZ_BIN_VERSION_MAJOR  0x02 
#define TZ_BIN_VERSION_MINOR  0x01 

void
e_cal_backend_mapi_util_mapi_tz_to_bin (const char *mapi_tzid, struct SBinary *sb)
{
	GByteArray *ba;
	guint8 flag8;
	guint16 flag16;
	gunichar2 *buf;
	glong items_written;
	guint32 i;

	ba = g_byte_array_new ();

	/* UTF-8 length of the keyname */
	flag16 = g_utf8_strlen (mapi_tzid, -1);
	ba = g_byte_array_append (ba, &flag16, sizeof (guint16));
	/* Keyname */
	buf = g_utf8_to_utf16 (mapi_tzid, flag16, NULL, &items_written, NULL);
	ba = g_byte_array_append (ba, buf, (sizeof (gunichar2) * items_written));
	g_free (buf);

	/* number of rules *//* FIXME: Need to support rules */
	flag16 = 0x0000;
	ba = g_byte_array_append (ba, &flag16, sizeof (guint16));

	/* wFlags: we know only keyname based names */
	flag16 = TZDEFINITION_FLAG_VALID_KEYNAME;
	ba = g_byte_array_prepend (ba, &flag16, sizeof (guint16));

	/* Length in bytes until rules info */
	flag16 = (guint16) (ba->len);
	ba = g_byte_array_prepend (ba, &flag16, sizeof (guint16));

	/* Minor version */
	flag8 = TZ_BIN_VERSION_MINOR;
	ba = g_byte_array_prepend (ba, &flag8, sizeof (guint8));

	/* Major version */
	flag8 = TZ_BIN_VERSION_MAJOR;
	ba = g_byte_array_prepend (ba, &flag8, sizeof (guint8));

	/* Rules may now be appended here */

	sb->lpb = ba->data;
	sb->cb = ba->len;

	d(g_message ("New timezone stream.. Length: %d bytes.. Hex-data follows:", ba->len));
	d(for (i = 0; i < ba->len; i++) 
		g_print("0x%.2X ", ba->data[i]));

	g_byte_array_free (ba, FALSE);
}

gchar *
e_cal_backend_mapi_util_bin_to_mapi_tz (GByteArray *ba)
{
	guint8 flag8;
	guint16 flag16, cbHeader = 0;
	guint8 *ptr = ba->data;
//	guint len = ba->len;
	gchar *buf = NULL;

	d(g_message ("New timezone stream.. Length: %d bytes.. Info follows:", ba->len));

	/* Major version */
	flag8 = *((guint8 *)ptr);
	ptr += sizeof (guint8);
	d(g_print ("Major version: %d\n", flag8));
	if (TZ_BIN_VERSION_MAJOR != flag8)
		return NULL;

	/* Minor version */
	flag8 = *((guint8 *)ptr);
	ptr += sizeof (guint8);
	d(g_print ("Minor version: %d\n", flag8));
	if (TZ_BIN_VERSION_MINOR > flag8)
		return NULL;

	/* Length in bytes until rules info */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	d(g_print ("Length in bytes until rules: %d\n", flag16));
	cbHeader = flag16;

	/* wFlags: we don't yet understand GUID based names */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	d(g_print ("wFlags: %d\n", flag16));
	cbHeader -= sizeof (guint16);
	if (TZDEFINITION_FLAG_VALID_KEYNAME != flag16)
		return NULL;

	/* UTF-8 length of the keyname */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	d(g_print ("UTF8 length of keyname: %d\n", flag16));
	cbHeader -= sizeof (guint16);

	/* number of rules is at the end of the header.. we'll parse and use later */
	cbHeader -= sizeof (guint16);

	/* Keyname */
	buf = g_utf16_to_utf8 ((const gunichar2 *)ptr, cbHeader/sizeof (gunichar2), NULL, NULL, NULL);
	ptr += cbHeader;
	d(g_print ("Keyname: %s\n", buf));

	/* number of rules */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	d(g_print ("Number of rules: %d\n", flag16));

	/* FIXME: Need to support rules */

	return buf;
}

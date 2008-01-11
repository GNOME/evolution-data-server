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



#ifndef E_CAL_BACKEND_MAPI_TZ_UTILS_H
#define E_CAL_BACKEND_MAPI_TZ_UTILS_H

#include <glib.h>

G_BEGIN_DECLS

const gchar *
e_cal_backend_mapi_tz_util_get_mapi_equivalent (const gchar *ical_tzid);

const gchar *
e_cal_backend_mapi_tz_util_get_ical_equivalent (const gchar *mapi_tzid);

gboolean
e_cal_backend_mapi_tz_util_populate (void);

void
e_cal_backend_mapi_tz_util_destroy (void);

void
e_cal_backend_mapi_tz_util_dump (void);

void
e_cal_backend_mapi_util_mapi_tz_to_bin (const char *mapi_tzid, struct SBinary *sb);

gchar *
e_cal_backend_mapi_util_bin_to_mapi_tz (GByteArray *ba);

G_END_DECLS

#if 0
typedef int16_t WORD;
typedef int8_t BYTE;
typedef uint32_t GUID;
typedef uint64_t ULONG;
typedef time_t SYSTEMTIME;
typedef uint8_t* LPBYTE;
typedef char* LPWSTR;
typedef char WCHAR;

// TZREG
// =====================
//   This is an individual description that defines when a daylight
//   saving shift, and the return to standard time occurs, and how
//   far the shift is.  This is basically the same as
//   TIME_ZONE_INFORMATION documented in MSDN, except that the strings
//   describing the names "daylight" and "standard" time are omitted.
//
typedef struct RenTimeZone
{
    long        lBias;           // offset from GMT
    long        lStandardBias;   // offset from bias during standard time
    long        lDaylightBias;   // offset from bias during daylight time
    SYSTEMTIME  stStandardDate;  // time to switch to standard time
    SYSTEMTIME  stDaylightDate;  // time to switch to daylight time
} TZREG;

// TZRULE
// =====================
//   This structure represents both a description when a daylight. 
//   saving shift occurs, and in addition, the year in which that
//   timezone rule came into effect. 
//
typedef struct
{
    WORD        wFlags;   // indicates which rule matches legacy recur
    SYSTEMTIME  stStart;  // indicates when the rule starts
    TZREG       TZReg;    // the timezone info
} TZRULE;

// TZDEFINITION
// =====================
//   This represents an entire timezone including all historical, current
//   and future timezone shift rules for daylight saving time, etc.  It's
//   identified by a unique GUID.
//
typedef struct
{
    WORD     wFlags;       // indicates which fields are valid
    GUID     guidTZID;     // guid uniquely identifying this timezone
    LPWSTR   pwszKeyName;  // the name of the key for this timezone
    WORD     cRules;       // the number of timezone rules for this definition
    TZRULE*  rgRules;      // an array of rules describing when shifts occur
} TZDEFINITION;

// Allocates return value with new.
// clean up with delete[].
TZDEFINITION* BinToTZDEFINITION(ULONG cbDef, LPBYTE lpbDef);
#endif

#endif

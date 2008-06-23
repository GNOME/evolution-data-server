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
//#include "e-cal-backend-mapi-constants.h"
#include "e-cal-backend-mapi-recur-utils.h"

#define ZERO_BYTE 	0x00

/* preamble */
#define PREAMBLE 0x30043004

/** Pattern termination **/
#define REPEAT_UNTIL 	0x00002021
#define REPEAT_FOR_N 	0x00002022
#define REPEAT_FOREVER 	0x00002023

/** Outlook version indicator (?) **/
#define VERSION_PREAMBLE 0x00003006
/* This version-id is equivalent to Outlook 2007 */
#define VERSION_ID 	 0x00003009


/* terminal sequence (should add twice) */
#define TERMINAL_SEQ 	0x00000000

struct icaltimetype dt1601;
struct icaltimetype dt1970;

static icalrecurrencetype_weekday 
get_ical_weekday (uint32_t olWeekday) {
	switch (olWeekday) {
		case olSunday: 
			return ICAL_SUNDAY_WEEKDAY;
		case olMonday: 
			return ICAL_MONDAY_WEEKDAY;
		case olTuesday: 
			return ICAL_TUESDAY_WEEKDAY;
		case olWednesday: 
			return ICAL_WEDNESDAY_WEEKDAY;
		case olThursday: 
			return ICAL_THURSDAY_WEEKDAY;
		case olFriday: 
			return ICAL_FRIDAY_WEEKDAY;
		case olSaturday: 
			return ICAL_SATURDAY_WEEKDAY;
		default: 
			return ICAL_SUNDAY_WEEKDAY;
	}
}

gboolean
e_cal_backend_mapi_util_bin_to_rrule (GByteArray *ba, ECalComponent *comp)
{
	struct icalrecurrencetype rt;
	guint16 flag16;
	guint32 flag32;
	guint8 *ptr = ba->data;
	GSList l;
	gint i;

	icalrecurrencetype_clear (&rt);

	/* Major version */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (PREAMBLE != flag32)
		return FALSE;

	/* FREQUENCY */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	if (flag16 == 0x200A) {
		rt.freq = ICAL_DAILY_RECURRENCE;

		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);
		if (flag32 == 0x0) {
			/* Daily every N days */

			/* some crappy mod here */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32 / (24 * 60));

			/* some constant 0 for the stuff we handle */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;
		} else if (flag32 == 0x1) {
			/* Daily every weekday */

	/* NOTE: Evolution does not handle daily-every-weekday any different 
	 * from a weekly recurrence.
	 */
			rt.freq = ICAL_WEEKLY_RECURRENCE;

			/* some crappy mod here */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32);

			/* some constant 0 for the stuff we handle */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			/* BITMASK */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			i = 0;
			if (flag32 & olSunday)
				rt.by_day[i++] = ICAL_SUNDAY_WEEKDAY;
			if (flag32 & olMonday)
				rt.by_day[i++] = ICAL_MONDAY_WEEKDAY;
			if (flag32 & olTuesday)
				rt.by_day[i++] = ICAL_TUESDAY_WEEKDAY;
			if (flag32 & olWednesday)
				rt.by_day[i++] = ICAL_WEDNESDAY_WEEKDAY;
			if (flag32 & olThursday)
				rt.by_day[i++] = ICAL_THURSDAY_WEEKDAY;
			if (flag32 & olFriday)
				rt.by_day[i++] = ICAL_FRIDAY_WEEKDAY;
			if (flag32 & olSaturday)
				rt.by_day[i++] = ICAL_SATURDAY_WEEKDAY;
		}

	} else if (flag16 == 0x200B) {
		rt.freq = ICAL_WEEKLY_RECURRENCE;

		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);
		if (flag32 == 0x1) {
			/* weekly every N weeks (for all events and non-regenerating tasks) */

			/* some crappy mod here */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32);

			/* some constant 0 */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			/* BITMASK */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			i = 0;
			if (flag32 & olSunday)
				rt.by_day[i++] = ICAL_SUNDAY_WEEKDAY;
			if (flag32 & olMonday)
				rt.by_day[i++] = ICAL_MONDAY_WEEKDAY;
			if (flag32 & olTuesday)
				rt.by_day[i++] = ICAL_TUESDAY_WEEKDAY;
			if (flag32 & olWednesday)
				rt.by_day[i++] = ICAL_WEDNESDAY_WEEKDAY;
			if (flag32 & olThursday)
				rt.by_day[i++] = ICAL_THURSDAY_WEEKDAY;
			if (flag32 & olFriday)
				rt.by_day[i++] = ICAL_FRIDAY_WEEKDAY;
			if (flag32 & olSaturday)
				rt.by_day[i++] = ICAL_SATURDAY_WEEKDAY;
		} else if (flag32 == 0x0) {
			/* weekly every N weeks (for all regenerating tasks) */

			/* FIXME: we don't handle regenerating tasks */
			return FALSE;
		}

	} else if (flag16 == 0x200C) {
		rt.freq = ICAL_MONTHLY_RECURRENCE;

		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);

		if (flag32 == 0x2) {
			/* Monthly every N months on day D */

			/* some crappy mod here */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32);

			/* some constant 0 for the stuff we handle */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			/* MONTH_DAY */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.by_month_day[0] = (short) (flag32);
		} else if (flag32 == 0x3) {
			/* Monthly every N months on the Xth Y */

			/* some crappy mod here */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32);

			/* some constant 0 for the stuff we handle */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32 > 4)

		}

	} else if (flag16 == 0x200D) {
		rt.freq = ICAL_YEARLY_RECURRENCE;

		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);

		if (flag32 == 0x2) {
			/* Yearly on day D of month M */

			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32 / 12);

			/* some constant 0 for the stuff we handle */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
		} else if (flag32 == 0x3) {
			/* Yearly on the Xth Y of month M */
			g_warning ("Encountered a recurrence pattern Evolution cannot handle");

			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* INTERVAL */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			rt.interval = (short) (flag32 / 12);

			/* some constant 0 */
			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);
			if (flag32)
				return FALSE;

			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			flag32 = *((guint32 *)ptr);
			ptr += sizeof (guint32);

			/* TODO: Add support for this kinda recurrence in Evolution */
			return FALSE;
		}
	} else 
		return FALSE;

	/* repeat */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (flag32 == REPEAT_UNTIL) {
		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);
	} else if (flag32 == REPEAT_FOR_N) {
		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);

		rt.count = flag32;
	} else if (flag32 == REPEAT_FOREVER) {
		flag32 = *((guint32 *)ptr);
		ptr += sizeof (guint32);
		if (flag32)
			return FALSE;
	}

	/* week_start */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	rt.week_start = get_ical_weekday (flag32);
g_print ("week start %x\n", flag32);

	/* number of exceptions */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	/* fixme: exceptions */
	if (flag32) 
		ptr += flag32 * sizeof (guint32);
g_print ("excpt no %x\n", flag32);

	/* number of changed exceptions */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	/* fixme: exceptions */
	if (flag32) 
		ptr += flag32 * sizeof (guint32);
g_print ("changed excpt %x\n", flag32);
	
	/* start date */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
g_print ("start date %x\n", flag32);

	/* end date */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
g_print ("end date %x\n", flag32);

	/* some constant */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (flag32 != VERSION_PREAMBLE)
		return FALSE;
g_print ("constant %x\n", flag32);

	/* version info */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (flag32 != VERSION_ID)
		return FALSE;
g_print ("ver info %x\n", flag32);

	/* start time in mins */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
g_print ("start time in mins %x\n", flag32);

	/* end time in mins */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
g_print ("end time in mins %x\n", flag32);

	/* exceptions */
	flag16 = *((guint16 *)ptr);
	ptr += sizeof (guint16);
	if (flag16 != 0x0)
		return FALSE;
g_print ("excpt no %x\n", flag16);

	/* term seq */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (flag32 != TERMINAL_SEQ)
		return FALSE;
g_print ("term seq 1 %x\n", flag32);

	/* term seq */
	flag32 = *((guint32 *)ptr);
	ptr += sizeof (guint32);
	if (flag32 != TERMINAL_SEQ)
		return FALSE;

	/* Set the recurrence */

	l.data = &rt;
	l.next = NULL;

	e_cal_component_set_rrule_list (comp, &l);

	g_print ("\n\nyipppeee... parsed the blob..\n\n");

	return TRUE;
}

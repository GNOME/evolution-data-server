/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

/* This is not a real test, it only prints expected and received
   recurrence descriptions and the most it verifies that something
   had been returned, instead of nothing and vice versa. The actual
   comparison of the expected value and the result is for humans. */

typedef struct _EEmptyFixture {
	gboolean dummy;
} EEmptyFixture;

static void
e_empty_test_setup (EEmptyFixture *fixture,
		    gconstpointer user_data)
{
}

static void
e_empty_test_teardown (EEmptyFixture *fixture,
		       gconstpointer user_data)
{
}

#define EVENT_STR \
	"BEGIN:VEVENT\r\n" \
	"UID:1\r\n" \
	"SUMMARY:test\r\n" \
	"%s\r\n" \
	"END:VEVENT\r\n"

#define TASK_STR \
	"BEGIN:VTODO\r\n" \
	"UID:1\r\n" \
	"SUMMARY:test\r\n" \
	"%s\r\n" \
	"END:VTODO\r\n"

#define MEMO_STR \
	"BEGIN:VJOURNAL\r\n" \
	"UID:1\r\n" \
	"SUMMARY:test\r\n" \
	"%s\r\n" \
	"END:VJOURNAL\r\n"

#define TEXT_EXPECTED TRUE
#define NULL_EXPECTED FALSE
#define CUSTOM_RECURRENCE "RRULE:FREQ=MONTHLY;COUNT=2;BYHOUR=1"
#define MEETING_STR "ORGANIZER:MAILTO:organizer@nowhere\r\nATTENDEE;CUTYPE=INDIVIDUAL;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION:MAILTO:user@no.where\r\n"
#define EXCEPTION_STR "EXDATE;VALUE=DATE:20180216\r\n"
#define EXCEPTIONS_STR "EXDATE;VALUE=DATE:20180216\r\nEXDATE;VALUE=DATE:20180218\r\n"
#define BOTH_FLAGS (E_CAL_RECUR_DESCRIBE_RECURRENCE_FLAG_PREFIXED | E_CAL_RECUR_DESCRIBE_RECURRENCE_FLAG_FALLBACK)
#define FALLBACK_FLAG E_CAL_RECUR_DESCRIBE_RECURRENCE_FLAG_FALLBACK
#define PREFIXED_FLAG E_CAL_RECUR_DESCRIBE_RECURRENCE_FLAG_PREFIXED

static void
test_recur_description (EEmptyFixture *fixture,
			gconstpointer user_data)
{
	struct _tests {
		const gchar *description;
		guint32 flags;
		gboolean expects;
		const gchar *comp_str;
		const gchar *add_content;
	} tests[] = {
		{ "Event without recurrence", 0, NULL_EXPECTED, EVENT_STR, "" },
		{ "Task without recurrence",  0, NULL_EXPECTED, TASK_STR, "" },
		{ "Memo without recurrence",  0, NULL_EXPECTED, MEMO_STR, "" },
		{ "Event without recurrence", PREFIXED_FLAG, NULL_EXPECTED, EVENT_STR, "" },
		{ "Task without recurrence",  PREFIXED_FLAG, NULL_EXPECTED, TASK_STR, "" },
		{ "Memo without recurrence",  PREFIXED_FLAG, NULL_EXPECTED, MEMO_STR, "" },
		{ "Event without recurrence", FALLBACK_FLAG, NULL_EXPECTED, EVENT_STR, "" },
		{ "Task without recurrence",  FALLBACK_FLAG, NULL_EXPECTED, TASK_STR, "" },
		{ "Memo without recurrence",  FALLBACK_FLAG, NULL_EXPECTED, MEMO_STR, "" },
		{ "Event with custom recurrence", 0, NULL_EXPECTED, EVENT_STR, CUSTOM_RECURRENCE },
		{ "Task with custom recurrence",  0, NULL_EXPECTED, TASK_STR, CUSTOM_RECURRENCE },
		{ "Memo with custom recurrence",  0, NULL_EXPECTED, MEMO_STR, CUSTOM_RECURRENCE },
		{ "Event with custom recurrence, with prefixed flag", PREFIXED_FLAG, NULL_EXPECTED, EVENT_STR, CUSTOM_RECURRENCE },
		{ "Task with custom recurrence, with prefixed flag",  PREFIXED_FLAG, NULL_EXPECTED, TASK_STR, CUSTOM_RECURRENCE },
		{ "Memo with custom recurrence, with prefixed flag",  PREFIXED_FLAG, NULL_EXPECTED, MEMO_STR, CUSTOM_RECURRENCE },
		{ "The appointment recurs", FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, CUSTOM_RECURRENCE },
		{ "The meeting recurs",  FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, MEETING_STR CUSTOM_RECURRENCE },
		{ "The task recurs",  FALLBACK_FLAG, TEXT_EXPECTED, TASK_STR, CUSTOM_RECURRENCE },
		{ "The memo recurs",  FALLBACK_FLAG, TEXT_EXPECTED, MEMO_STR, CUSTOM_RECURRENCE },
		{ "The appointment recurs", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, CUSTOM_RECURRENCE },
		{ "The meeting recurs", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, MEETING_STR CUSTOM_RECURRENCE },
		{ "The task recurs",  BOTH_FLAGS, TEXT_EXPECTED, TASK_STR, CUSTOM_RECURRENCE },
		{ "The memo recurs",  BOTH_FLAGS, TEXT_EXPECTED, MEMO_STR, CUSTOM_RECURRENCE },
		{ "Every 3 days for 2 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", 0, TEXT_EXPECTED, EVENT_STR, MEETING_STR "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", 0, TEXT_EXPECTED, TASK_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", 0, TEXT_EXPECTED, MEMO_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, MEETING_STR "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", FALLBACK_FLAG, TEXT_EXPECTED, TASK_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 3 days for 2 occurrences", FALLBACK_FLAG, TEXT_EXPECTED, MEMO_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The appointment recurs every 3 days for 2 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The meeting recurs every 3 days for 2 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, MEETING_STR "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The task recurs every 3 days for 2 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, TASK_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The memo recurs every 3 days for 2 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, MEMO_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The appointment recurs every 3 days for 2 occurrences", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The meeting recurs every 3 days for 2 occurrences", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, MEETING_STR "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The task recurs every 3 days for 2 occurrences", BOTH_FLAGS, TEXT_EXPECTED, TASK_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "The memo recurs every 3 days for 2 occurrences", BOTH_FLAGS, TEXT_EXPECTED, MEMO_STR, "RRULE:FREQ=DAILY;COUNT=2;INTERVAL=3" },
		{ "Every 4 days until Thu 03/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;UNTIL=20180301;INTERVAL=4" },
		{ "Every 5 days forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;INTERVAL=5" },
		{ "Every 2 weeks on Thursday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TH" },
		{ "Every week on Monday and Wednesday for 2 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;COUNT=2;BYDAY=MO,WE" },
		{ "Every 3 weeks on Tuesday, Friday and Saturday until Thu 03/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;UNTIL=20180301;INTERVAL=3;BYDAY=TU,FR,SA" },
		{ "Every 4 weeks on Tuesday, Thursday, Friday and Saturday until Thu 03/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;UNTIL=20180301;INTERVAL=4;BYDAY=TU,TH,FR,SA" },
		{ "Every 5 weeks on Tuesday, Wednesday, Thursday, Friday and Saturday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;INTERVAL=5;BYDAY=TU,WE,TH,FR,SA" },
		{ "Every 6 weeks on Monday, Tuesday, Wednesday, Thursday, Friday and Saturday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;INTERVAL=6;BYDAY=MO,TU,WE,TH,FR,SA" },
		{ "Every 7 weeks on Monday, Tuesday, Wednesday, Thursday, Friday, Saturday and Sunday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;INTERVAL=7;BYDAY=SU,MO,TU,WE,TH,FR,SA" },
		{ "Every week on Monday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=MO" },
		{ "Every week on Tuesday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=TU" },
		{ "Every week on Wednesday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=WE" },
		{ "Every week on Thursday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=TH" },
		{ "Every week on Friday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=FR" },
		{ "Every week on Saturday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=SA" },
		{ "Every week on Sunday forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;BYDAY=SU" },
		{ "Every week on Thursday until Thu 03/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=WEEKLY;UNTIL=20180301\r\nDTSTART:20180215T150000Z" },
		{ "Every month on the 1st day for one occurrence", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;COUNT=1;BYMONTHDAY=1" },
		{ "Every 2 months on the first Monday for 3 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;COUNT=3;INTERVAL=2;BYDAY=MO;BYSETPOS=1" },
		{ "Every 2 months on the second Tuesday for 3 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;COUNT=3;INTERVAL=2;BYDAY=TU;BYSETPOS=2" },
		{ "Every 2 months on the third Wednesday for 3 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;COUNT=3;INTERVAL=2;BYDAY=WE;BYSETPOS=3" },
		{ "Every month on the fourth Thursday until Tue 05/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;UNTIL=20180501;BYDAY=TH;BYSETPOS=4" },
		{ "Every month on the fifth Friday until Tue 05/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;UNTIL=20180501;BYDAY=FR;BYSETPOS=5" },
		{ "Every month on the last Saturday until Tue 05/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;UNTIL=20180501;BYDAY=SA;BYSETPOS=-1" },
		{ "Every month on the third Sunday until Tue 05/01/2018", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;UNTIL=20180501;BYDAY=SU;BYSETPOS=3" },
		{ "Every 2 months on the 13th day forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;INTERVAL=2;BYMONTHDAY=13" },
		{ "Every 3 months on the 26th day forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=MONTHLY;INTERVAL=3;BYMONTHDAY=26" },
		{ "Every 3 years forever", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=YEARLY;INTERVAL=3" },
		{ "Every year for 3 occurrences", 0, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=YEARLY;COUNT=3" },
		{ "The appointment recurs every day for 2 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, "RRULE:FREQ=DAILY;COUNT=2" },
		{ "The meeting recurs every 2 weeks on Wednesday and Thursday for 3 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, MEETING_STR "RRULE:FREQ=WEEKLY;COUNT=3;INTERVAL=2;BYDAY=WE,TH" },
		{ "The task recurs every 2 months on the 21st day for 3 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, TASK_STR, "RRULE:FREQ=MONTHLY;COUNT=3;INTERVAL=2;BYMONTHDAY=21" },
		{ "The memo recurs every 2 years for 3 occurrences", PREFIXED_FLAG, TEXT_EXPECTED, MEMO_STR, "RRULE:FREQ=YEARLY;COUNT=3;INTERVAL=2" },
		{ "Every 2 years for 3 occurrences, with 2 exceptions", 0, TEXT_EXPECTED, EVENT_STR, EXCEPTIONS_STR "RRULE:FREQ=YEARLY;COUNT=3;INTERVAL=2" },
		{ "Every 2 weeks on Thursday and Saturday for 3 occurrences, with one exception", 0, TEXT_EXPECTED, EVENT_STR, EXCEPTION_STR "RRULE:FREQ=WEEKLY;COUNT=3;INTERVAL=2;BYDAY=TH,SA" },
		{ "Every 2 years for 3 occurrences, with 2 exceptions", FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, EXCEPTIONS_STR "RRULE:FREQ=YEARLY;COUNT=3;INTERVAL=2" },
		{ "Every 2 weeks on Thursday and Saturday for 3 occurrences, with one exception", FALLBACK_FLAG, TEXT_EXPECTED, EVENT_STR, EXCEPTION_STR "RRULE:FREQ=WEEKLY;COUNT=3;INTERVAL=2;BYDAY=TH,SA" },
		{ "The appointment recurs every 2 years for 3 occurrences, with 2 exceptions", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, EXCEPTIONS_STR "RRULE:FREQ=YEARLY;COUNT=3;INTERVAL=2" },
		{ "The appointment recurs every 2 weeks on Thursday and Saturday for 3 occurrences, with one exception", PREFIXED_FLAG, TEXT_EXPECTED, EVENT_STR, EXCEPTION_STR "RRULE:FREQ=WEEKLY;COUNT=3;INTERVAL=2;BYDAY=TH,SA" },
		{ "The appointment recurs every 2 years for 3 occurrences, with 2 exceptions", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, EXCEPTIONS_STR "RRULE:FREQ=YEARLY;COUNT=3;INTERVAL=2" },
		{ "The appointment recurs every 2 weeks on Thursday and Saturday for 3 occurrences, with one exception", BOTH_FLAGS, TEXT_EXPECTED, EVENT_STR, EXCEPTION_STR "RRULE:FREQ=WEEKLY;COUNT=3;INTERVAL=2;BYDAY=TH,SA" }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (tests); ii++) {
		gchar *description;
		gchar *comp_str;
		icalcomponent *icalcomp;

		comp_str = g_strdup_printf (tests[ii].comp_str, tests[ii].add_content);
		icalcomp = icalcomponent_new_from_string (comp_str);
		g_assert_nonnull (icalcomp);
		g_free (comp_str);

		description = e_cal_recur_describe_recurrence (icalcomp, G_DATE_MONDAY, tests[ii].flags);
		if (tests[ii].expects == TEXT_EXPECTED) {
			g_assert_nonnull (description);

			if (g_strcmp0 (tests[ii].description, description) != 0) {
				printf ("[%d]: Expected '%s' with %s flags and %s, but got '%s'\n", ii, tests[ii].description,
					tests[ii].flags == BOTH_FLAGS ? "BOTH" :
					tests[ii].flags == PREFIXED_FLAG ? "PREFIXED" :
					tests[ii].flags == FALLBACK_FLAG ? "FALLBACK" : "no",
					icalcomponent_isa (icalcomp) == ICAL_VEVENT_COMPONENT ? "VEVENT" :
					icalcomponent_isa (icalcomp) == ICAL_VTODO_COMPONENT ? "VTODO" :
					icalcomponent_isa (icalcomp) == ICAL_VJOURNAL_COMPONENT ? "VJOURNAL" : "???",
					description);
			}
		} else {
			g_assert_null (description);
		}

		icalcomponent_free (icalcomp);
		g_free (description);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://bugzilla.gnome.org/");

	g_test_add (
		"/ECal/RecurDescription",
		EEmptyFixture,
		NULL,
		e_empty_test_setup,
		test_recur_description,
		e_empty_test_teardown);

	return g_test_run ();
}

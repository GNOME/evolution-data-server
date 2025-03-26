/* Miscellaneous time-related utilities
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Federico Mena <federico@ximian.com>
 *          Miguel de Icaza <miguel@ximian.com>
 *          Damon Chaplin <damon@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <ctype.h>

#include "e-cal-check-timezones.h"
#include "e-timezone-cache.h"

#include "e-cal-time-util.h"

#ifdef G_OS_WIN32
#ifdef gmtime_r
#undef gmtime_r
#endif

/* The gmtime() in Microsoft's C library is MT-safe */
#define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#endif

#define REFORMATION_DAY 639787	/* First day of the reformation, counted from 1 Jan 1 */
#define MISSING_DAYS 11		/* They corrected out 11 days */
#define THURSDAY 4		/* First day of reformation */
#define SATURDAY 6		/* Offset value; 1 Jan 1 was a Saturday */
#define ISODATE_LENGTH 17 /* 4+2+2+1+2+2+2+1 + 1 */

/* Number of days in a month, using 0 (Jan) to 11 (Dec). For leap years,
 * add 1 to February (month 1). */
static const gint glob_days_in_month[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/**************************************************************************
 * time_t manipulation functions.
 *
 * NOTE: these use the Unix timezone functions like mktime() and localtime()
 * and so should not be used in Evolution. New Evolution code should use
 * ICalTime values rather than time_t values wherever possible.
 **************************************************************************/

/**
 * time_add_day:
 * @time: A time_t value.
 * @days: Number of days to add.
 *
 * Adds a day onto the time, using local time.
 * Note that if clocks go forward due to daylight savings time, there are
 * some non-existent local times, so the hour may be changed to make it a
 * valid time. This also means that it may not be wise to keep calling
 * time_add_day() to step through a certain period - if the hour gets changed
 * to make it valid time, any further calls to time_add_day() will also return
 * this hour, which may not be what you want.
 *
 * Returns: a time_t value containing @time plus the days added.
 */
time_t
time_add_day (time_t time,
              gint days)
{
	struct tm *tm;

	tm = localtime (&time);
	tm->tm_mday += days;
	tm->tm_isdst = -1;

	return mktime (tm);
}

/**
 * time_add_week:
 * @time: A time_t value.
 * @weeks: Number of weeks to add.
 *
 * Adds the given number of weeks to a time value.
 *
 * Returns: a time_t value containing @time plus the weeks added.
 */
time_t
time_add_week (time_t time,
               gint weeks)
{
	return time_add_day (time, weeks * 7);
}

/**
 * time_day_begin:
 * @t: A time_t value.
 *
 * Returns the start of the day, according to the local time.
 *
 * Returns: the time corresponding to the beginning of the day.
 */
time_t
time_day_begin (time_t t)
{
	struct tm tm;

	tm = *localtime (&t);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;

	return mktime (&tm);
}

/**
 * time_day_end:
 * @t: A time_t value.
 *
 * Returns the end of the day, according to the local time.
 *
 * Returns: the time corresponding to the end of the day.
 */
time_t
time_day_end (time_t t)
{
	struct tm tm;

	tm = *localtime (&t);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_mday++;
	tm.tm_isdst = -1;

	return mktime (&tm);
}

/**************************************************************************
 * time_t manipulation functions, using timezones in libical.
 *
 * NOTE: these are only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values rather than
 * time_t values wherever possible.
 **************************************************************************/

/**
 * time_add_day_with_zone:
 * @time: A time_t value.
 * @days: Number of days to add.
 * @zone: Timezone to use.
 *
 * Adds or subtracts a number of days to/from the given time_t value, using
 * the given timezone.
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: a time_t value containing @time plus the days added.
 */
time_t
time_add_day_with_zone (time_t time,
			gint days,
			const ICalTimezone *zone)
{
	ICalTime *tt;
	time_t res;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Add/subtract the number of days. */
	i_cal_time_adjust (tt, days, 0, 0, 0);

	/* Convert back to a time_t. */
	res = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone);

	g_object_unref (tt);

	return res;
}

/**
 * time_add_week_with_zone:
 * @time: A time_t value.
 * @weeks: Number of weeks to add.
 * @zone: Timezone to use.
 *
 * Adds or subtracts a number of weeks to/from the given time_t value, using
 * the given timezone.
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: a time_t value containing @time plus the weeks added.
 */
time_t
time_add_week_with_zone (time_t time,
			 gint weeks,
			 const ICalTimezone *zone)
{
	return time_add_day_with_zone (time, weeks * 7, zone);
}

/**
 * time_add_month_with_zone:
 * @time: A time_t value.
 * @months: Number of months to add.
 * @zone: Timezone to use.
 *
 * Adds or subtracts a number of months to/from the given time_t value, using
 * the given timezone.
 *
 * If the day would be off the end of the month (e.g. adding 1 month to
 * 30th January, would lead to an invalid day, 30th February), it moves it
 * down to the last day in the month, e.g. 28th Feb (or 29th in a leap year.)
 *
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: a time_t value containing @time plus the months added.
 */
time_t
time_add_month_with_zone (time_t time,
			  gint months,
			  const ICalTimezone *zone)
{
	ICalTime *tt;
	gint day, days_in_month;
	time_t res;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Add on the number of months. */
	i_cal_time_set_month (tt, i_cal_time_get_month (tt) + months);

	/* Save the day, and set it to 1, so we don't overflow into the next
	 * month. */
	day = i_cal_time_get_day (tt);
	i_cal_time_set_day (tt, 1);

	/* Normalize it, fixing any month overflow. */
	i_cal_time_normalize_inplace (tt);

	/* If we go past the end of a month, set it to the last day. */
	days_in_month = time_days_in_month (i_cal_time_get_year (tt), i_cal_time_get_month (tt) - 1);
	if (day > days_in_month)
		day = days_in_month;

	i_cal_time_set_day (tt, day);

	/* Convert back to a time_t. */
	res = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone);

	g_object_unref (tt);

	return res;
}

/**
 * time_year_begin_with_zone:
 * @time: A time_t value.
 * @zone: Timezone to use.
 *
 * Returns the start of the year containing the given time_t, using the given
 * timezone.
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: the beginning of the year.
 */
time_t
time_year_begin_with_zone (time_t time,
			   const ICalTimezone *zone)
{
	ICalTime *tt;
	time_t res;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Set it to the start of the year. */
	i_cal_time_set_month (tt, 1);
	i_cal_time_set_day (tt, 1);
	i_cal_time_set_hour (tt, 0);
	i_cal_time_set_minute (tt, 0);
	i_cal_time_set_second (tt, 0);

	/* Convert back to a time_t. */
	res = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone);

	g_object_unref (tt);

	return res;
}

/**
 * time_month_begin_with_zone:
 * @time: A time_t value.
 * @zone: Timezone to use.
 *
 * Returns the start of the month containing the given time_t, using the given
 * timezone.
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: the beginning of the month.
 */
time_t
time_month_begin_with_zone (time_t time,
			    const ICalTimezone *zone)
{
	ICalTime *tt;
	time_t res;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Set it to the start of the month. */
	i_cal_time_set_day (tt, 1);
	i_cal_time_set_hour (tt, 0);
	i_cal_time_set_minute (tt, 0);
	i_cal_time_set_second (tt, 0);

	/* Convert back to a time_t. */
	res = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone);

	g_object_unref (tt);

	return res;
}

/**
 * time_week_begin_with_zone:
 * @time: A time_t value.
 * @week_start_day: Day to use as the starting of the week.
 * @zone: Timezone to use.
 *
 * Returns the start of the week containing the given time_t, using the given
 * timezone. week_start_day should use the same values as mktime(),
 * i.e. 0 (Sun) to 6 (Sat).
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: the beginning of the week.
 */
time_t
time_week_begin_with_zone (time_t time,
			   gint week_start_day,
			   const ICalTimezone *zone)
{
	ICalTime *tt;
	gint weekday, offset;
	time_t res;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Get the weekday. */
	weekday = time_day_of_week (i_cal_time_get_day (tt), i_cal_time_get_month (tt) - 1, i_cal_time_get_year (tt));

	/* Calculate the current offset from the week start day. */
	offset = (weekday + 7 - week_start_day) % 7;

	/* Set it to the start of the month. */
	i_cal_time_set_day (tt, i_cal_time_get_day (tt) - offset);
	i_cal_time_set_hour (tt, 0);
	i_cal_time_set_minute (tt, 0);
	i_cal_time_set_second (tt, 0);

	/* Normalize it, to fix any overflow. */
	i_cal_time_normalize_inplace (tt);

	/* Convert back to a time_t. */
	res = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone);

	g_object_unref (tt);

	return res;
}

/**
 * time_day_begin_with_zone:
 * @time: A time_t value.
 * @zone: Timezone to use.
 *
 * Returns the start of the day containing the given time_t, using the given
 * timezone.
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: the beginning of the day.
 */
time_t
time_day_begin_with_zone (time_t time,
			  const ICalTimezone *zone)
{
	ICalTime *tt;
	time_t new_time;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Set it to the start of the day. */
	i_cal_time_set_hour (tt, 0);
	i_cal_time_set_minute (tt, 0);
	i_cal_time_set_second (tt, 0);

	/* Convert back to a time_t and make sure the time is in the past. */
	while (new_time = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone), new_time > time) {
		i_cal_time_adjust (tt, 0, -1, 0, 0);
	}

	g_object_unref (tt);

	return new_time;
}

/**
 * time_day_end_with_zone:
 * @time: A time_t value.
 * @zone: Timezone to use.
 *
 * Returns the end of the day containing the given time_t, using the given
 * timezone. (The end of the day is the start of the next day.)
 * NOTE: this function is only here to make the transition to the timezone
 * functions easier. New code should use ICalTime values and
 * i_cal_time_adjust() to add or subtract days, hours, minutes & seconds.
 *
 * Returns: the end of the day.
 */
time_t
time_day_end_with_zone (time_t time,
			const ICalTimezone *zone)
{
	ICalTime *tt;
	time_t new_time;

	/* Convert to an ICalTime. */
	tt = i_cal_time_new_from_timet_with_zone (time, FALSE, (ICalTimezone *) zone);

	/* Set it to the start of the next day. */
	i_cal_time_set_hour (tt, 0);
	i_cal_time_set_minute (tt, 0);
	i_cal_time_set_second (tt, 0);

	i_cal_time_adjust (tt, 1, 0, 0, 0);

	/* Convert back to a time_t and make sure the time is in the future. */
	while (new_time = i_cal_time_as_timet_with_zone (tt, (ICalTimezone *) zone), new_time <= time) {
		i_cal_time_adjust (tt, 0, 1, 0, 0);
	}

	g_object_unref (tt);

	return new_time;
}

/**
 * time_to_gdate_with_zone:
 * @date: Destination #GDate value.
 * @time: A time value.
 * @zone: (nullable): Desired timezone for destination @date, or %NULL if
 *    the UTC timezone is desired.
 *
 * Converts a time_t value to a #GDate structure using the specified timezone.
 * This is analogous to g_date_set_time() but takes the timezone into account.
 **/
void
time_to_gdate_with_zone (GDate *date,
			 time_t time,
			 const ICalTimezone *zone)
{
	ICalTime *tt;

	g_return_if_fail (date != NULL);
	g_return_if_fail (time != -1);

	tt = i_cal_time_new_from_timet_with_zone (
		time, FALSE,
		zone ? (ICalTimezone *) zone : i_cal_timezone_get_utc_timezone ());

	g_date_set_dmy (date, i_cal_time_get_day (tt), i_cal_time_get_month (tt), i_cal_time_get_year (tt));

	g_object_unref (tt);
}

/**************************************************************************
 * General time functions.
 **************************************************************************/

/**
 * time_days_in_month:
 * @year: The year.
 * @month: The month.
 *
 * Returns the number of days in the month. Year is the normal year, e.g. 2001.
 * Month is 0 (Jan) to 11 (Dec).
 *
 * Returns: number of days in the given month/year.
 */
gint
time_days_in_month (gint year,
                    gint month)
{
	gint days;

	g_return_val_if_fail (year >= 1900, 0);
	g_return_val_if_fail ((month >= 0) && (month < 12), 0);

	days = glob_days_in_month[month];
	if (month == 1 && time_is_leap_year (year))
		days++;

	return days;
}

/**
 * time_day_of_year:
 * @day: The day.
 * @month: The month.
 * @year: The year.
 *
 * Returns the 1-based day number within the year of the specified date.
 * Year is the normal year, e.g. 2001. Month is 0 to 11.
 *
 * Returns: the day of the year.
 */
gint
time_day_of_year (gint day,
                  gint month,
                  gint year)
{
	gint i;

	for (i = 0; i < month; i++) {
		day += glob_days_in_month[i];

		if (i == 1 && time_is_leap_year (year))
			day++;
	}

	return day;
}

/**
 * time_day_of_week:
 * @day: The day.
 * @month: The month.
 * @year: The year.
 *
 * Returns the day of the week for the specified date, 0 (Sun) to 6 (Sat).
 * For the days that were removed on the Gregorian reformation, it returns
 * Thursday. Year is the normal year, e.g. 2001. Month is 0 to 11.
 *
 * Returns: the day of the week for the given date.
 */
gint
time_day_of_week (gint day,
                  gint month,
                  gint year)
{
	gint n;

	n = (year - 1) * 365 + time_leap_years_up_to (year - 1)
	  + time_day_of_year (day, month, year);

	if (n < REFORMATION_DAY)
		return (n - 1 + SATURDAY) % 7;

	if (n >= (REFORMATION_DAY + MISSING_DAYS))
		return (n - 1 + SATURDAY - MISSING_DAYS) % 7;

	return THURSDAY;
}

/**
 * time_is_leap_year:
 * @year: The year.
 *
 * Returns whether the specified year is a leap year. Year is the normal year,
 * e.g. 2001.
 *
 * Returns: TRUE if the year is leap, FALSE if not.
 */
gboolean
time_is_leap_year (gint year)
{
	if (year <= 1752)
		return !(year % 4);
	else
		return (!(year % 4) && (year % 100)) || !(year % 400);
}

/**
 * time_leap_years_up_to:
 * @year: The year.
 *
 * Returns the number of leap years since year 1 up to (but not including) the
 * specified year. Year is the normal year, e.g. 2001.
 *
 * Returns: number of leap years.
 */
gint
time_leap_years_up_to (gint year)
{
	/* There is normally a leap year every 4 years, except at the turn of
	 * centuries since 1700. But there is a leap year on centuries since 1700
	 * which are divisible by 400. */
	return (year / 4
		- ((year > 1700) ? (year / 100 - 17) : 0)
		+ ((year > 1600) ? ((year - 1600) / 400) : 0));
}

/**
 * isodate_from_time_t:
 * @t: A time value.
 *
 * Creates an ISO 8601 UTC representation from a time value.
 *
 * Returns: String with the ISO 8601 representation of the UTC time.
 **/
gchar *
isodate_from_time_t (time_t t)
{
	gchar *ret;
	struct tm stm;
	const gchar fmt[] = "%04d%02d%02dT%02d%02d%02dZ";

	gmtime_r (&t, &stm);
	ret = g_malloc (ISODATE_LENGTH);
	g_snprintf (
		ret, ISODATE_LENGTH, fmt,
		(stm.tm_year + 1900),
		(stm.tm_mon + 1),
		stm.tm_mday,
		stm.tm_hour,
		stm.tm_min,
		stm.tm_sec);

	return ret;
}

/**
 * time_from_isodate:
 * @str: Date/time value in ISO 8601 format.
 *
 * Converts an ISO 8601 UTC time string into a time_t value.
 *
 * Returns: Time_t corresponding to the specified ISO string.
 * Note that we only allow UTC times at present.
 **/
time_t
time_from_isodate (const gchar *str)
{
	ICalTime *tt;
	ICalTimezone *utc_zone;
	gint len, i;
	time_t res;

	g_return_val_if_fail (str != NULL, -1);

	/* yyyymmdd[Thhmmss[Z]] */

	len = strlen (str);

	if (!(len == 8 || len == 15 || len == 16))
		return -1;

	for (i = 0; i < len; i++)
		if (!((i != 8 && i != 15 && isdigit (str[i]))
		      || (i == 8 && str[i] == 'T')
		      || (i == 15 && str[i] == 'Z')))
			return -1;

#define digit_at(x,y) (x[y] - '0')

	tt = i_cal_time_new_null_time ();

	i_cal_time_set_year (tt, digit_at (str, 0) * 1000 +
				     digit_at (str, 1) * 100 +
				     digit_at (str, 2) * 10 +
				     digit_at (str, 3));

	i_cal_time_set_month (tt, digit_at (str, 4) * 10 +
				      digit_at (str, 5));

	i_cal_time_set_day (tt, digit_at (str, 6) * 10 +
				    digit_at (str, 7));

	if (len > 8) {
		i_cal_time_set_hour (tt, digit_at (str, 9) * 10 +
					     digit_at (str, 10));
		i_cal_time_set_minute (tt, digit_at (str, 11) * 10 +
					       digit_at (str, 12));
		i_cal_time_set_second (tt, digit_at (str, 13) * 10 +
					       digit_at (str, 14));
	}

	utc_zone = i_cal_timezone_get_utc_timezone ();

	res = i_cal_time_as_timet_with_zone (tt, utc_zone);

	g_object_unref (tt);

	return res;
}

/**
 * e_cal_util_icaltime_to_tm:
 * @itt: An #ICalTime
 *
 * Converts an #ICalTime into a GLibc's struct tm.
 *
 * Returns: The converted time as a struct tm. All fields will be
 *    set properly except for tm.tm_yday.
 *
 * Since: 2.22
 */
struct tm
e_cal_util_icaltime_to_tm (const ICalTime *itt)
{
	struct tm tm;
	ICalTime *tt = (ICalTime *) itt;

	memset (&tm, 0, sizeof (struct tm));

	g_return_val_if_fail (itt != NULL, tm);

	if (!i_cal_time_is_date (tt)) {
		tm.tm_sec = i_cal_time_get_second (tt);
		tm.tm_min = i_cal_time_get_minute (tt);
		tm.tm_hour = i_cal_time_get_hour (tt);
	}

	tm.tm_mday = i_cal_time_get_day (tt);
	tm.tm_mon = i_cal_time_get_month (tt) - 1;
	tm.tm_year = i_cal_time_get_year (tt) - 1900;
	tm.tm_wday = time_day_of_week (i_cal_time_get_day (tt), i_cal_time_get_month (tt) - 1, i_cal_time_get_year (tt));
	tm.tm_isdst = -1;

	return tm;
}

/**
 * e_cal_util_icaltime_to_tm_with_zone:
 * @itt: A time value.
 * @from_zone: Source timezone.
 * @to_zone: Destination timezone.
 *
 * Converts a time value from one timezone to another, and returns a struct tm
 * representation of the time.
 *
 * Returns: The converted time as a struct tm. All fields will be
 *    set properly except for tm.tm_yday.
 *
 * Since: 2.22
 **/
struct tm
e_cal_util_icaltime_to_tm_with_zone (const ICalTime *itt,
				     const ICalTimezone *from_zone,
				     const ICalTimezone *to_zone)
{
	struct tm tm;
	ICalTime *itt_copy;

	memset (&tm, 0, sizeof (tm));
	tm.tm_isdst = -1;

	g_return_val_if_fail (itt != NULL, tm);

	itt_copy = i_cal_time_clone (itt);

	i_cal_time_convert_timezone (itt_copy, (ICalTimezone *) from_zone, (ICalTimezone *) to_zone);
	tm = e_cal_util_icaltime_to_tm (itt_copy);
	g_object_unref (itt_copy);

	return tm;
}

/**
 * e_cal_util_tm_to_icaltime:
 * @tm: A struct tm.
 * @is_date: Whether the given time is a date only or not.
 *
 * Converts a struct tm into an #ICalTime. Free the returned object
 * with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): The converted time as an #ICalTime.
 *
 * Since: 2.22
 */
ICalTime *
e_cal_util_tm_to_icaltime (struct tm *tm,
			   gboolean is_date)
{
	ICalTime *itt;

	g_return_val_if_fail (tm != NULL, NULL);

	itt = i_cal_time_new_null_time ();

	if (!is_date)
		i_cal_time_set_time (itt, tm->tm_hour, tm->tm_min, tm->tm_sec);

	i_cal_time_set_date (itt, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
	i_cal_time_set_is_date (itt, is_date);

	return itt;
}

/**
 * e_cal_util_guess_timezone:
 * @tzid: a time zone ID
 *
 * Tries to match the @tzid with a built-in iCal timezone.
 *
 * Whenever possible, the timezone should be taken from
 * the timezone cache or the VCALENDAR component the owner
 * component belongs to, this should be used just as the last
 * resort when such time zone could not be found.
 *
 * Returns: (nullable) (transfer none): matching built-in #ICalTimezone for the @tzid,
 *    or %NULL, when could not be found
 *
 * Since: 3.58
 **/
ICalTimezone *
e_cal_util_guess_timezone (const gchar *tzid)
{
	ICalTimezone *zone;

	if (!tzid || !*tzid)
		return NULL;

	zone = i_cal_timezone_get_builtin_timezone (tzid);
	if (zone)
		return zone;

	zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
	if (zone)
		return zone;

	tzid = e_cal_match_tzid (tzid);
	if (tzid) {
		zone = i_cal_timezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			zone = i_cal_timezone_get_builtin_timezone (tzid);
	}

	return zone;
}

/**
 * e_cal_util_comp_time_to_zone:
 * @icomp: an #ICalComponent to get the property from
 * @prop_kind: an #ICalPropertyKind of the property to read
 * @to_zone: (nullable): an #ICalTimezone to convert the time to, or %NULL for UTC
 * @vcalendar: (nullable): an optional VCALENDAR component with timezones for the @icomp, or %NULL when not available
 * @tz_cache: (nullable): an #ETimezoneCache to use to read the time zones from, or %NULL if not available
 * @out_itt: (out) (optional) (transfer full): return location for the converted time as #ICalTime, or %NULL when not requested
 *
 * Converts the time/date property @prop_kind to @to_zone. When such property
 * does not exist, or does not contain DATE nor DATE-TIME value, then
 * the function returns -1 and no output argument is set.
 *
 * The @vcalendar is used to get the timezone for the property, if provided,
 * otherwise the timezone is tried to be found in the @tz_cache. If neither
 * can get it, the iCal builtin timezones are checked. When the set timezone
 * cannot be found, floating time is used (which can be almost always wrong).
 *
 * Note: this uses i_cal_component_get_first_property(), thus it cannot be used
 *    in case any upper caller uses it too at the same time.
 *
 * Returns: property's time converted into @to_zone, or -1, when the conversion
 *    was not possible.
 *
 * Since: 3.58
 **/
time_t
e_cal_util_comp_time_to_zone (ICalComponent *icomp,
			      ICalPropertyKind prop_kind,
			      ICalTimezone *to_zone,
			      ICalComponent *vcalendar,
			      ETimezoneCache *tz_cache,
			      ICalTime **out_itt)
{
	ICalProperty *prop;
	time_t res = (time_t) -1;

	g_return_val_if_fail (I_CAL_IS_COMPONENT (icomp), res);

	prop = i_cal_component_get_first_property (icomp, prop_kind);
	if (prop)
		res = e_cal_util_property_time_to_zone (prop, to_zone, vcalendar, tz_cache, out_itt);

	g_clear_object (&prop);

	return res;
}

/**
 * e_cal_util_property_time_to_zone:
 * @prop: an #ICalProperty of DATE or DATE-TIME value
 * @to_zone: (nullable): an #ICalTimezone to convert the time to, or %NULL for UTC
 * @vcalendar: (nullable): an optional VCALENDAR component with timezones for the @prop, or %NULL when not available
 * @tz_cache: (nullable): an #ETimezoneCache to use to read the time zones from, or %NULL if not available
 * @out_itt: (out) (optional) (transfer full): return location for the converted time as #ICalTime, or %NULL when not requested
 *
 * Converts the time/date property @prop to @to_zone. When such property
 * does not contain DATE nor DATE-TIME value, the function returns -1
 * and no output argument is set.
 *
 * The @vcalendar is used to get the timezone for the property, if provided,
 * otherwise the timezone is tried to be found in the @tz_cache. If neither
 * can get it, the iCal builtin timezones are checked. When the set timezone
 * cannot be found, floating time is used (which can be almost always wrong).
 *
 * Returns: property's time converted into @to_zone, or -1, when the conversion
 *    was not possible.
 *
 * Since: 3.58
 **/
time_t
e_cal_util_property_time_to_zone (ICalProperty *prop,
				  ICalTimezone *to_zone,
				  ICalComponent *vcalendar,
				  ETimezoneCache *tz_cache,
				  ICalTime **out_itt)
{
	time_t res = (time_t) -1;
	ICalValue *value;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (prop), res);

	value = i_cal_property_get_value (prop);
	if (value) {
		ICalTime *itt = NULL;

		if (i_cal_value_isa (value) == I_CAL_DATE_VALUE ||
		    i_cal_value_isa (value) == I_CAL_DATETIME_VALUE)
			itt = i_cal_value_get_datetime (value);
		else if (i_cal_value_isa (value) == I_CAL_DATETIMEDATE_VALUE)
			itt = i_cal_value_get_datetimedate (value);

		if (itt) {
			ICalParameter *param;
			const gchar *tzid = NULL;

			param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
			if (param)
				tzid = i_cal_parameter_get_tzid (param);
			else if (i_cal_time_is_utc (itt))
				tzid = "UTC";

			res = e_cal_util_time_to_zone (itt, tzid, to_zone, vcalendar, tz_cache, out_itt);

			g_clear_object (&param);
			g_clear_object (&itt);
		}

		g_clear_object (&value);
	}

	return res;
}

/**
 * e_cal_util_time_to_zone:
 * @itt: an #ICalTime to convert
 * @tzid: (nullable): a timezone ID the @itt is at, or %NULL for floating time
 * @vcalendar: (nullable): an optional VCALENDAR component with timezones for the @tzid, or %NULL when not available
 * @tz_cache: (nullable): an #ETimezoneCache to use to read the time zones from, or %NULL if not available
 * @out_itt: (out) (optional) (transfer full): return location for the converted time as #ICalTime, or %NULL when not requested
 *
 * Converts the @itt in @tzid to @to_zone.
 *
 * The @vcalendar is used to get the timezone for the property, if provided,
 * otherwise the timezone is tried to be found in the @tz_cache. If neither
 * can get it, the iCal builtin timezones are checked. When the set timezone
 * cannot be found, floating time is used (which can be almost always wrong).
 *
 * Returns: the time converted into @to_zone, or -1, when the conversion
 *    was not possible.
 *
 * Since: 3.58
 **/
time_t
e_cal_util_time_to_zone (const ICalTime *itt,
			 const gchar *tzid,
			 ICalTimezone *to_zone,
			 ICalComponent *vcalendar,
			 ETimezoneCache *tz_cache,
			 ICalTime **out_itt)
{
	ICalTime *itt_copy;
	ICalTimezone *from_zone = NULL;
	time_t res = (time_t) -1;

	g_return_val_if_fail (I_CAL_IS_TIME (itt), res);

	if (i_cal_time_is_null_time (itt) || !i_cal_time_is_valid_time (itt))
		return res;

	itt_copy = i_cal_time_clone (itt);

	if (tzid && *tzid) {
		if (g_ascii_strcasecmp (tzid, "UTC") == 0) {
			from_zone = g_object_ref (i_cal_timezone_get_utc_timezone ());
		} else {
			if (vcalendar)
				from_zone = i_cal_component_get_timezone (vcalendar, tzid);

			if (!from_zone && tz_cache) {
				from_zone = e_timezone_cache_get_timezone (tz_cache, tzid);
				if (from_zone)
					g_object_ref (from_zone);
			}

			if (!from_zone) {
				from_zone = e_cal_util_guess_timezone (tzid);
				if (from_zone)
					g_object_ref (from_zone);
			}
		}
	} else if (i_cal_time_is_utc (itt)) {
		from_zone = g_object_ref (i_cal_timezone_get_utc_timezone ());
	}

	i_cal_time_set_timezone (itt_copy, from_zone);
	i_cal_time_convert_timezone (itt_copy, from_zone, to_zone);
	i_cal_time_set_timezone (itt_copy, to_zone);

	res = i_cal_time_as_timet_with_zone (itt_copy, to_zone);

	if (out_itt)
		*out_itt = g_steal_pointer (&itt_copy);

	g_clear_object (&from_zone);
	g_clear_object (&itt_copy);

	return res;
}

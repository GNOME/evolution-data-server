/*
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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel-mime-utils.h"
#include "camel-sexp.h"

#include "camel-search-utils.h"

/**
 * camel_search_util_add_months:
 * @t: Initial time
 * @months: number of months to add or subtract
 *
 * Increases time @t by the given number of months (or decreases, if
 * @months is negative).
 *
 * Returns: a new #time_t value
 *
 * Since: 3.58
 **/
time_t
camel_search_util_add_months (time_t t,
			      gint months)
{
	GDateTime *dt, *dt2;
	time_t res;

	if (!months)
		return t;

	dt = g_date_time_new_from_unix_utc (t);

	/* just for issues, to return something inaccurate, but sane */
	res = t + (60 * 60 * 24 * 30 * months);

	g_return_val_if_fail (dt != NULL, res);

	dt2 = g_date_time_add_months (dt, months);
	g_date_time_unref (dt);
	g_return_val_if_fail (dt2 != NULL, res);

	res = g_date_time_to_unix (dt2);
	g_date_time_unref (dt2);

	return res;
}

static time_t
folder_search_num_to_timet (gint num)
{
	time_t res = (time_t) -1;

	if (num > 9999999) {
		GDateTime *dtm;

		dtm = g_date_time_new_utc (num / 10000, (num / 100) % 100, num % 100, 0, 0, 0.0);
		if (dtm) {
			res = (time_t) g_date_time_to_unix (dtm);
			g_date_time_unref (dtm);
		}
	}

	return res;
}

/**
 * camel_search_util_str_to_time:
 * @str: (nullable): string to convert to time, or %NULL
 *
 * Converts a string representation to a time_t (as gint64).
 * When @str is %NULL, returns -1.
 *
 * Returns: a time_t representation of the @str, -1 on error
 *
 * Since: 3.58
 **/
gint64
camel_search_util_str_to_time (const gchar *str)
{
	GDateTime *datetime;
	gint64 res = -1;

	if (!str || !*str)
		return -1;

	datetime = g_date_time_new_from_iso8601 (str, NULL);
	if (datetime) {
		res = g_date_time_to_unix (datetime);
		g_date_time_unref (datetime);
	} else if (strlen (str) == 8) {
		gint num;

		num = atoi (str);
		res = folder_search_num_to_timet (num);
	} else {
		res = camel_header_decode_date (str, NULL);
	}

	return res;
}

/**
 * camel_search_util_make_time:
 * @argc: number of arguments in @argv
 * @argv: array or arguments
 *
 * Implementation of 'make-time' function, which expects one argument,
 * a string or an integer, to be converted into time_t.
 *
 * Returns: time_t equivalent of the passed in argument, or (time_t) -1 on error.
 *
 * Since: 3.58
 **/
time_t
camel_search_util_make_time (gint argc,
			     CamelSExpResult **argv)
{
	time_t res = (time_t) -1;

	g_return_val_if_fail (argv != NULL, res);

	if (argc == 1 && argv[0]->type == CAMEL_SEXP_RES_STRING && argv[0]->value.string) {
		res = camel_search_util_str_to_time (argv[0]->value.string);
	} else if (argc == 1 && argv[0]->type == CAMEL_SEXP_RES_TIME) {
		res = argv[0]->value.time;
	} else if (argc == 1 && argv[0]->type == CAMEL_SEXP_RES_INT) {
		res = folder_search_num_to_timet (argv[0]->value.number);
	}

	return res;
}

/**
 * camel_search_util_compare_date:
 * @datetime1: a time_t-like value of the first date-time
 * @datetime2: a time_t-like value of the second date-time
 *
 * Compares date portion of the two date-time values, first converted
 * into the local time zone. The returned value is like with strcmp().
 *
 * Returns: 0 when the dates are equal, < 0 when first is before second and
 *    > 0 when the first is after the second date
 *
 * Since: 3.58
 **/
gint
camel_search_util_compare_date (gint64 datetime1,
				gint64 datetime2)
{
	struct tm tm;
	time_t tt;
	gint dt1, dt2;

	tt = (time_t) datetime1;
	localtime_r (&tt, &tm);
	dt1 = ((tm.tm_year + 1900) * 10000) + ((tm.tm_mon + 1) * 100) + tm.tm_mday;

	tt = (time_t) datetime2;
	localtime_r (&tt, &tm);
	dt2 = ((tm.tm_year + 1900) * 10000) + ((tm.tm_mon + 1) * 100) + tm.tm_mday;

	return dt1 - dt2;
}

/**
 * camel_search_util_hash_message_id:
 * @message_id: a raw Message-ID header value
 * @needs_decode: whether the @message_id requires camel_header_msgid_decode() first
 *
 * Calculates a hash of the Message-ID header value @message_id.
 *
 * Returns: hash of the @message_id, or 0 on any error.
 *
 * Since: 3.58
 **/
guint64
camel_search_util_hash_message_id (const gchar *message_id,
				   gboolean needs_decode)
{
	GChecksum *checksum;
	CamelSummaryMessageID message_id_struct;
	gchar *msgid = NULL;
	guint8 *digest;
	gsize length;

	if (!message_id)
		return 0;

	if (needs_decode)
		msgid = camel_header_msgid_decode (message_id);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) (msgid ? msgid : message_id), -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	memcpy (message_id_struct.id.hash, digest, sizeof (message_id_struct.id.hash));
	g_free (msgid);

	return message_id_struct.id.id;
}

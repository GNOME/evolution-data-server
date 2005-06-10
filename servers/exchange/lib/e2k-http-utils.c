/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e2k-http-utils.h"

#include <libedataserver/e-time-utils.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern const char *e2k_rfc822_months [];

/**
 * e2k_http_parse_date:
 * @date: an HTTP Date header, returned from Exchange
 *
 * Converts an HTTP Date header into a time_t value. Doesn't
 * do much sanity checking on the format since we know IIS always
 * returns the date in RFC 1123 format, not either of the other two
 * allowable formats.
 *
 * Return value: a %time_t corresponding to @date.
 **/
time_t
e2k_http_parse_date (const char *date)
{
	struct tm tm;
	char *p;

	if (strlen (date) < 29 || date[3] != ',' || date[4] != ' ')
		return -1;

	memset (&tm, 0, sizeof (tm));
	p = (char *)date + 5;

	tm.tm_mday = strtol (p, &p, 10);
	p++;
	for (tm.tm_mon = 0; tm.tm_mon < 12; tm.tm_mon++) {
		if (!strncmp (p, e2k_rfc822_months[tm.tm_mon], 3))
			break;
	}
	p += 3;

	tm.tm_year = strtol (p, &p, 10) - 1900;

	tm.tm_hour = strtol (p, &p, 10);
	p++;
	tm.tm_min  = strtol (p, &p, 10);
	p++;
	tm.tm_sec  = strtol (p, &p, 10);

	return e_mktime_utc (&tm);
}

/**
 * e2k_http_parse_status:
 * @status_line: an HTTP Status-Line
 *
 * Parses an HTTP Status-Line and returns the Status-Code
 *
 * Return value: the Status-Code portion of @status_line
 **/
E2kHTTPStatus
e2k_http_parse_status (const char *status_line)
{
	if (strncmp (status_line, "HTTP/1.", 7) != 0 ||
	    !isdigit (status_line[7]) ||
	    status_line[8] != ' ')
		return E2K_HTTP_MALFORMED;

	return atoi (status_line + 9);
}

/**
 * e2k_http_accept_language:
 *
 * Generates an Accept-Language value to send to the Exchange server.
 * The user's default folders (Inbox, Calendar, etc) are not created
 * until the user connects to Exchange for the first time, and in that
 * case, it needs to know what language to name the folders in.
 * libexchange users are responsible for setting the Accept-Language
 * header on any request that could be the first-ever request to a
 * mailbox. (Exchange will return 401 Unauthorized if it receives a
 * request with no Accept-Language header for an uninitialized
 * mailbox.)
 *
 * Return value: an Accept-Language string.
 **/
const char *
e2k_http_accept_language (void)
{
	static char *accept = NULL;

	if (!accept) {
		GString *buf;
		const char *lang, *sub;
		int baselen;

		buf = g_string_new (NULL);

		lang = getenv ("LANG");
		if (!lang || !strcmp (lang, "C") || !strcmp (lang, "POSIX"))
			lang = "en";

		/* lang is "language[_territory][.codeset][@modifier]",
		 * eg "fr" or "de_AT.utf8@euro". The Accept-Language
		 * header should be a comma-separated list of
		 * "language[-territory]". For the above cases we'd
		 * generate "fr, en" and "de-AT, de, en". (We always
		 * include "en" in case the server doesn't support the
		 * user's preferred locale.)
		 */

		baselen = strcspn (lang, "_.@");
		g_string_append_len (buf, lang, baselen);
		if (lang[baselen] == '_') {
			sub = lang + baselen + 1;
			g_string_append_c   (buf, '-');
			g_string_append_len (buf, sub, strcspn (sub, ".@"));

			g_string_append     (buf, ", ");
			g_string_append_len (buf, lang, baselen);
		}

		if (baselen != 2 || strncmp (lang, "en", 2) != 0)
			g_string_append (buf, ", en");

		accept = buf->str;
		g_string_free (buf, FALSE);
	}

	return accept;
}


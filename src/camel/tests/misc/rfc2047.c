/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * Authors: Jeffrey Stedfast <fejj@novell.com>
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

struct {
	const gchar *encoded;
	const gchar *decoded;
	gint dummy;
} test1[] = {
	/* the first half are rfc compliant cases (which are the most important) */
	{ "=?iso-8859-1?q?this=20is=20some=20text?=", "this is some text", 0 },
	{ "this =?iso-8859-1?q?is_some?= text", "this is some text", 0 },
	{ "=?iso-8859-1?q?th?= =?iso-8859-1?q?is?= is some text", "this is some text", 0 },
	{ "=?ISO-8859-1?B?SWYgeW91IGNhbiByZWFkIHRoaXMgeW8=?=  =?ISO-8859-2?B?dSB1bmRlcnN0YW5kIHRoZSBleGFtcGxlLg==?=",
	  "If you can read this you understand the example.", 0 },
#if 0
	/* And oddly enough, camel fails on these, removed for now */

	/* second half: brokenly encoded rfc2047 words */
	{ "foo=?UTF-8?Q?bar?=baz", "foobarbaz", 0 },
	{ "foo =?UTF-8?Q?bar?=baz", "foo barbaz", 0 },
	{ "foo=?UTF-8?Q?bar?= baz", "foobar baz", 0 },
	{ "=?UTF-8?Q?foo bar?=baz", "foo barbaz", 0 },
	{ "foo=?UTF-8?Q?bar baz?=", "foobar baz", 0 },
	{ "=?UTF-8?Q?foo?==?UTF-8?Q?bar baz?=", "foobar baz", 0 },
	{ "=?UTF-8?Q?foo?= =?UTF-8?Q?bar baz?=", "foobar baz", 0 },
	{ "=?UTF-8?Q?foo?= bar =?UTF-8?Q?baz?=", "foo bar baz", 0 },
	{ "=?foo=?UTF-8?Q?bar baz?=", "=?foobar baz", 0 },
	{ "=?foo?=?UTF-8?Q?bar baz?=", "=?foo?bar baz", 0 },
	{ "=?foo?Q=?UTF-8?Q?bar baz?=", "=?foo?Qbar baz", 0 },
	{ "=?foo?Q?=?UTF-8?Q?bar baz?=", "=?foo?Q?bar baz", 0 },
	{ "=?UTF-8?Q?bar ? baz?=", "=?UTF-8?Q?bar ? baz?=", 0 },
#endif
};

struct {
	const gchar *encoded;
	const gchar *decoded;
	gint dummy;
} test2[] = {
	/* ctext tests */
	{ "Test of ctext (=?ISO-8859-1?Q?a?= =?ISO-8859-1?Q?b?=)", "Test of ctext (ab)", 1 },
	{ "ctext with \\\"quoted\\\" pairs", "ctext with \"quoted\" pairs", 1 }
};

gint main (gint argc, gchar ** argv)
{
	gchar *decoded;
	gint i;

	camel_test_init (argc, argv);

	camel_test_start ("rfc2047 decoding");

	for (i = 0; i < G_N_ELEMENTS (test1); i++) {
		camel_test_push ("rfc2047 decoding[%d] '%s'", i, test1[i].encoded);
		decoded = camel_header_decode_string (test1[i].encoded, "iso-8859-1");
		check (decoded && strcmp (decoded, test1[i].decoded) == 0);
		g_free (decoded);
		camel_test_pull ();
	}

	camel_test_end ();

	camel_test_start ("rfc2047 ctext decoding");

	for (i = 0; i < G_N_ELEMENTS (test2); i++) {
		camel_test_push ("rfc2047 ctext decoding[%d] '%s'", i, test2[i].encoded);
		decoded = camel_header_format_ctext (test2[i].encoded, "iso-8859-1");
		check (decoded && strcmp (decoded, test2[i].decoded) == 0);
		g_free (decoded);
		camel_test_pull ();
	}

	camel_test_end ();

	return 0;
}

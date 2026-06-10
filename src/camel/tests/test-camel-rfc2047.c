/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@novell.com>
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
	{ "aaa bbb  =?utf-8?Q?=63=63=63?= =?utf-8?Q?=64=64=64?="
	  "    =?utf-8?Q?=65=65=65?= fff =?utf-8?Q?=67=67=67?=  hhh",
	  "aaa bbb  cccdddeee fff ggg  hhh", 0 }

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
	{ "ctext with \\\"quoted\\\" pairs", "ctext with \"quoted\" pairs", 1 },
	{ "aaa bbb  =?utf-8?Q?=63=63=63?= =?utf-8?Q?=64=64=64?="
	  "    =?utf-8?Q?=65=65=65?= fff =?utf-8?Q?=67=67=67?=  hhh",
	  "aaa bbb  cccdddeee fff ggg  hhh", 1 }
};

static void
test_rfc2047_decoding (void)
{
	gchar *decoded;
	gint i;

	for (i = 0; i < G_N_ELEMENTS (test1); i++) {
		decoded = camel_header_decode_string (test1[i].encoded, "iso-8859-1");
		g_assert_true (decoded != NULL);
		if (strcmp (decoded, test1[i].decoded) != 0)
			g_error ("rfc2047 decoding[%d]: got '%s', expected '%s'",
				i, decoded, test1[i].decoded);
		g_free (decoded);
	}
}

static void
test_rfc2047_ctext_decoding (void)
{
	gchar *decoded;
	gint i;

	for (i = 0; i < G_N_ELEMENTS (test2); i++) {
		decoded = camel_header_format_ctext (test2[i].encoded, "iso-8859-1");
		g_assert_true (decoded != NULL);
		if (strcmp (decoded, test2[i].decoded) != 0)
			g_error ("rfc2047 ctext decoding[%d]: got '%s', expected '%s'",
				i, decoded, test2[i].decoded);
		g_free (decoded);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/RFC2047/decoding", test_rfc2047_decoding);
	g_test_add_func ("/Camel/RFC2047/ctext-decoding", test_rfc2047_ctext_decoding);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

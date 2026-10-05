/*
 * SPDX-FileCopyrightText: (C) 2026 Mike C. Ward <mward5@tinfoil-fedora.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include "camel-test.h"

static gchar *
quoted_encode (const gchar *text)
{
	gsize len = strlen (text);
	guchar *out;
	gint state = -1, save = 0;
	gsize out_len;

	/* each byte can grow to three, plus the soft line breaks */
	out = g_malloc0 (len * 4 + 16);
	out_len = camel_quoted_encode_close ((guchar *) text, len, out, &state, &save);
	out[out_len] = '\0';

	return (gchar *) out;
}

static gchar *
quoted_decode (const gchar *text)
{
	gsize len = strlen (text);
	guchar *out;
	gint state = 0;
	gint save = 0;
	gsize out_len;

	out = g_malloc0 (len + 1);
	out_len = camel_quoted_decode_step ((guchar *) text, len, out, &state, &save);
	out[out_len] = '\0';

	return (gchar *) out;
}

static void
test_quoted_printable_tab (void)
{
	const gchar *text = "a\tb \tc\t\nd\t\n";
	gchar *encoded, *decoded;

	encoded = quoted_encode (text);
	g_assert_cmpstr (encoded, ==, "a=09b =09c=09\nd=09\n");

	decoded = quoted_decode (encoded);
	g_assert_cmpstr (decoded, ==, text);

	g_free (decoded);
	g_free (encoded);
}

static void
test_quoted_printable_space (void)
{
	gchar *encoded;

	encoded = quoted_encode ("a b \nc ");
	g_assert_cmpstr (encoded, ==, "a b=20\nc=20");
	g_free (encoded);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/QuotedPrintable/tab", test_quoted_printable_tab);
	g_test_add_func ("/Camel/QuotedPrintable/space", test_quoted_printable_space);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}

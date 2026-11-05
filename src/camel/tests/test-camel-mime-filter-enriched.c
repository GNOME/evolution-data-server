/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "camel-test.h"

static void
test_valid (void)
{
	gchar *html;

	html = camel_enriched_to_html ("<bold>Hello</bold>", 0);
	g_assert_cmpstr (html, ==, "<b>Hello</b>");
	g_free (html);

	html = camel_enriched_to_html ("<color><param>red</param>Hello</color>", 0);
	g_assert_cmpstr (html, ==, "<font color=\"red\">Hello</font>");
	g_free (html);
}

static void
test_invalid (void)
{
	gchar *html, *tag, *input, *expected;

	html = camel_enriched_to_html ("<nosuchtag>text", 0);
	g_assert_cmpstr (html, ==, "text");
	g_free (html);

	tag = g_strnfill (300, 'a');
	input = g_strdup_printf ("<%s>text", tag);
	expected = g_strdup_printf ("&lt;%s&gt;text", tag);

	html = camel_enriched_to_html (input, 0);
	g_assert_cmpstr (html, ==, expected);

	g_free (html);
	g_free (expected);
	g_free (input);
	g_free (tag);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/MimeFilter/Enriched/valid", test_valid);
	g_test_add_func ("/Camel/MimeFilter/Enriched/invalid", test_invalid);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

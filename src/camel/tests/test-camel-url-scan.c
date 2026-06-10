/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

static struct {
	const gchar *text, *url;
} url_tests[] = {
	{ "bob@foo.com", "mailto:bob@foo.com" },
	{ "Ends with bob@foo.com", "mailto:bob@foo.com" },
	{ "bob@foo.com at start", "mailto:bob@foo.com" },
	{ "bob@foo.com.", "mailto:bob@foo.com" },
	{ "\"bob@foo.com\"", "mailto:bob@foo.com" },
	{ "<bob@foo.com>", "mailto:bob@foo.com" },
	{ "(bob@foo.com)", "mailto:bob@foo.com" },
	{ "bob@foo.com, 555-9999", "mailto:bob@foo.com" },
	{ "|bob@foo.com|555-9999|", "mailto:bob@foo.com" },
	{ "bob@ no match bob@", NULL },
	{ "@foo.com no match @foo.com", NULL },
	{ "\"bob\"@foo.com", NULL },
	{ "M@ke money fast!", NULL },
	{ "ASCII art @_@ @>->-", NULL },

	{ "http://www.foo.com", "http://www.foo.com" },
	{ "Ends with http://www.foo.com", "http://www.foo.com" },
	{ "http://www.foo.com at start", "http://www.foo.com" },
	{ "http://www.foo.com.", "http://www.foo.com" },
	{ "http://www.foo.com/.", "http://www.foo.com/" },
	{ "<http://www.foo.com>", "http://www.foo.com" },
	{ "(http://www.foo.com)", "http://www.foo.com" },
	{ "http://www.foo.com, 555-9999", "http://www.foo.com" },
	{ "|http://www.foo.com|555-9999|", "http://www.foo.com" },
	{ "foo http://www.foo.com/ bar", "http://www.foo.com/" },
	{ "foo http://www.foo.com/index.html bar", "http://www.foo.com/index.html" },
	{ "foo http://www.foo.com/q?99 bar", "http://www.foo.com/q?99" },
	{ "foo http://www.foo.com/;foo=bar&baz=quux bar", "http://www.foo.com/;foo=bar&baz=quux" },
	{ "foo http://www.foo.com/index.html#anchor bar", "http://www.foo.com/index.html#anchor" },
	{ "http://www.foo.com/index.html; foo", "http://www.foo.com/index.html" },
	{ "http://www.foo.com/index.html: foo", "http://www.foo.com/index.html" },
	{ "http://www.foo.com/index.html-- foo", "http://www.foo.com/index.html" },
	{ "http://www.foo.com/index.html?", "http://www.foo.com/index.html" },
	{ "http://www.foo.com/index.html!", "http://www.foo.com/index.html" },
	{ "\"http://www.foo.com/index.html\"", "http://www.foo.com/index.html" },
	{ "'http://www.foo.com/index.html'", "http://www.foo.com/index.html" },
	{ "http://bob@www.foo.com/bar/baz/", "http://bob@www.foo.com/bar/baz/" },
	{ "http no match http", NULL },
	{ "http: no match http:", NULL },
	{ "http:// no match http://", NULL },
	{ "unrecognized://bob@foo.com/path", "mailto:bob@foo.com" },

	{ "src/www.c", NULL },
	{ "Ewwwwww.Gross.", NULL },
};

static void
test_url_scanning (void)
{
	gchar *html, *url, *p;
	gint i;
	guint32 flags;

	flags = CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;

	for (i = 0; i < G_N_ELEMENTS (url_tests); i++) {
		html = camel_text_to_html (url_tests[i].text, flags, 0);

		url = strstr (html, "href=\"");
		if (url) {
			url += 6;
			p = strchr (url, '"');
			if (p)
				*p = '\0';

			while ((p = strstr (url, "&amp;")))
				memmove (p + 1, p + 5, strlen (p + 5) + 1);
		}

		if ((url && (!url_tests[i].url || strcmp (url, url_tests[i].url) != 0)) ||
		    (!url && url_tests[i].url)) {
			g_error (
				"FAILED on \"%s\" -> %s (got %s)",
				url_tests[i].text,
				url_tests[i].url ? url_tests[i].url : "(nothing)",
				url ? url : "(nothing)");
		}

		g_free (html);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/URL-Scan/scanning", test_url_scanning);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

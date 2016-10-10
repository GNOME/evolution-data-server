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
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

struct {
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

static gint num_url_tests = G_N_ELEMENTS (url_tests);

gint main (gint argc, gchar **argv)
{
	gchar *html, *url, *p;
	gint i, errors = 0;
	guint32 flags;

	camel_test_init (argc, argv);

	camel_test_start ("URL scanning");

	flags = CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;
	for (i = 0; i < num_url_tests; i++) {
		camel_test_push ("'%s' => '%s'", url_tests[i].text, url_tests[i].url ? url_tests[i].url : "None");

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
			printf (
				"FAILED on \"%s\" -> %s\n  (got %s)\n\n",
				url_tests[i].text,
				url_tests[i].url ? url_tests[i].url : "(nothing)",
				url ? url : "(nothing)");
			errors++;
		}

		g_free (html);
	}

	printf ("\n%d errors\n", errors);

	camel_test_end ();

	return errors;
}

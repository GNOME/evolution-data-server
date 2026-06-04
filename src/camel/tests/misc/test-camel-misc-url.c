/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

static const gchar *base = "http://a/b/c/d;p?q#f";

static struct {
	const gchar *url_string, *result;
} tests[] = {
	{ "g:h", "g:h" },
	{ "g", "http://a/b/c/g" },
	{ "./g", "http://a/b/c/g" },
	{ "g/", "http://a/b/c/g/" },
	{ "/g", "http://a/g" },
	{ "//g", "http://g" },
	{ "?y", "http://a/b/c/d;p?y" },
	{ "g?y", "http://a/b/c/g?y" },
	{ "g?y/./x", "http://a/b/c/g?y/./x" },
	{ "#s", "http://a/b/c/d;p?q#s" },
	{ "g#s", "http://a/b/c/g#s" },
	{ "g#s/./x", "http://a/b/c/g#s/./x" },
	{ "g?y#s", "http://a/b/c/g?y#s" },
	{ ";x", "http://a/b/c/d;x" },
	{ "g;x", "http://a/b/c/g;x" },
	{ "g;x?y#s", "http://a/b/c/g;x?y#s" },
	{ ".", "http://a/b/c/" },
	{ "./", "http://a/b/c/" },
	{ "..", "http://a/b/" },
	{ "../", "http://a/b/" },
	{ "../g", "http://a/b/g" },
	{ "../..", "http://a/" },
	{ "../../", "http://a/" },
	{ "../../g", "http://a/g" },
	{ "", "http://a/b/c/d;p?q#f" },
	{ "../../../g", "http://a/../g" },
	{ "../../../../g", "http://a/../../g" },
	{ "/./g", "http://a/./g" },
	{ "/../g", "http://a/../g" },
	{ "g.", "http://a/b/c/g." },
	{ ".g", "http://a/b/c/.g" },
	{ "g..", "http://a/b/c/g.." },
	{ "..g", "http://a/b/c/..g" },
	{ "./../g", "http://a/b/g" },
	{ "./g/.", "http://a/b/c/g/" },
	{ "g/./h", "http://a/b/c/g/h" },
	{ "g/../h", "http://a/b/c/h" },
	{ "http:g", "http:g" },
	{ "http:", "http:" },

	/* (not from rfc 1808) */
	{ "sendmail:", "sendmail:" },
	{ "mbox:/var/mail/user", "mbox:/var/mail/user" },
	{ "pop://user@host", "pop://user@host" },
	{ "pop://user@host:99", "pop://user@host:99" },
	{ "pop://user:password@host", "pop://user:password@host" },
	{ "pop://user:password@host:99", "pop://user:password@host:99" },
	{ "pop://user;auth=APOP@host", "pop://user;auth=APOP@host" },
	{ "pop://user@host/;keep_on_server", "pop://user@host/;keep_on_server" },
	{ "pop://user@host/;keep_on_server=1", "pop://user@host/;keep_on_server=1" },
	{ "pop://us%65r@host", "pop://user@host" },
	{ "pop://us%40r@host", "pop://us%40r@host" },
	{ "pop://us%3ar@host", "pop://us%3ar@host" },
	{ "pop://us%2fr@host", "pop://us%2fr@host" }
};

static CamelURL *base_url = NULL;

static void
test_url_base_parse (void)
{
	gchar *url_string;
	GError *error = NULL;

	base_url = camel_url_new (base, &error);
	if (!base_url)
		g_error ("Could not parse %s: %s", base, error->message);

	url_string = camel_url_to_string (base_url, 0);
	if (strcmp (url_string, base) != 0)
		g_error ("URL <%s> unparses to <%s>", base, url_string);
	g_free (url_string);
}

static void
test_url_relative_resolution (void)
{
	CamelURL *url;
	gchar *url_string;
	gint i;

	g_assert_nonnull (base_url);

	for (i = 0; i < G_N_ELEMENTS (tests); i++) {
		url = camel_url_new_with_base (base_url, tests[i].url_string);
		if (!url) {
			g_error (
				"<%s> + <%s>: could not parse",
				base, tests[i].url_string);
			continue;
		}

		url_string = camel_url_to_string (url, 0);
		if (strcmp (url_string, tests[i].result) != 0)
			g_error (
				"<%s> + <%s> = <%s>, got <%s>",
				base, tests[i].url_string,
				tests[i].result, url_string);
		g_free (url_string);
		camel_url_free (url);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/URL/base-parse", test_url_base_parse);
	g_test_add_func ("/Camel/URL/relative-resolution", test_url_relative_resolution);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

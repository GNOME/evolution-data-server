/*
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
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

static void
detect_hostname_bad_chars (void)
{
	struct _data {
		const gchar *hostname;
		gboolean needs_convert;
	} data[] = {
		{ "example.com", FALSE },
		{ "ex\xd0\xb0" "mple.com", TRUE }
	};
	gint ii;

	camel_test_start ("Detect hostname bad chars");

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gboolean needs_convert = camel_hostname_utils_requires_ascii (data[ii].hostname);
		check_msg (needs_convert == data[ii].needs_convert,
			"Failed on [%d] (%s): returns %d, expected %d", ii, data[ii].hostname, needs_convert, data[ii].needs_convert);
	}

	camel_test_end ();
}

static void
convert_hostname_bad_chars_email (void)
{
	struct _data {
		const gchar *value;
		const gchar *fmt_expected;
		const gchar *enc_expected;
	} data[] = {
		{ "user@example.com", NULL, NULL },
		{ "user@ex\xd0\xb0" "mple.com",
		  "user@xn--exmple-4nf.com",
		  "user@xn--exmple-4nf.com" },
		{ "Žába1 <1st@žába.no.where>",
		  "Žába1 <1st@xn--ba-lia14d.no.where>",
		  "=?iso-8859-2?Q?=AE=E1ba1?= <1st@xn--ba-lia14d.no.where>" },
		{ "Zaba2 <2nd@zab\xd0\xb0" ".no.where>",
		  "Zaba2 <2nd@xn--zab-8cd.no.where>",
		  "Zaba2 <2nd@xn--zab-8cd.no.where>" },
		{ "Žába1 <1st@žába.no.where>, Zaba2 <2nd@zab\xd0\xb0" ".no.where>",
		  "Žába1 <1st@xn--ba-lia14d.no.where>, Zaba2 <2nd@xn--zab-8cd.no.where>",
		  "=?iso-8859-2?Q?=AE=E1ba1?= <1st@xn--ba-lia14d.no.where>, Zaba2\n\t <2nd@xn--zab-8cd.no.where>" }
	};
	gint ii;

	camel_test_start ("Convert hostname bad chars in email");

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gchar *converted = camel_utils_sanitize_ascii_domain_in_address (data[ii].value, TRUE);
		check_msg (g_strcmp0 (converted, data[ii].fmt_expected) == 0,
			"Failed on [%d] (%s): returns '%s', expected formatted '%s'", ii, data[ii].value, converted, data[ii].fmt_expected);
		g_free (converted);

		converted = camel_utils_sanitize_ascii_domain_in_address (data[ii].value, FALSE);
		check_msg (g_strcmp0 (converted, data[ii].enc_expected) == 0,
			"Failed on [%d] (%s): returns '%s', expected encoded '%s'", ii, data[ii].value, converted, data[ii].enc_expected);
		g_free (converted);
	}

	camel_test_end ();
}

static void
convert_hostname_bad_chars_url (void)
{
	struct _data {
		const gchar *value;
		const gchar *expected;
	} data[] = {
		{ "mailto:user@example.com", NULL },
		{ "mailto:user@ex\xd0\xb0" "mple.com?subject=Tést",
		  "mailto:user@xn--exmple-4nf.com?subject=T%c3%a9st" },
		{ "http://žába.no.where/index.html?param1=a&amp;param2=b#fragment",
		  "http://xn--ba-lia14d.no.where/index.html?param1=a&amp;param2=b#fragment" },
		{ "https://1st@žába.no.where/",
		  "https://1st@xn--ba-lia14d.no.where/" },
		{ "ftp://2nd@zab\xd0\xb0" ".no.where/index.html",
		  "ftp://2nd@xn--zab-8cd.no.where/index.html" }
	};
	gint ii;

	camel_test_start ("Convert hostname bad chars in URL");

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gchar *converted = camel_utils_sanitize_ascii_domain_in_url_str (data[ii].value);
		check_msg (g_strcmp0 (converted, data[ii].expected) == 0,
			"Failed on [%d] (%s): returns '%s', expected '%s'", ii, data[ii].value, converted, data[ii].expected);
		g_free (converted);
	}

	camel_test_end ();
}

gint
main (gint argc,
      gchar **argv)
{

	camel_test_init (argc, argv);

	detect_hostname_bad_chars ();
	convert_hostname_bad_chars_email ();
	convert_hostname_bad_chars_url ();

	return 0;
}

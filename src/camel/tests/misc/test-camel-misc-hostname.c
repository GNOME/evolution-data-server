/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

static void
test_detect_hostname_bad_chars (void)
{
	struct _data {
		const gchar *hostname;
		gboolean needs_convert;
	} data[] = {
		{ "example.com", FALSE },
		{ "ex\xd0\xb0" "mple.com", TRUE }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gboolean needs_convert = camel_hostname_utils_requires_ascii (data[ii].hostname);
		if (needs_convert != data[ii].needs_convert)
			g_error ("Failed on [%d] (%s): returns %d, expected %d",
				ii, data[ii].hostname, needs_convert, data[ii].needs_convert);
	}
}

static void
test_convert_hostname_bad_chars_email (void)
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
		{ "\xc5\xbd\xc3\xa1" "ba1 <1st@\xc5\xbe\xc3\xa1" "ba.no.where>",
		  "\xc5\xbd\xc3\xa1" "ba1 <1st@xn--ba-lia14d.no.where>",
		  "=?iso-8859-2?Q?=AE=E1ba1?= <1st@xn--ba-lia14d.no.where>" },
		{ "Zaba2 <2nd@zab\xd0\xb0" ".no.where>",
		  "Zaba2 <2nd@xn--zab-8cd.no.where>",
		  "Zaba2 <2nd@xn--zab-8cd.no.where>" },
		{ "\xc5\xbd\xc3\xa1" "ba1 <1st@\xc5\xbe\xc3\xa1" "ba.no.where>, Zaba2 <2nd@zab\xd0\xb0" ".no.where>",
		  "\xc5\xbd\xc3\xa1" "ba1 <1st@xn--ba-lia14d.no.where>, Zaba2 <2nd@xn--zab-8cd.no.where>",
		  "=?iso-8859-2?Q?=AE=E1ba1?= <1st@xn--ba-lia14d.no.where>, Zaba2\n\t <2nd@xn--zab-8cd.no.where>" }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gchar *converted;

		converted = camel_utils_sanitize_ascii_domain_in_address (data[ii].value, TRUE);
		if (g_strcmp0 (converted, data[ii].fmt_expected) != 0)
			g_error ("Failed on [%d] (%s): returns '%s', expected formatted '%s'",
				ii, data[ii].value, converted, data[ii].fmt_expected);
		g_free (converted);

		converted = camel_utils_sanitize_ascii_domain_in_address (data[ii].value, FALSE);
		if (g_strcmp0 (converted, data[ii].enc_expected) != 0)
			g_error ("Failed on [%d] (%s): returns '%s', expected encoded '%s'",
				ii, data[ii].value, converted, data[ii].enc_expected);
		g_free (converted);
	}
}

static void
test_convert_hostname_bad_chars_url (void)
{
	struct _data {
		const gchar *value;
		const gchar *expected;
	} data[] = {
		{ "mailto:user@example.com", NULL },
		{ "mailto:user@ex\xd0\xb0" "mple.com?subject=T\xc3\xa9st",
		  "mailto:user@xn--exmple-4nf.com?subject=T%c3%a9st" },
		{ "http://\xc5\xbe\xc3\xa1" "ba.no.where/index.html?param1=a&amp;param2=b#fragment",
		  "http://xn--ba-lia14d.no.where/index.html?param1=a&amp;param2=b#fragment" },
		{ "https://1st@\xc5\xbe\xc3\xa1" "ba.no.where/",
		  "https://1st@xn--ba-lia14d.no.where/" },
		{ "ftp://2nd@zab\xd0\xb0" ".no.where/index.html",
		  "ftp://2nd@xn--zab-8cd.no.where/index.html" }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gchar *converted = camel_utils_sanitize_ascii_domain_in_url_str (data[ii].value);
		if (g_strcmp0 (converted, data[ii].expected) != 0)
			g_error ("Failed on [%d] (%s): returns '%s', expected '%s'",
				ii, data[ii].value, converted, data[ii].expected);
		g_free (converted);
	}
}

static void
test_header_folding (void)
{
	struct _data {
		const gchar *value;
		const gchar *folded;
		const gchar *unfolded;  /* NULL when expecting the same as `value` */
		const gchar *folded2; /* NULL when expecting the same as `folded` */
	} data[] = {
		{ "Short: header value",
		  "Short: header value",
		  NULL, NULL },
		{ "Short: header\tvalue",
		  "Short: header\tvalue",
		  NULL, NULL },
		{ "Long: 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:\n"
		  " 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  NULL, NULL },
		{ "Long:\t789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:\n"
		  "\t789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  NULL, NULL },
		{ "Long: 789_123456789_123456789_123456789_1234567895"
		  "123456789_123456789_123456789_123456789_123456789 "
		  "123456789_123456789_123456789_123456789_1234567895"
		  "123456789_123456789_123456789_123456789_1234567890",
		  "Long:\n"
		  " 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  NULL, NULL },
		{ "Long: 789_123456789_123456789_123456789_1234567895"
		  "123456789_123456789_123456789_123456789_123456789\t"
		  "123456789_123456789_123456789_123456789_1234567895"
		  "123456789_123456789_123456789_123456789_1234567890",
		  "Long:\n"
		  " 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  "\t123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  NULL, NULL },
		{ "Aaaaaaaaa: aaaa=aaaa@aa.aaaaa; aaaaaa-aaaaaaa=aaaaaa; aaaaaaa=xXXxXXXxxxXXXXXxoxxxxXXXxXxxXXXxXxxXxxxXxxXoXoXXXxXXxXXxXoXoxxxXX/"
		  "XxxXoXxXxxXoxXxoxxXxX/xx/oXoxxoXoxxXXxxxxxxXoxxxXXXXxxXxXXXXXoooxXxxoxXxXXXXxxXXxXXoxoXxxXxxo/XxxXoXooXXxxXoxxX/XXXXXxoxXxxXXXxX"
		  "XXXoXooxXoXxoxxXXXxXXX/xxXooxxoXxo/+xXxoxXXoXxxxXXXooXXXxxXXXxoxxxXxxxoxXxxxoxxxxxXxxxxXXXXoxoXXX/XxxxXXXo+/oxxxxXXxXxXXxxXXoXXX"
		  "xxX+XXxxXxoxxxXxxXXoXXxXXxXxox+XxoXxxXxoXXoXXxoxoxoXXXXXoXxXXXXXXoXXXoXxXXoxXXoXXxXXxoxXxXxXXoxxoXxXXxXXoxxxXXxoXxxxXXXXxXxxXXoX"
		  "XxoxxXxxXXooXXXxXxxxxXXxxXX/XX+XX+xXx+xoXxxXooxXXxXoxxxxXXXxxxxxXxxoxX+/XxxXooxxxxxooxoxxXxxxxXXxxXxooxXooxoxxxxXxXXxxXXoxXoXxXX"
		  "XxXxXxXXxxxXxxXXoxxxxxXXXxXXxxxXoxxoo+xXXXXxXXXXXXXxXxxxXXXxXXXxxXXxXXXXXXXXXXXxXXXXXxXXXXxXXXXXXoXXo+ooXxXXXoXXxXoxxxoXxoXoxXXX"
		  "xxXXxxxxxoxxXxxXXooXxxxoxxxoXoxXXxxoooxxxoXxoXXx+xxXXXXXXXxXXXoXxxxXxXxXXxxXxXoxXxoXXXXx+xXoxxxXXxXoxXoxXoxoXXxxxxoxXXxxxXXxx+oX"
		  "XXXXxXXXXX/xXXxoXXxxxxXxoxxXxXxoxoxXxXXxXxxxxooxoXxoxXxXxXxXXX/XXxxXXxxxXxxXxx+xxXo+oxoxXxxxxxXoXxooxxXXxxxxX+XXxXxXxXXxxxXoXXxX"
		  "XXxxxXoxxxoxoooxXxXoxoXxXXXxxxXXoXoXxXxxXoXXXXXxxXXxX+xxxxXXoxxxXxXxxXxxXXxo+oxooxxoxxoXxxx/+XoXxoxXxxXXxxXXxxXxxxXoxxxXxxooxxxX"
		  "XXxXxxxxxXxoxXXxxX/xxoooxoXXXXxXxXoXooxx+xXXxXXXXXxxoXxoXXxxxoXXxxXx+xxxXxXXxXXxxxXXXxXXoXXXXoxo+xxXXXoXxxXoXx/X+XXXxxXXxxxxXXxX"
		  "xxoXXox/X/xxxoxXxxXXxXxxXxXXxXxxXxXoXxxoxX+XXXXXoXxxxxxxXXoxxxoxxxXoXXXxXoXxXoXxxXXXxXoXXXxXo/oxxxoXxxXXXXxxXxxXoxX+XXXx+xXXXXxx"
		  "x/xXooxx+xxxxxXxoXoXooXXXxXxxooxXxXxxXXXxxxxoXoXxxxoxXxX+xXxXoXXxxoxxXxxXXxXxXXXoXXxxXXXxoXXxXXxXxxXxxoXxXXXXXXXXXXXXXXx+xxXXxXX"
		  "XXXXXoXXo+ooXxXxxXXXoXxoxxxoxXXxXxxxXoxxoXxXXXxXXxXXXxXoxx/x+xxxxXxXxxxxoXXXX=",
		  "Aaaaaaaaa: aaaa=aaaa@aa.aaaaa; aaaaaa-aaaaaaa=aaaaaa;\n"
		  " aaaaaaa=xXXxXXXxxxXXXXXxoxxxxXXXxXxxXXXxXxxXxxxXxxXoXoXXXxXXxXXxXoXoxxxXX/XxxXoXxXxxXoxXxoxxXxX/xx/oXoxxoXoxxXXxxxxxxXoxxxXXXXx"
		  "xXxXXXXXoooxXxxoxXxXXXXxxXXxXXoxoXxxXxxo/XxxXoXooXXxxXoxxX/XXXXXxoxXxxXXXxXXXXoXooxXoXxoxxXXXxXXX/xxXooxxoXxo/+xXxoxXXoXxxxXXXoo"
		  "XXXxxXXXxoxxxXxxxoxXxxxoxxxxxXxxxxXXXXoxoXXX/XxxxXXXo+/oxxxxXXxXxXXxxXXoXXXxxX+XXxxXxoxxxXxxXXoXXxXXxXxox+XxoXxxXxoXXoXXxoxoxoXX"
		  "XXXoXxXXXXXXoXXXoXxXXoxXXoXXxXXxoxXxXxXXoxxoXxXXxXXoxxxXXxoXxxxXXXXxXxxXXoXXxoxxXxxXXooXXXxXxxxxXXxxXX/XX+XX+xXx+xoXxxXooxXXxXox"
		  "xxxXXXxxxxxXxxoxX+/XxxXooxxxxxooxoxxXxxxxXXxxXxooxXooxoxxxxXxXXxxXXoxXoXxXXXxXxXxXXxxxXxxXXoxxxxxXXXxXXxxxXoxxoo+xXXXXxXXXXXXXxX"
		  "xxxXXXxXXXxxXXxXXXXXXXXXXXxXXXXXxXXXXxXXXXXXoXXo+ooXxXXXoXXxXoxxxoXxoXoxXXXxxXXxxxxxoxxXxxXXooXxxxoxxxoXoxXXxxoooxxxoXxoXXx+xxXX"
		  "XXXXXxXXXoXxxxXxXxXXxxXxXoxXxoXXXXx+xXoxxxXXxXoxXoxXoxoXXxxxxoxXXxxxXXxx+oXXXXXxXXXXX/xXXxoXXxxxxXxoxxXxXxoxoxXxXXxXxxxxooxoXxox"
		  "XxXxXxXXX/XXxxXXxxxXxxXxx+xxXo+oxoxXxxxxxXoXxooxxXXxxxxX+XXxXxXxXXxxxXoXXxXXXxxxXoxxxoxoooxXxXoxoXxXXX\n"
		  " xxxXXoXoXxXxxXoXXXXXxxXXxX+xxxxXXoxxxXxXxxXxxXXxo+oxooxxoxxoXxxx/+XoXxoxXxxXXxxXXxxXxxxXoxxxXxxooxxxXXXxXxxxxxXxoxXXxxX/xxooox"
		  "oXXXXxXxXoXooxx+xXXxXXXXXxxoXxoXXxxxoXXxxXx+xxxXxXXxXXxxxXXXxXXoXXXXoxo+xxXXXoXxxXoXx/X+XXXxxXXxxxxXXxXxxoXXox/X/xxxoxXxxXXxXxxX"
		  "xXXxXxxXxXoXxxoxX+XXXXXoXxxxxxxXXoxxxoxxxXoXXXxXoXxXoXxxXXXxXoXXXxXo/oxxxoXxxXXXXxxXxxXoxX+XXXx+xXXXXxxx/xXooxx+xxxxxXxoXoXooXXX"
		  "xXxxooxXxXxxXXXxxxxoXoXxxxoxXxX+xXxXoXXxxoxxXxxXXxXxXXXoXXxxXXXxoXXxXXxXxxXxxoXxXXXXXXXXXXXXXXx+xxXXxXXXXXXXoXXo+ooXxXxxXXXoXxox"
		  "xxoxXXxXxxxXoxxoXxXXXxXXxXXXxXoxx/x+xxxxXxXxxxxoXXXX=",
		  "Aaaaaaaaa: aaaa=aaaa@aa.aaaaa; aaaaaa-aaaaaaa=aaaaaa;"
		  " aaaaaaa=xXXxXXXxxxXXXXXxoxxxxXXXxXxxXXXxXxxXxxxXxxXoXoXXXxXXxXXxXoXoxxxXX/XxxXoXxXxxXoxXxoxxXxX/xx/oXoxxoXoxxXXxxxxxxXoxxxXXXXx"
		  "xXxXXXXXoooxXxxoxXxXXXXxxXXxXXoxoXxxXxxo/XxxXoXooXXxxXoxxX/XXXXXxoxXxxXXXxXXXXoXooxXoXxoxxXXXxXXX/xxXooxxoXxo/+xXxoxXXoXxxxXXXoo"
		  "XXXxxXXXxoxxxXxxxoxXxxxoxxxxxXxxxxXXXXoxoXXX/XxxxXXXo+/oxxxxXXxXxXXxxXXoXXXxxX+XXxxXxoxxxXxxXXoXXxXXxXxox+XxoXxxXxoXXoXXxoxoxoXX"
		  "XXXoXxXXXXXXoXXXoXxXXoxXXoXXxXXxoxXxXxXXoxxoXxXXxXXoxxxXXxoXxxxXXXXxXxxXXoXXxoxxXxxXXooXXXxXxxxxXXxxXX/XX+XX+xXx+xoXxxXooxXXxXox"
		  "xxxXXXxxxxxXxxoxX+/XxxXooxxxxxooxoxxXxxxxXXxxXxooxXooxoxxxxXxXXxxXXoxXoXxXXXxXxXxXXxxxXxxXXoxxxxxXXXxXXxxxXoxxoo+xXXXXxXXXXXXXxX"
		  "xxxXXXxXXXxxXXxXXXXXXXXXXXxXXXXXxXXXXxXXXXXXoXXo+ooXxXXXoXXxXoxxxoXxoXoxXXXxxXXxxxxxoxxXxxXXooXxxxoxxxoXoxXXxxoooxxxoXxoXXx+xxXX"
		  "XXXXXxXXXoXxxxXxXxXXxxXxXoxXxoXXXXx+xXoxxxXXxXoxXoxXoxoXXxxxxoxXXxxxXXxx+oXXXXXxXXXXX/xXXxoXXxxxxXxoxxXxXxoxoxXxXXxXxxxxooxoXxox"
		  "XxXxXxXXX/XXxxXXxxxXxxXxx+xxXo+oxoxXxxxxxXoXxooxxXXxxxxX+XXxXxXxXXxxxXoXXxXXXxxxXoxxxoxoooxXxXoxoXxXXX"
		  " xxxXXoXoXxXxxXoXXXXXxxXXxX+xxxxXXoxxxXxXxxXxxXXxo+oxooxxoxxoXxxx/+XoXxoxXxxXXxxXXxxXxxxXoxxxXxxooxxxXXXxXxxxxxXxoxXXxxX/xxooox"
		  "oXXXXxXxXoXooxx+xXXxXXXXXxxoXxoXXxxxoXXxxXx+xxxXxXXxXXxxxXXXxXXoXXXXoxo+xxXXXoXxxXoXx/X+XXXxxXXxxxxXXxXxxoXXox/X/xxxoxXxxXXxXxxX"
		  "xXXxXxxXxXoXxxoxX+XXXXXoXxxxxxxXXoxxxoxxxXoXXXxXoXxXoXxxXXXxXoXXXxXo/oxxxoXxxXXXXxxXxxXoxX+XXXx+xXXXXxxx/xXooxx+xxxxxXxoXoXooXXX"
		  "xXxxooxXxXxxXXXxxxxoXoXxxxoxXxX+xXxXoXXxxoxxXxxXXxXxXXXoXXxxXXXxoXXxXXxXxxXxxoXxXXXXXXXXXXXXXXx+xxXXxXXXXXXXoXXo+ooXxXxxXXXoXxox"
		  "xxoxXXxXxxxXoxxoXxXXXxXXxXXXxXoxx/x+xxxxXxXxxxxoXXXX=",
		  "Aaaaaaaaa: aaaa=aaaa@aa.aaaaa; aaaaaa-aaaaaaa=aaaaaa;\n"
		  " aaaaaaa=xXXxXXXxxxXXXXXxoxxxxXXXxXxxXXXxXxxXxxxXxxXoXoXXXxXXxXXxXoXoxxxXX/XxxXoXxXxxXoxXxoxxXxX/xx/oXoxxoXoxxXXxxxxxxXoxxxXXXXx"
		  "xXxXXXXXoooxXxxoxXxXXXXxxXXxXXoxoXxxXxxo/XxxXoXooXXxxXoxxX/XXXXXxoxXxxXXXxXXXXoXooxXoXxoxxXXXxXXX/xxXooxxoXxo/+xXxoxXXoXxxxXXXoo"
		  "XXXxxXXXxoxxxXxxxoxXxxxoxxxxxXxxxxXXXXoxoXXX/XxxxXXXo+/oxxxxXXxXxXXxxXXoXXXxxX+XXxxXxoxxxXxxXXoXXxXXxXxox+XxoXxxXxoXXoXXxoxoxoXX"
		  "XXXoXxXXXXXXoXXXoXxXXoxXXoXXxXXxoxXxXxXXoxxoXxXXxXXoxxxXXxoXxxxXXXXxXxxXXoXXxoxxXxxXXooXXXxXxxxxXXxxXX/XX+XX+xXx+xoXxxXooxXXxXox"
		  "xxxXXXxxxxxXxxoxX+/XxxXooxxxxxooxoxxXxxxxXXxxXxooxXooxoxxxxXxXXxxXXoxXoXxXXXxXxXxXXxxxXxxXXoxxxxxXXXxXXxxxXoxxoo+xXXXXxXXXXXXXxX"
		  "xxxXXXxXXXxxXXxXXXXXXXXXXXxXXXXXxXXXXxXXXXXXoXXo+ooXxXXXoXXxXoxxxoXxoXoxXXXxxXXxxxxxoxxXxxXXooXxxxoxxxoXoxXXxxoooxxxoXxoXXx+xxXX"
		  "XXXXXxXXXoXxxxXxXxXXxxXxXoxXxoXXXXx+xXoxxxXXxXoxXoxXoxoXXxxxxoxXXxxxXXxx+oXXXXXxXXXXX/xXXxoXXxxxxXxoxxXxXxoxoxXxXXxXxxxxooxoXxox"
		  "XxXxXxXXX/XXxxXXxxxXxxXxx+xxXo+oxoxXxxxxxXoXxooxxXXxxxxX+XXxXxXxXXxxxXoXXxXXXxxxXoxxxoxoooxXxXoxoXxXXX\n"
		  " xxxXXoXoXxXxxXoXXXXXxxXXxX+xxxxXXoxxxXxXxxXxxXXxo+oxooxxoxxoXxxx/+XoXxoxXxxXXxxXXxxXxxxXoxxxXxxooxxxXXXxXxxxxxXxoxXXxxX/xxooox"
		  "oXXXXxXxXoXooxx+xXXxXXXXXxxoXxoXXxxxoXXxxXx+xxxXxXXxXXxxxXXXxXXoXXXXoxo+xxXXXoXxxXoXx/X+XXXxxXXxxxxXXxXxxoXXox/X/xxxoxXxxXXxXxxX"
		  "xXXxXxxXxXoXxxoxX+XXXXXoXxxxxxxXXoxxxoxxxXoXXXxXoXxXoXxxXXXxXoXXXxXo/oxxxoXxxXXXXxxXxxXoxX+XXXx+xXXXXxxx/xXooxx+xxxxxXxoXoXooXXX"
		  "xXxxooxXxXxxXXXxxxxoXoXxxxoxXxX+xXxXoXXxxoxxXxxXXxXxXXXoXXxxXXXxoXXxXXxXxxXxxoXxXXXXXXXXXXXXXXx+xxXXxXXXXXXXoXXo+ooXxXxxXXXoXxox"
		  "xxoxXXxXxxxXoxxoXxXXXxXXxXXXxXoxx/x+xxxxXxXxxxxoXXXX=" },
		{ NULL,
		  "Long:\n"
		  "   789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:   789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:  \n"
		  " 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890" },
		{ NULL,
		  "Long:\n"
		  "\t \t789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:\t \t789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890",
		  "Long:\t \n"
		  "\t789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  " 123456789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_1234567890" },
		{ NULL,
		  "Long:\n"
		  "   \t  \t      789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  "   \t  \t     123456789_123456789_123456789_123456789_123456 \t 5123456789_123456789_123456789_123456789",
		  "Long:   \t  \t      789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789"
		  "   \t  \t     123456789_123456789_123456789_123456789_123456 \t 5123456789_123456789_123456789_123456789",
		  "Long:   \t  \t     \n"
		  " 789_123456789_123456789_123456789_1234567895123456789_123456789_123456789_123456789_123456789\n"
		  "   \t  \t     123456789_123456789_123456789_123456789_123456 \t\n"
		  " 5123456789_123456789_123456789_123456789" }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		gchar *folded, *folded2, *folded3, *unfolded;
		const gchar *expected_unfolded;
		const gchar *expected_folded2;

		if (data[ii].value) {
			folded = camel_header_fold (data[ii].value, strchr (data[ii].value, ':') - data[ii].value - 1);
			if (g_strcmp0 (folded, data[ii].folded) != 0)
				g_error ("Failed on [%d] (%s): returns '%s', expected '%s'",
					ii, data[ii].value, folded, data[ii].folded);
		} else {
			folded = g_strdup (data[ii].folded);
		}

		expected_unfolded = data[ii].unfolded;
		if (!expected_unfolded)
			expected_unfolded = data[ii].value;
		if (!expected_unfolded)
			expected_unfolded = data[ii].unfolded;
		unfolded = camel_header_unfold (folded);
		if (g_strcmp0 (unfolded, expected_unfolded) != 0)
			g_error ("Failed to unfold on [%d]: returns '%s', expected '%s'",
				ii, unfolded, expected_unfolded);
		g_free (unfolded);

		expected_folded2 = data[ii].folded2;
		if (!expected_folded2)
			expected_folded2 = folded;
		folded2 = camel_header_fold (folded, strchr (folded, ':') - folded - 1);
		if (g_strcmp0 (folded2, expected_folded2) != 0)
			g_error ("Failed fold2 of folded on [%d]: returns '%s', expected '%s'",
				ii, folded2, expected_folded2);

		folded3 = camel_header_fold (folded2, strchr (folded2, ':') - folded2 - 1);
		if (g_strcmp0 (folded3, expected_folded2) != 0)
			g_error ("Failed fold3 of folded on [%d]: returns '%s', expected '%s'",
				ii, folded3, expected_folded2);

		g_free (folded);
		g_free (folded2);
		g_free (folded3);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/Hostname/DetectBadChars", test_detect_hostname_bad_chars);
	g_test_add_func ("/Camel/Hostname/ConvertBadCharsEmail", test_convert_hostname_bad_chars_email);
	g_test_add_func ("/Camel/Hostname/ConvertBadCharsUrl", test_convert_hostname_bad_chars_url);
	g_test_add_func ("/Camel/Header/Folding", test_header_folding);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

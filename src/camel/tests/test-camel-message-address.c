/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "camel-test.h"
#include "messages.h"
#include "addresses.h"

/* for stat */
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <iconv.h>

#include "address-data.h"

static gchar *
convert_charset (const gchar *in,
                 const gchar *from,
                 const gchar *to)
{
	GIConv ic;
	gchar *out, *outp;
	const gchar *inp;
	gsize inlen, outlen;

	ic = g_iconv_open (to, from);

	if (ic == (GIConv) -1)
		return g_strdup (in);

	inlen = strlen (in);
	outlen = inlen * 5 + 16;

	outp = out = g_malloc (outlen);
	inp = in;

	if (camel_iconv (ic, &inp, &inlen, &outp, &outlen) == -1) {
		g_free (out);
		g_iconv_close (ic);
		return g_strdup (in);
	}

	if (camel_iconv (ic, NULL, 0, &outp, &outlen) == -1) {
		g_free (out);
		g_iconv_close (ic);
		return g_strdup (in);
	}

	g_iconv_close (ic);

	*outp = 0;

	return out;
}

static void
check_address_line_decode (gint i,
                           const gchar *line,
                           const gchar *name,
                           const gchar *email)
{
	CamelInternetAddress *addr;
	const gchar *dname, *demail;

	dname = NULL;
	demail = NULL;
	addr = camel_internet_address_new ();
	g_assert_true (camel_address_decode (CAMEL_ADDRESS (addr), line) == 1);
	g_assert_true (camel_internet_address_get (CAMEL_INTERNET_ADDRESS (addr), 0, &dname, &demail));
	if (!(g_strcmp0 (dname, name) == 0 || (!name && dname && !*dname)))
		g_error ("address line %d '%s': decoded name = '%s', but should be '%s'", i, line, dname, name);
	if (g_strcmp0 (demail, email) != 0)
		g_error ("address line %d '%s': decoded email = '%s', but should be '%s'", i, line, demail, email);
	g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
	g_clear_object (&addr);
}

#define to_utf8(in, type) convert_charset (in, type, "utf-8")
#define from_utf8(in, type) convert_charset (in, "utf-8", type)

static void
test_header_encode_phrase (void)
{
	struct _items {
		const gchar *input;
		const gchar *output;
	} items[] = {
		{ "a b c", "a b c" },
		{ "A\xc5\xa1" "A", "=?iso-8859-2?Q?A=B9A?=" },
		{ "B\xc3\xa9" "B", "=?ISO-8859-1?Q?B=E9B?=" },
		{ "C\xc3\xad", "=?ISO-8859-1?Q?C=ED?=" },
		{ "B\xc3\xa9" "B C\xc3\xad", "=?ISO-8859-1?Q?B=E9B_C=ED?=" },
		{ "A\xc5\xa1" "A B\xc3\xa9" "B C\xc3\xad", "=?UTF-8?Q?A=C5=A1A_B=C3=A9B_C=C3=AD?=" },
		{ "B\xc3\xa9" "B A\xc5\xa1" "A C\xc3\xad", "=?UTF-8?Q?B=C3=A9B_A=C5=A1A_C=C3=AD?=" },
		{ "B\xc3\xa9" "B C\xc3\xad A\xc5\xa1" "A", "=?UTF-8?Q?B=C3=A9B_C=C3=AD_A=C5=A1A?=" },
		{ "x A\xc5\xa1" "A B\xc3\xa9" "B C\xc3\xad", "x =?UTF-8?Q?A=C5=A1A_B=C3=A9B_C=C3=AD?=" },
		{ "B\xc3\xa9" "B A\xc5\xa1" "A C\xc3\xad y", "=?UTF-8?Q?B=C3=A9B_A=C5=A1A_C=C3=AD?= y" },
		{ "x B\xc3\xa9" "B C\xc3\xad A\xc5\xa1" "A y", "x =?UTF-8?Q?B=C3=A9B_C=C3=AD_A=C5=A1A?= y" },
		{ "\xf0\x9f\x90\x88", "=?UTF-8?Q?=F0=9F=90=88?=" },
		{ "\xf0\x9f\x90\x88 B\xc3\xa9" "B", "=?UTF-8?Q?=F0=9F=90=88_B=C3=A9B?=" },
		{ "B\xc3\xa9" "B \xf0\x9f\x90\x88", "=?UTF-8?Q?B=C3=A9B_=F0=9F=90=88?=" }
	};
	guint ii;
	gchar *str;

	for (ii = 0; ii < G_N_ELEMENTS (items); ii++) {
		str = camel_header_encode_phrase ((const guchar *) items[ii].input);
		if (g_ascii_strcasecmp (str, items[ii].output) != 0)
			g_error ("encode_phrase[%u]: returned = '%s' expected = '%s'", ii, str, items[ii].output);
		g_free (str);
	}
}

static void
test_header_encode_string (void)
{
	struct _items {
		const gchar *input;
		const gchar *output;
	} items[] = {
		{ "a b c", "a b c" },
		{ "A\xc5\xa1" "A", "=?iso-8859-2?Q?A=B9A?=" },
		{ "B\xc3\xa9" "B", "=?ISO-8859-1?Q?B=E9B?=" },
		{ "C\xc3\xad", "=?ISO-8859-1?Q?C=ED?=" },
		{ "B\xc3\xa9" "B C\xc3\xad", "=?ISO-8859-1?Q?B=E9B?= =?ISO-8859-1?Q?_C=ED?=" },
		{ "A\xc5\xa1" "A B\xc3\xa9" "B C\xc3\xad", "=?UTF-8?Q?A=C5=A1A?= =?UTF-8?Q?_B=C3=A9B?= =?UTF-8?Q?_C=C3=AD?=" },
		{ "B\xc3\xa9" "B A\xc5\xa1" "A C\xc3\xad", "=?UTF-8?Q?B=C3=A9B?= =?UTF-8?Q?_A=C5=A1A?= =?UTF-8?Q?_C=C3=AD?=" },
		{ "B\xc3\xa9" "B C\xc3\xad A\xc5\xa1" "A", "=?UTF-8?Q?B=C3=A9B?= =?UTF-8?Q?_C=C3=AD?= =?UTF-8?Q?_A=C5=A1A?=" },
		{ "x A\xc5\xa1" "A B\xc3\xa9" "B C\xc3\xad", "x =?UTF-8?Q?A=C5=A1A?= =?UTF-8?Q?_B=C3=A9B?= =?UTF-8?Q?_C=C3=AD?=" },
		{ "B\xc3\xa9" "B A\xc5\xa1" "A C\xc3\xad y", "=?UTF-8?Q?B=C3=A9B?= =?UTF-8?Q?_A=C5=A1A?= =?UTF-8?Q?_C=C3=AD?= y" },
		{ "x B\xc3\xa9" "B C\xc3\xad A\xc5\xa1" "A y", "x =?UTF-8?Q?B=C3=A9B?= =?UTF-8?Q?_C=C3=AD?= =?UTF-8?Q?_A=C5=A1A?= y" },
		{ "\xf0\x9f\x90\x88", "=?UTF-8?Q?=F0=9F=90=88?=" },
		{ "\xf0\x9f\x90\x88 B\xc3\xa9" "B", "=?UTF-8?Q?=F0=9F=90=88?= =?UTF-8?Q?_B=C3=A9B?=" },
		{ "B\xc3\xa9" "B \xf0\x9f\x90\x88", "=?UTF-8?Q?B=C3=A9B?= =?UTF-8?Q?_=F0=9F=90=88?=" }
	};
	guint ii;
	gchar *str;

	for (ii = 0; ii < G_N_ELEMENTS (items); ii++) {
		str = camel_header_encode_string ((const guchar *) items[ii].input);
		if (g_ascii_strcasecmp (str, items[ii].output) != 0)
			g_error ("encode_string[%u]: returned = '%s' expected = '%s'", ii, str, items[ii].output);
		g_free (str);
	}
}

static void
test_address_basics (void)
{
	CamelInternetAddress *addr, *addr2;
	const gchar *real, *where;
	gchar name[16], a[32];
	gint i;

	addr = camel_internet_address_new ();

	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == 0);
	g_assert_true (camel_internet_address_get (addr, 0, &real, &where) == FALSE);

	addr2 = CAMEL_INTERNET_ADDRESS (camel_address_new_clone (CAMEL_ADDRESS (addr)));
	test_address_compare (addr, addr2);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	camel_internet_address_add (addr, "Zed", "nowhere@here.com.au");
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == 1);
	g_assert_true (camel_internet_address_get (addr, 0, &real, &where) == TRUE);
	g_assert_true (string_equal ("Zed", real));
	g_assert_true (strcmp (where, "nowhere@here.com.au") == 0);

	addr2 = CAMEL_INTERNET_ADDRESS (camel_address_new_clone (CAMEL_ADDRESS (addr)));
	test_address_compare (addr, addr2);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	for (i = 1; i < 10; i++) {
		g_snprintf (name, sizeof (name), "Zed %d", i);
		g_snprintf (a, sizeof (a), "nowhere@here-%d.com.au", i);
		camel_internet_address_add (addr, name, a);
		g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == i + 1);
		g_assert_true (camel_internet_address_get (addr, i, &real, &where) == TRUE);
		g_assert_true (string_equal (name, real));
		g_assert_true (strcmp (where, a) == 0);
	}

	g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
	g_clear_object (&addr);
}

static void
test_address_search (void)
{
	CamelInternetAddress *addr;
	const gchar *where;
	gchar name[16], a[32];
	gint i;

	addr = camel_internet_address_new ();

	camel_internet_address_add (addr, "Zed", "nowhere@here.com.au");
	for (i = 1; i < 10; i++) {
		g_snprintf (name, sizeof (name), "Zed %d", i);
		g_snprintf (a, sizeof (a), "nowhere@here-%d.com.au", i);
		camel_internet_address_add (addr, name, a);
	}

	g_assert_true (camel_internet_address_find_name (addr, "Zed 1", &where) == 1);
	g_assert_true (camel_internet_address_find_name (addr, "Zed 9", &where) == 9);
	g_assert_true (camel_internet_address_find_name (addr, "Zed", &where) == 0);
	g_assert_true (camel_internet_address_find_name (addr, " Zed", &where) == 0);
	g_assert_true (camel_internet_address_find_name (addr, "Zed ", &where) == 0);
	g_assert_true (camel_internet_address_find_name (addr, "  Zed ", &where) == 0);
	g_assert_true (camel_internet_address_find_name (addr, "Zed 20", &where) == -1);
	g_assert_true (camel_internet_address_find_name (addr, "", &where) == -1);

	g_assert_true (camel_internet_address_find_address (addr, "nowhere@here-1.com.au", &where) == 1);
	g_assert_true (camel_internet_address_find_address (addr, "nowhere@here-1 . com.au", &where) == 1);
	g_assert_true (camel_internet_address_find_address (addr, "nowhere@here-2 .com.au ", &where) == 2);
	g_assert_true (camel_internet_address_find_address (addr, " nowhere @here-3.com.au", &where) == 3);
	g_assert_true (camel_internet_address_find_address (addr, "nowhere@here-20.com.au ", &where) == -1);
	g_assert_true (camel_internet_address_find_address (addr, "", &where) == -1);

	g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
	g_clear_object (&addr);
}

static void
test_address_copy_cat (void)
{
	CamelInternetAddress *addr, *addr2;
	gchar name[16], a[32];
	gint i;

	addr = camel_internet_address_new ();

	camel_internet_address_add (addr, "Zed", "nowhere@here.com.au");
	for (i = 1; i < 10; i++) {
		g_snprintf (name, sizeof (name), "Zed %d", i);
		g_snprintf (a, sizeof (a), "nowhere@here-%d.com.au", i);
		camel_internet_address_add (addr, name, a);
	}

	addr2 = CAMEL_INTERNET_ADDRESS (camel_address_new_clone (CAMEL_ADDRESS (addr)));
	test_address_compare (addr, addr2);

	camel_address_remove (CAMEL_ADDRESS (addr2), 0);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 9);
	camel_address_remove (CAMEL_ADDRESS (addr2), 0);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 8);
	camel_address_remove (CAMEL_ADDRESS (addr2), 5);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 7);
	camel_address_remove (CAMEL_ADDRESS (addr2), 10);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 7);
	camel_address_remove (CAMEL_ADDRESS (addr2), -1);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 0);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	/* clone + cat */
	addr2 = CAMEL_INTERNET_ADDRESS (camel_address_new_clone (CAMEL_ADDRESS (addr)));
	camel_address_cat (CAMEL_ADDRESS (addr2), CAMEL_ADDRESS (addr));
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == 10);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 20);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	/* cat + cat + copy */
	addr2 = camel_internet_address_new ();
	camel_address_cat (CAMEL_ADDRESS (addr2), CAMEL_ADDRESS (addr));
	test_address_compare (addr, addr2);
	camel_address_cat (CAMEL_ADDRESS (addr2), CAMEL_ADDRESS (addr));
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == 10);
	g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 20);
	camel_address_copy (CAMEL_ADDRESS (addr2), CAMEL_ADDRESS (addr));
	test_address_compare (addr, addr2);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	/* copy */
	addr2 = camel_internet_address_new ();
	camel_address_copy (CAMEL_ADDRESS (addr2), CAMEL_ADDRESS (addr));
	test_address_compare (addr, addr2);
	g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
	g_clear_object (&addr2);

	g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
	g_clear_object (&addr);
}

static void
test_address_i18n (void)
{
	CamelInternetAddress *addr, *addr2;
	const gchar *charset;
	const gchar *real, *where;
	gchar *enc, *enc2, *format, *format2;
	gchar *name;
	gchar *ptr;
	gint i;

	for (i = 0; i < G_N_ELEMENTS (test_lines); i++) {
		addr = camel_internet_address_new ();

		/* first, convert to api format (utf-8) */
		charset = test_lines[i].type;
		name = to_utf8 (test_lines[i].line, charset);

		/* remove new-line characters from the name, because they are truncated on decode */
		for (ptr = name; *ptr; ptr++) {
			if (*ptr == '\n' || *ptr == '\r')
				*ptr = ' ';
		}

		camel_internet_address_add (addr, name, "nobody@nowhere.com");
		g_assert_true (camel_internet_address_get (addr, 0, &real, &where) == TRUE);
		g_assert_true (string_equal (name, real));
		g_assert_true (strcmp (where, "nobody@nowhere.com") == 0);
		g_free (name);

		g_assert_true (camel_internet_address_get (addr, 1, &real, &where) == FALSE);
		g_assert_true (camel_address_length (CAMEL_ADDRESS (addr)) == 1);

		enc = camel_address_encode (CAMEL_ADDRESS (addr));

		addr2 = camel_internet_address_new ();
		g_assert_true (camel_address_decode (CAMEL_ADDRESS (addr2), enc) == 1);
		g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 1);

		enc2 = camel_address_encode (CAMEL_ADDRESS (addr2));
		g_assert_true (string_equal (enc, enc2));
		g_free (enc2);

		test_address_compare (addr, addr2);
		g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
		g_clear_object (&addr2);
		g_free (enc);

		/* format/unformat -- not guaranteed to be reversible, keep assertions but accept failures */
		format = camel_address_format (CAMEL_ADDRESS (addr));

		addr2 = camel_internet_address_new ();
		g_assert_true (camel_address_unformat (CAMEL_ADDRESS (addr2), format) == 1);
		g_assert_true (camel_address_length (CAMEL_ADDRESS (addr2)) == 1);

		format2 = camel_address_format (CAMEL_ADDRESS (addr2));
		g_assert_true (string_equal (format, format2));
		g_free (format2);

		/* currently format/unformat doesn't handle ,'s and other special chars at all */
		if (camel_address_length (CAMEL_ADDRESS (addr2)) == 1)
			test_address_compare (addr, addr2);

		g_free (format);

		g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
		g_clear_object (&addr2);

		g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
		g_clear_object (&addr);
	}
}

static void
test_address_i18n_decode (void)
{
	CamelInternetAddress *addr, *addr2;
	gchar *enc, *format;
	gint i;

	for (i = 0; i < G_N_ELEMENTS (test_address); i++) {
		addr = camel_internet_address_new ();
		g_assert_true (camel_address_decode (CAMEL_ADDRESS (addr), test_address[i].addr) == test_address[i].count);
		format = camel_address_format (CAMEL_ADDRESS (addr));
		g_assert_true (string_equal (format, test_address[i].utf8));
		g_free (format);

		addr2 = CAMEL_INTERNET_ADDRESS (camel_internet_address_new ());
		enc = camel_address_encode (CAMEL_ADDRESS (addr));
		g_assert_true (camel_address_decode (CAMEL_ADDRESS (addr2), enc) == test_address[i].count);
		g_free (enc);
		test_address_compare (addr, addr2);
		g_assert_cmpuint (G_OBJECT (addr2)->ref_count, ==, 1);
		g_clear_object (&addr2);

		g_assert_cmpuint (G_OBJECT (addr)->ref_count, ==, 1);
		g_clear_object (&addr);
	}
}

static void
test_address_line_decoder (void)
{
	const gchar *name, *email;
	gchar *line;
	gint i, jj;

	for (i = 0; i < G_N_ELEMENTS (test_decode); i++) {
		name = test_decode[i].name;
		email = test_decode[i].email;

		for (jj = 0; jj < G_N_ELEMENTS (line_decode_formats); jj++) {
			if (line_decode_formats[jj].without_name) {
				line = g_strdup_printf (line_decode_formats[jj].without_name, email);
				check_address_line_decode (i, line, NULL, email);
				g_free (line);
			}

			if (!name)
				continue;

			line = g_strdup_printf (line_decode_formats[jj].with_name, name, email);
			check_address_line_decode (i, line, name, email);
			g_free (line);
		}
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/Message/Address/Basics", test_address_basics);
	g_test_add_func ("/Camel/Message/Address/Search", test_address_search);
	g_test_add_func ("/Camel/Message/Address/CopyCatClone", test_address_copy_cat);
	g_test_add_func ("/Camel/Message/Address/I18N", test_address_i18n);
	g_test_add_func ("/Camel/Message/Address/I18NDecode", test_address_i18n_decode);
	g_test_add_func ("/Camel/Message/Address/LineDecoder", test_address_line_decoder);
	g_test_add_func ("/Camel/Message/Address/EncoderPhrase", test_header_encode_phrase);
	g_test_add_func ("/Camel/Message/Address/EncoderString", test_header_encode_string);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

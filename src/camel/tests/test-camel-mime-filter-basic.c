/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include "camel-test.h"

static gchar *
run_base64_enc (const guchar *data,
                const gsize *chunks,
                gsize n_chunks)
{
	CamelMimeFilter *filter;
	GString *result;
	gsize consumed;
	gsize ii;
	gchar *out;
	gsize outlen;
	gsize outpre;

	filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
	result = g_string_new (NULL);
	consumed = 0;
	out = NULL;
	outlen = 0;
	outpre = 0;

	for (ii = 0; ii < n_chunks; ii++) {
		camel_mime_filter_filter (
			filter, (const gchar *) (data + consumed), chunks[ii], 0,
			&out, &outlen, &outpre);
		g_string_append_len (result, out, (gssize) outlen);
		consumed += chunks[ii];
	}

	camel_mime_filter_complete (filter, "", 0, 0, &out, &outlen, &outpre);
	g_string_append_len (result, out, (gssize) outlen);

	g_object_unref (filter);

	return g_string_free (result, FALSE);
}

static void
assert_base64_enc (const guchar *data,
                   gsize len,
                   const gsize *chunks,
                   gsize n_chunks,
                   const gchar *expected)
{
	gchar *actual;

	actual = run_base64_enc (data, chunks, n_chunks);
	g_assert_cmpstr (actual, ==, expected);
	g_free (actual);
}

static void
test_base64_too_short (void)
{
	const guchar one[] = { 'A' };
	const guchar two[] = { 'A', 'B' };
	const guchar three[] = { 'A', 'B', 'C' };
	const gsize whole[] = { 1 };
	const gsize whole2[] = { 2 };
	const gsize whole3[] = { 3 };

	assert_base64_enc (one, 1, whole, 1, "QQ==\r\n");
	assert_base64_enc (two, 2, whole2, 1, "QUI=\r\n");
	assert_base64_enc (three, 3, whole3, 1, "QUJD\r\n");
}

static void
test_base64_empty (void)
{
	assert_base64_enc ((const guchar *) "", 0, NULL, 0, "");
}

static void
test_base64_mid_line (void)
{
	const guchar data[] = "Hello, World! This is Camel.";
	const gsize whole[] = { sizeof (data) - 1 };

	assert_base64_enc (
		data, sizeof (data) - 1, whole, 1,
		"SGVsbG8sIFdvcmxkISBUaGlzIGlzIENhbWVsLg==\r\n");
}

static guchar *
make_cycling_data (gsize len)
{
	guchar *data = g_malloc (len);
	gsize ii;

	for (ii = 0; ii < len; ii++) {
		data[ii] = (guchar) ('A' + (ii % 26));
	}

	return data;
}

static void
test_base64_line_boundary_single (void)
{
	guchar *data = make_cycling_data (57);
	const gsize whole[] = { 57 };

	assert_base64_enc (
		data, 57, whole, 1,
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n");

	g_free (data);
}

static void
test_base64_line_boundary_double (void)
{
	guchar *data = make_cycling_data (114);
	const gsize whole[] = { 114 };

	assert_base64_enc (
		data, 114, whole, 1,
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n"
		"RkdISUpLTE1OT1BRUlNUVVZXWFlaQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElK\r\n");

	g_free (data);
}

static void
test_base64_multi_line (void)
{
	guchar *data = make_cycling_data (150);
	const gsize whole[] = { 150 };

	assert_base64_enc (
		data, 150, whole, 1,
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n"
		"RkdISUpLTE1OT1BRUlNUVVZXWFlaQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElK\r\n"
		"S0xNTk9QUVJTVFVWV1hZWkFCQ0RFRkdISUpLTE1OT1BRUlNU\r\n");

	g_free (data);
}

static void
test_base64_chunked_arbitrary (void)
{
	guchar *data = make_cycling_data (150);
	const gsize chunks[] = { 7, 41, 1, 0, 59, 42 };
	const gchar *expected =
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n"
		"RkdISUpLTE1OT1BRUlNUVVZXWFlaQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElK\r\n"
		"S0xNTk9QUVJTVFVWV1hZWkFCQ0RFRkdISUpLTE1OT1BRUlNU\r\n";

	assert_base64_enc (data, 150, chunks, G_N_ELEMENTS (chunks), expected);

	g_free (data);
}

static void
test_base64_chunked_on_boundary (void)
{
	guchar *data = make_cycling_data (150);
	const gsize chunks[] = { 57, 93 };
	const gchar *expected =
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n"
		"RkdISUpLTE1OT1BRUlNUVVZXWFlaQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElK\r\n"
		"S0xNTk9QUVJTVFVWV1hZWkFCQ0RFRkdISUpLTE1OT1BRUlNU\r\n";

	assert_base64_enc (data, 150, chunks, G_N_ELEMENTS (chunks), expected);

	g_free (data);
}

static void
test_base64_chunked_mid_group (void)
{
	guchar *data = make_cycling_data (150);
	const gsize chunks[] = { 58, 92 };
	const gchar *expected =
		"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWkFCQ0RF\r\n"
		"RkdISUpLTE1OT1BRUlNUVVZXWFlaQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElK\r\n"
		"S0xNTk9QUVJTVFVWV1hZWkFCQ0RFRkdISUpLTE1OT1BRUlNU\r\n";

	assert_base64_enc (data, 150, chunks, G_N_ELEMENTS (chunks), expected);

	g_free (data);
}

static void
test_base64_reset (void)
{
	CamelMimeFilter *filter;
	gchar *out;
	gsize outlen, outpre;
	GString *first;
	GString *second;

	filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
	out = NULL;
	outlen = 0;
	outpre = 0;

	first = g_string_new (NULL);
	camel_mime_filter_filter (filter, "ABC", 3, 0, &out, &outlen, &outpre);
	g_string_append_len (first, out, (gssize) outlen);
	camel_mime_filter_complete (filter, "", 0, 0, &out, &outlen, &outpre);
	g_string_append_len (first, out, (gssize) outlen);

	g_assert_cmpstr (first->str, ==, "QUJD\r\n");

	camel_mime_filter_reset (filter);

	second = g_string_new (NULL);
	camel_mime_filter_filter (filter, "ABC", 3, 0, &out, &outlen, &outpre);
	g_string_append_len (second, out, (gssize) outlen);
	camel_mime_filter_complete (filter, "", 0, 0, &out, &outlen, &outpre);
	g_string_append_len (second, out, (gssize) outlen);

	g_assert_cmpstr (second->str, ==, "QUJD\r\n");

	g_string_free (first, TRUE);
	g_string_free (second, TRUE);
	g_object_unref (filter);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/too-short", test_base64_too_short);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/empty", test_base64_empty);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/mid-line", test_base64_mid_line);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/line-boundary-single", test_base64_line_boundary_single);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/line-boundary-double", test_base64_line_boundary_double);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/multi-line", test_base64_multi_line);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/chunked-arbitrary", test_base64_chunked_arbitrary);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/chunked-on-boundary", test_base64_chunked_on_boundary);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/chunked-mid-group", test_base64_chunked_mid_group);
	g_test_add_func ("/Camel/MimeFilterBasic/Base64Enc/reset", test_base64_reset);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}

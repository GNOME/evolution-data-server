/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
 * test-canon.c
 *
 * Test the CamelMimeFilterCanon class
 */

#include <stdio.h>
#include <string.h>

#include "camel-test.h"

#define d(x)

#define NUM_CASES 1
#define CHUNK_SIZE 4096

struct {
	gint flags;
	const gchar *in;
	const gchar *out;
} tests[] = {
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF,
	  "From \nRussia - with love.\n\n",
	  "=46rom \r\nRussia - with love.\r\n\r\n" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF,
	  "From \r\nRussia - with love.\r\n\n",
	  "=46rom \r\nRussia - with love.\r\n\r\n" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF,
	  "Tasmiania with fur    \nFrom",
	  "Tasmiania with fur    \r\nFrom" },
	{ CAMEL_MIME_FILTER_CANON_FROM,
	  "Tasmiania with fur    \nFrom",
	  "Tasmiania with fur    \nFrom" },
	{ CAMEL_MIME_FILTER_CANON_CRLF,
	  "Tasmiania with fur    \nFrom",
	  "Tasmiania with fur    \r\nFrom" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF,
	  "Tasmiania with fur    \nFrom here",
	  "Tasmiania with fur    \r\n=46rom here" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF | CAMEL_MIME_FILTER_CANON_STRIP,
	  "Tasmiania with fur    \nFrom here",
	  "Tasmiania with fur\r\n=46rom here" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF | CAMEL_MIME_FILTER_CANON_STRIP,
	  "Tasmiania with fur    \nFrom here\n",
	  "Tasmiania with fur\r\n=46rom here\r\n" },
	{ CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_CRLF | CAMEL_MIME_FILTER_CANON_STRIP,
	  "Tasmiania with fur    \nFrom here or there ? \n",
	  "Tasmiania with fur\r\n=46rom here or there ?\r\n" },
};

static void
test_case (gint test_num,
           gsize chunk_size)
{
	GOutputStream *memory_stream;
	GOutputStream *filter_stream;
	CamelMimeFilter *filter;
	const gchar *p;
	gchar *data;
	gsize size;
	gssize written;

	filter = camel_mime_filter_canon_new (tests[test_num].flags);
	memory_stream = g_memory_output_stream_new_resizable ();
	filter_stream = camel_filter_output_stream_new (memory_stream, filter);
	g_assert_cmpuint (G_OBJECT (filter)->ref_count, ==, 2);
	g_object_unref (filter);

	p = tests[test_num].in;
	while (*p) {
		gint w = MIN (strlen (p), chunk_size);

		written = g_output_stream_write (
			filter_stream, p, w, NULL, NULL);
		g_assert_cmpint (written, ==, w);
		p += w;
	}
	g_output_stream_flush (filter_stream, NULL, NULL);

	data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (memory_stream));
	size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (memory_stream));

	if (size != strlen (tests[test_num].out))
		g_error (
			"Buffer length mismatch: "
			"expected %d got %d\n or '%s' got '%.*s'",
			(gint) strlen (tests[test_num].out), (gint) size,
			tests[test_num].out, (gint) size, data);

	if (memcmp (data, tests[test_num].out, size) != 0)
		g_error (
			"Buffer mismatch: expected '%s' got '%.*s'",
			tests[test_num].out, (gint) size, data);

	g_assert_cmpuint (G_OBJECT (filter_stream)->ref_count, ==, 1);
	g_object_unref (filter_stream);
	g_assert_cmpuint (G_OBJECT (memory_stream)->ref_count, ==, 1);
	g_object_unref (memory_stream);
}

static void
test_canon_filter (void)
{
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (tests); ii++) {
		gsize chunk_size;

		/* try all write sizes */
		for (chunk_size = 1; chunk_size < 20; chunk_size++)
			test_case (ii, chunk_size);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/Canon/filter", test_canon_filter);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

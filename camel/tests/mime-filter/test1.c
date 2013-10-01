/*
 * test-crlf.c
 *
 * Test the CamelMimeFilterCanon class
 */

#include <stdio.h>
#include <string.h>

#include "camel-test.h"

#define d (x)

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

	filter = camel_mime_filter_canon_new (tests[test_num].flags);
	memory_stream = g_memory_output_stream_new_resizable ();
	filter_stream = camel_filter_output_stream_new (memory_stream, filter);
	check_unref (filter, 2);

	p = tests[test_num].in;
	while (*p) {
		gint w = MIN (strlen (p), chunk_size);

		check (g_output_stream_write (
			filter_stream, p, w, NULL, NULL) == w);
		p += w;
	}
	g_output_stream_flush (filter_stream, NULL, NULL);

	data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (memory_stream));
	size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (memory_stream));

	check_msg (
		size == strlen (tests[test_num].out),
		"Buffer length mismatch: "
		"expected %d got %d\n or '%s' got '%.*s'",
		strlen (tests[test_num].out), size,
		tests[test_num].out, size, data);
	check_msg (
		memcmp (data, tests[test_num].out, size) == 0,
		"Buffer mismatch: expected '%s' got '%.*s'",
		tests[test_num].out, size, data);

	check_unref (filter_stream, 1);
	check_unref (memory_stream, 1);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ii;

	camel_test_init (argc, argv);

	camel_test_start ("canonicalisation filter tests");

	for (ii = 0; ii < G_N_ELEMENTS (tests); ii++) {
		gsize chunk_size;

		camel_test_push ("Data test %d '%s'\n", ii, tests[ii].in);

		/* try all write sizes */
		for (chunk_size = 1; chunk_size < 20; chunk_size++) {
			camel_test_push ("Chunk size %d\n", chunk_size);
			test_case (ii, chunk_size);
			camel_test_pull ();
		}

		camel_test_pull ();
	}

	camel_test_end ();

	return 0;
}

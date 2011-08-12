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

gint
main (gint argc,
      gchar **argv)
{
	CamelStream *stream;
	CamelMimeFilter *sh;
	gint i;

	camel_test_init (argc, argv);

	camel_test_start ("canonicalisation filter tests");

	for (i = 0; i < G_N_ELEMENTS (tests); i++) {
		gint step;

		camel_test_push ("Data test %d '%s'\n", i, tests[i].in);

		/* try all write sizes */
		for (step = 1; step < 20; step++) {
			GByteArray *byte_array;
			CamelStream *out;
			const gchar *p;

			camel_test_push ("Chunk size %d\n", step);

			byte_array = g_byte_array_new ();
			out = camel_stream_mem_new_with_byte_array (byte_array);
			stream = camel_stream_filter_new (out);
			sh = camel_mime_filter_canon_new (tests[i].flags);
			check (camel_stream_filter_add (
				CAMEL_STREAM_FILTER (stream), sh) != -1);
			check_unref (sh, 2);

			p = tests[i].in;
			while (*p) {
				gint w = MIN (strlen (p), step);

				check (camel_stream_write (
					stream, p, w, NULL, NULL) == w);
				p += w;
			}
			camel_stream_flush (stream, NULL, NULL);

			check_msg (byte_array->len == strlen (tests[i].out), "Buffer length mismatch: expected %d got %d\n or '%s' got '%.*s'", strlen (tests[i].out), byte_array->len, tests[i].out, byte_array->len, byte_array->data);
			check_msg (0 == memcmp (byte_array->data, tests[i].out, byte_array->len), "Buffer mismatch: expected '%s' got '%.*s'", tests[i].out, byte_array->len, byte_array->data);
			check_unref (stream, 1);
			check_unref (out, 1);

			camel_test_pull ();
		}

		camel_test_pull ();
	}

	camel_test_end ();

	return 0;
}

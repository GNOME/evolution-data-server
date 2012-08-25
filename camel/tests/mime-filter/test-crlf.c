/*
  test - crlf.c
 *
  Test the CamelMimeFilterCrlf class
*/

#include <stdio.h>
#include <string.h>

#include "camel-test.h"

#define d(x)

#define NUM_CASES 1
#define CHUNK_SIZE 4096

enum {
	CRLF_ENCODE,
	CRLF_DECODE,
	CRLF_DONE
};

gint
main (gint argc,
      gchar **argv)
{
	CamelStream *source;
	CamelStream *correct;
	CamelStream *stream;
	CamelMimeFilter *sh;
	gchar *work;
	gint i;
	gssize comp_progress, comp_correct_chunk, comp_filter_chunk;
	gint comp_i;
	gchar comp_correct[CHUNK_SIZE], comp_filter[CHUNK_SIZE];

	camel_test_init (argc, argv);

	for (i = 0; i < NUM_CASES; i++) {
		gint j;

		work = g_strdup_printf ("CRLF/DOT filter, test case %d", i);
		camel_test_start (work);
		g_free (work);

		for (j = CRLF_ENCODE; j < CRLF_DONE; j++) {
			CamelMimeFilterCRLFDirection direction;
			gchar *infile = NULL, *outfile = NULL;

			switch (j) {
			case CRLF_ENCODE:
				camel_test_push ("Test of the encoder");
				direction = CAMEL_MIME_FILTER_CRLF_ENCODE;
				infile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, i + 1);
				outfile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, i + 1);
				break;
			case CRLF_DECODE:
				camel_test_push ("Test of the decoder");
				direction = CAMEL_MIME_FILTER_CRLF_DECODE;
				infile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, i + 1);
				outfile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, i + 1);
				break;
			default:
				break;
			}

			camel_test_push ("Initializing objects");
			source = camel_stream_fs_new_with_name (infile, 0, O_RDONLY, NULL);
			if (!source) {
				camel_test_fail ("Failed to open input case in \"%s\"", infile);
				g_free (infile);
				continue;
			}
			g_free (infile);

			correct = camel_stream_fs_new_with_name (outfile, 0, O_RDONLY, NULL);
			if (!correct) {
				camel_test_fail ("Failed to open correct output in \"%s\"", outfile);
				g_free (outfile);
				continue;
			}
			g_free (outfile);

			stream = camel_stream_filter_new (source);
			if (!stream) {
				camel_test_fail ("Couldn't create CamelStreamFilter??");
				continue;
			}

			sh = camel_mime_filter_crlf_new (direction, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
			if (!sh) {
				camel_test_fail ("Couldn't create CamelMimeFilterCrlf??");
				continue;
			}

			camel_stream_filter_add (
				CAMEL_STREAM_FILTER (stream), sh);
			camel_test_pull ();

			camel_test_push ("Running filter and comparing to correct result");

			comp_progress = 0;

			while (1) {
				comp_correct_chunk = camel_stream_read (
					correct, comp_correct,
					CHUNK_SIZE, NULL, NULL);
				comp_filter_chunk = 0;

				if (comp_correct_chunk == 0)
					break;

				while (comp_filter_chunk < comp_correct_chunk) {
					gssize delta;

					delta = camel_stream_read (
						stream,
						comp_filter + comp_filter_chunk,
						CHUNK_SIZE - comp_filter_chunk,
						NULL, NULL);

					if (delta == 0) {
						camel_test_fail ("Chunks are different sizes: correct is %d, "
							"filter is %d, %d bytes into stream",
							comp_correct_chunk, comp_filter_chunk, comp_progress);
					}

					comp_filter_chunk += delta;
				}

				for (comp_i = 0; comp_i < comp_filter_chunk; comp_i++) {
					if (comp_correct[comp_i] != comp_filter[comp_i]) {
						camel_test_fail ("Difference: correct is %c, filter is %c, "
							"%d bytes into stream",
							comp_correct[comp_i],
							comp_filter[comp_i],
							comp_progress + comp_i);
					}
				}

				comp_progress += comp_filter_chunk;
			}

			camel_test_pull ();

			/* inefficient */
			camel_test_push ("Cleaning up");
			g_object_unref (stream);
			g_object_unref (correct);
			g_object_unref (source);
			g_object_unref (sh);
			camel_test_pull ();

			camel_test_pull ();
		}

		camel_test_end ();
	}

	return 0;
}

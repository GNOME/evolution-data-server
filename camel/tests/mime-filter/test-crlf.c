/*
  test - crlf.c
 *
  Test the CamelMimeFilterCrlf class
*/

#include <stdio.h>
#include <string.h>

#include "camel-test.h"

#define d(x)

#define CHUNK_SIZE 4096

enum {
	CRLF_ENCODE,
	CRLF_DECODE,
	CRLF_DONE
};

static void
test_case (gint test_num)
{
	GFileInputStream *source_stream;
	GFileInputStream *correct_stream;
	GInputStream *filter_stream;
	CamelMimeFilter *filter;
	CamelMimeFilterCRLFDirection direction;
	GFile *file;
	gssize comp_progress, comp_correct_chunk, comp_filter_chunk;
	gint comp_i;
	gchar comp_correct[CHUNK_SIZE], comp_filter[CHUNK_SIZE];
	gchar *infile = NULL, *outfile = NULL;
	GError *local_error = NULL;

	switch (test_num) {
	case CRLF_ENCODE:
		camel_test_push ("Test of the encoder");
		direction = CAMEL_MIME_FILTER_CRLF_ENCODE;
		infile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, 1);
		outfile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, 1);
		break;
	case CRLF_DECODE:
		camel_test_push ("Test of the decoder");
		direction = CAMEL_MIME_FILTER_CRLF_DECODE;
		infile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, 1);
		outfile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, 1);
		break;
	default:
		break;
	}

	camel_test_push ("Initializing objects");

	file = g_file_new_for_path (infile);
	source_stream = g_file_read (file, NULL, &local_error);
	g_object_unref (file);

	/* Sanity check. */
	g_warn_if_fail (
		((source_stream != NULL) && (local_error == NULL)) ||
		((source_stream == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		camel_test_fail (
			"Failed to open input case in \"%s\": %s",
			infile, local_error->message);
		g_free (infile);
		return;
	}
	g_free (infile);

	file = g_file_new_for_path (outfile);
	correct_stream = g_file_read (file, NULL, &local_error);
	g_object_unref (file);

	/* Sanity check. */
	g_warn_if_fail (
		((correct_stream != NULL) && (local_error == NULL)) ||
		((correct_stream == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		camel_test_fail (
			"Failed to open correct output in \"%s\": %s",
			outfile, local_error->message);
		g_free (outfile);
		return;
	}
	g_free (outfile);

	filter = camel_mime_filter_crlf_new (
		direction, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	filter_stream = camel_filter_input_stream_new (
		G_INPUT_STREAM (source_stream), filter);
	g_object_unref (filter);

	camel_test_pull ();

	camel_test_push ("Running filter and comparing to correct result");

	comp_progress = 0;

	while (1) {
		comp_correct_chunk = g_input_stream_read (
			G_INPUT_STREAM (correct_stream),
			comp_correct, CHUNK_SIZE, NULL, NULL);
		comp_filter_chunk = 0;

		if (comp_correct_chunk == 0)
			break;

		while (comp_filter_chunk < comp_correct_chunk) {
			gssize delta;

			delta = g_input_stream_read (
				filter_stream,
				comp_filter + comp_filter_chunk,
				CHUNK_SIZE - comp_filter_chunk,
				NULL, NULL);

			if (delta == 0) {
				camel_test_fail (
					"Chunks are different sizes: "
					"correct is %d, "
					"filter is %d, "
					"%d bytes into stream",
					comp_correct_chunk,
					comp_filter_chunk,
					comp_progress);
			}

			comp_filter_chunk += delta;
		}

		for (comp_i = 0; comp_i < comp_filter_chunk; comp_i++) {
			if (comp_correct[comp_i] != comp_filter[comp_i]) {
				camel_test_fail (
					"Difference: "
					"correct is %c, "
					"filter is %c, "
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
	g_object_unref (correct_stream);
	g_object_unref (source_stream);
	g_object_unref (filter_stream);
	camel_test_pull ();

	camel_test_pull ();
}

gint
main (gint argc,
      gchar **argv)
{
	gchar *work;
	gint ii;

	camel_test_init (argc, argv);

	work = g_strdup_printf ("CRLF/DOT filter, test case %d", 0);
	camel_test_start (work);
	g_free (work);

	for (ii = CRLF_ENCODE; ii < CRLF_DONE; ii++)
		test_case (ii);

	camel_test_end ();

	return 0;
}

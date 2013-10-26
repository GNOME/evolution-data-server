/*
 * test - html.c
 *
 * Test the CamelMimeFilterToHTML class
 */

#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "camel-test.h"

#define d(x)

#define CHUNK_SIZE 4096

static void
test_filter (CamelMimeFilter *filter,
             GFile *infile,
             GFile *outfile)
{
	GFileInputStream *indisk;
	GFileInputStream *outdisk;
	GOutputStream *in;
	GOutputStream *out;
	GInputStream *in_filter_stream;
	GOutputStream *out_filter_stream;
	gchar *in_data;
	gsize in_size;
	gchar *out_data;
	gsize out_size;

	camel_test_push ("setup");

	indisk = g_file_read (infile, NULL, NULL);
	check (indisk != NULL);
	outdisk = g_file_read (outfile, NULL, NULL);
	check (outdisk != NULL);

	out = g_memory_output_stream_new_resizable ();
	check (g_output_stream_splice (
		out, G_INPUT_STREAM (outdisk),
		G_OUTPUT_STREAM_SPLICE_NONE, NULL, NULL) > 0);

	camel_test_pull ();

	camel_test_push ("reading through filter stream");

	in = g_memory_output_stream_new_resizable ();

	in_filter_stream = camel_filter_input_stream_new (
		G_INPUT_STREAM (indisk), filter);

	/* Leave the base stream open so we can re-read it. */
	g_filter_input_stream_set_close_base_stream (
		G_FILTER_INPUT_STREAM (in_filter_stream), FALSE);

	check_count (indisk, 2);
	check_count (filter, 2);

	check (g_output_stream_splice (
		in, in_filter_stream,
		G_OUTPUT_STREAM_SPLICE_NONE, NULL, NULL) > 0);

	in_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (in));
	in_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (in));

	out_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (out));
	out_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (out));

	check_msg (
		in_size == out_size &&
		memcmp (in_data, out_data, in_size) == 0,
		"Buffer content mismatch: "
		"%d != %d, in = '%.*s' != out = '%.*s'",
		in_size, out_size,
		in_size, in_data,
		out_size, out_data);

	camel_test_pull ();

	camel_mime_filter_reset (filter);

	check_unref (in_filter_stream, 1);
	check_count (indisk, 1);
	check_count (filter, 1);
	check_unref (in, 1);

	check (g_seekable_seek (
		G_SEEKABLE (indisk), 0, G_SEEK_SET, NULL, NULL));

	camel_test_push ("writing through filter stream");

	in = g_memory_output_stream_new_resizable ();

	out_filter_stream = camel_filter_output_stream_new (in, filter);
	check_count (in, 2);
	check_count (filter, 2);

	check (g_output_stream_splice (
		out_filter_stream, G_INPUT_STREAM (indisk),
		G_OUTPUT_STREAM_SPLICE_NONE, NULL, NULL) > 0);
	check (g_output_stream_flush (out_filter_stream, NULL, NULL));

	in_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (in));
	in_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (in));

	out_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (out));
	out_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (out));

	check_msg (
		in_size == out_size &&
		memcmp (in_data, out_data, in_size) == 0,
		"Buffer content mismatch: "
		"%d != %d, in = '%.*s' != out = '%.*s'",
		in_size, out_size,
		in_size, in_data,
		out_size, out_data);

	check_unref (out_filter_stream, 1);
	check_unref (in, 1);
	check_unref (indisk, 1);
	check_unref (outdisk, 1);
	check_unref (out, 1);

	camel_test_pull ();
}

gint
main (gint argc,
      gchar **argv)
{
	gint i;

	camel_test_init (argc, argv);

	camel_test_start ("HTML Stream filtering");

	for (i = 0; i < 100; i++) {
		gchar inname[32], outname[32];
		CamelMimeFilter *filter;
		GFile *infile;
		GFile *outfile;
		struct stat st;

		g_snprintf (inname, sizeof (inname), "data/html.%d.in", i);
		g_snprintf (outname, sizeof (outname), "data/html.%d.out", i);

		if (g_stat (inname, &st) == -1)
			break;

		filter = camel_mime_filter_tohtml_new (
			CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);

		infile = g_file_new_for_path (inname);
		outfile = g_file_new_for_path (outname);

		camel_test_push ("Data file '%s'", inname);

		test_filter (filter, infile, outfile);

		camel_test_pull ();

		g_object_unref (infile);
		g_object_unref (outfile);

		check_unref (filter, 1);
	}

	camel_test_end ();

	return 0;
}

/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
 * test-tohtml.c
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

	indisk = g_file_read (infile, NULL, NULL);
	g_assert_nonnull (indisk);
	outdisk = g_file_read (outfile, NULL, NULL);
	g_assert_nonnull (outdisk);

	out = g_memory_output_stream_new_resizable ();
	g_assert_true (g_output_stream_splice (
		out, G_INPUT_STREAM (outdisk),
		G_OUTPUT_STREAM_SPLICE_NONE, NULL, NULL) > 0);

	/* reading through filter stream */

	in = g_memory_output_stream_new_resizable ();

	in_filter_stream = camel_filter_input_stream_new (
		G_INPUT_STREAM (indisk), filter);

	/* Leave the base stream open so we can re-read it. */
	g_filter_input_stream_set_close_base_stream (
		G_FILTER_INPUT_STREAM (in_filter_stream), FALSE);

	g_assert_cmpuint (G_OBJECT (indisk)->ref_count, ==, 2);
	g_assert_cmpuint (G_OBJECT (filter)->ref_count, ==, 2);

	g_assert_true (g_output_stream_splice (
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

	if (in_size != out_size ||
	    memcmp (in_data, out_data, in_size) != 0)
		g_error (
			"Buffer content mismatch: "
			"%d != %d, in = '%.*s' != out = '%.*s'",
			(gint) in_size, (gint) out_size,
			(gint) in_size, in_data,
			(gint) out_size, out_data);

	camel_mime_filter_reset (filter);

	g_assert_cmpuint (G_OBJECT (in_filter_stream)->ref_count, ==, 1);
	g_object_unref (in_filter_stream);
	g_assert_cmpuint (G_OBJECT (indisk)->ref_count, ==, 1);
	g_assert_cmpuint (G_OBJECT (filter)->ref_count, ==, 1);
	g_assert_cmpuint (G_OBJECT (in)->ref_count, ==, 1);
	g_object_unref (in);

	g_assert_true (g_seekable_seek (
		G_SEEKABLE (indisk), 0, G_SEEK_SET, NULL, NULL));

	/* writing through filter stream */

	in = g_memory_output_stream_new_resizable ();

	out_filter_stream = camel_filter_output_stream_new (in, filter);
	g_assert_cmpuint (G_OBJECT (in)->ref_count, ==, 2);
	g_assert_cmpuint (G_OBJECT (filter)->ref_count, ==, 2);

	g_assert_true (g_output_stream_splice (
		out_filter_stream, G_INPUT_STREAM (indisk),
		G_OUTPUT_STREAM_SPLICE_NONE, NULL, NULL) > 0);
	g_assert_true (g_output_stream_flush (out_filter_stream, NULL, NULL));

	in_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (in));
	in_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (in));

	out_data = g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (out));
	out_size = g_memory_output_stream_get_data_size (
		G_MEMORY_OUTPUT_STREAM (out));

	if (in_size != out_size ||
	    memcmp (in_data, out_data, in_size) != 0)
		g_error (
			"Buffer content mismatch: "
			"%d != %d, in = '%.*s' != out = '%.*s'",
			(gint) in_size, (gint) out_size,
			(gint) in_size, in_data,
			(gint) out_size, out_data);

	g_assert_cmpuint (G_OBJECT (out_filter_stream)->ref_count, ==, 1);
	g_object_unref (out_filter_stream);
	g_assert_cmpuint (G_OBJECT (in)->ref_count, ==, 1);
	g_object_unref (in);
	g_assert_cmpuint (G_OBJECT (indisk)->ref_count, ==, 1);
	g_object_unref (indisk);
	g_assert_cmpuint (G_OBJECT (outdisk)->ref_count, ==, 1);
	g_object_unref (outdisk);
	g_assert_cmpuint (G_OBJECT (out)->ref_count, ==, 1);
	g_object_unref (out);
}

static void
test_tohtml_stream_filter (void)
{
	gint i;

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

		test_filter (filter, infile, outfile);

		g_object_unref (infile);
		g_object_unref (outfile);

		g_assert_cmpuint (G_OBJECT (filter)->ref_count, ==, 1);
		g_object_unref (filter);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/ToHTML/stream-filter", test_tohtml_stream_filter);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

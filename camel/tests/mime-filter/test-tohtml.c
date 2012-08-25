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
test_filter (CamelMimeFilter *f,
             const gchar *inname,
             const gchar *outname)
{
	CamelStream *in, *out;
	CamelStream *indisk, *outdisk, *filter;
	GByteArray *byte_array_in;
	GByteArray *byte_array_out;
	gint id;

	camel_test_push ("Data file '%s'", inname);

	camel_test_push ("setup");

	indisk = camel_stream_fs_new_with_name (inname, O_RDONLY, 0, NULL);
	check (indisk);
	outdisk = camel_stream_fs_new_with_name (outname, O_RDONLY, 0, NULL);
	check (outdisk);

	byte_array_out = g_byte_array_new ();
	out = camel_stream_mem_new_with_byte_array (byte_array_out);
	check (camel_stream_write_to_stream (outdisk, out, NULL, NULL) > 0);

	camel_test_pull ();

	camel_test_push ("reading through filter stream");

	byte_array_in = g_byte_array_new ();
	in = camel_stream_mem_new_with_byte_array (byte_array_in);

	filter = camel_stream_filter_new (indisk);
	check_count (indisk, 2);
	id = camel_stream_filter_add ((CamelStreamFilter *) filter, f);
	check_count (f, 2);

	check (camel_stream_write_to_stream (filter, in, NULL, NULL) > 0);
	check_msg (byte_array_in->len == byte_array_out->len
		&& memcmp (byte_array_in->data, byte_array_out->data, byte_array_in->len) == 0,
		"Buffer content mismatch, %d != %d, in = '%.*s' != out = '%.*s'", byte_array_in->len, byte_array_out->len,
		byte_array_in->len, byte_array_in->data, byte_array_out->len, byte_array_out->data);

	camel_test_pull ();

	camel_stream_filter_remove ((CamelStreamFilter *) filter, id);
	check_count (f, 1);
	camel_mime_filter_reset (f);

	check_unref (filter, 1);
	check_count (indisk, 1);
	check_count (f, 1);
	check_unref (in, 1);

	check (g_seekable_seek (
		G_SEEKABLE (indisk), 0, G_SEEK_SET, NULL, NULL));

	camel_test_push ("writing through filter stream");

	byte_array_in = g_byte_array_new ();
	in = camel_stream_mem_new_with_byte_array (byte_array_in);
	filter = camel_stream_filter_new (in);
	check_count (in, 2);
	id = camel_stream_filter_add ((CamelStreamFilter *) filter, f);
	check_count (f, 2);

	check (camel_stream_write_to_stream (indisk, filter, NULL, NULL) > 0);
	check (camel_stream_flush (filter, NULL, NULL) == 0);
	check_msg (byte_array_in->len == byte_array_out->len
		&& memcmp (byte_array_in->data, byte_array_out->data, byte_array_in->len) == 0,
		"Buffer content mismatch, %d != %d, in = '%.*s' != out = '%.*s'", byte_array_in->len, byte_array_out->len,
		byte_array_in->len, byte_array_in->data, byte_array_out->len, byte_array_out->data);

	camel_stream_filter_remove ((CamelStreamFilter *) filter, id);
	check_unref (filter, 1);
	check_unref (in, 1);
	check_unref (indisk, 1);
	check_unref (outdisk, 1);
	check_unref (out, 1);

	camel_test_pull ();

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
		CamelMimeFilter *f;
		struct stat st;

		sprintf (inname, "data/html.%d.in", i);
		sprintf (outname, "data/html.%d.out", i);

		if (g_stat (inname, &st) == -1)
			break;

		f = camel_mime_filter_tohtml_new (CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);

		test_filter (f, inname, outname);

		check_unref (f, 1);
	}

	camel_test_end ();

	return 0;
}

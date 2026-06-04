/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
 * test-crlf.c
 *
 * Test the CamelMimeFilterCrlf class
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
		direction = CAMEL_MIME_FILTER_CRLF_ENCODE;
		infile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, 1);
		outfile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, 1);
		break;
	case CRLF_DECODE:
		direction = CAMEL_MIME_FILTER_CRLF_DECODE;
		infile = g_strdup_printf ("%s/crlf-%d.out", SOURCEDIR, 1);
		outfile = g_strdup_printf ("%s/crlf-%d.in", SOURCEDIR, 1);
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	file = g_file_new_for_path (infile);
	source_stream = g_file_read (file, NULL, &local_error);
	g_object_unref (file);

	/* Sanity check. */
	g_warn_if_fail (
		((source_stream != NULL) && (local_error == NULL)) ||
		((source_stream == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_error (
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
		g_error (
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
				g_error (
					"Chunks are different sizes: "
					"correct is %d, "
					"filter is %d, "
					"%d bytes into stream",
					(gint) comp_correct_chunk,
					(gint) comp_filter_chunk,
					(gint) comp_progress);
			}

			comp_filter_chunk += delta;
		}

		for (comp_i = 0; comp_i < comp_filter_chunk; comp_i++) {
			if (comp_correct[comp_i] != comp_filter[comp_i]) {
				g_error (
					"Difference: "
					"correct is %c, "
					"filter is %c, "
					"%d bytes into stream",
					comp_correct[comp_i],
					comp_filter[comp_i],
					(gint) (comp_progress + comp_i));
			}
		}

		comp_progress += comp_filter_chunk;
	}

	g_object_unref (correct_stream);
	g_object_unref (source_stream);
	g_object_unref (filter_stream);
}

static void
dump_data (const gchar *what,
	   const gchar *data,
	   guint len)
{
	guint ii;

	printf ("%s %u bytes:\n", what, len);
	for (ii = 0; ii < len; ii++) {
		printf (" %02x", data[ii]);
		if (!((ii + 1) % 16) && ii + 1 < len)
			printf ("\n");
	}
	printf ("\n");
}

static gboolean
test_case_ensure_crlf_end_run (const gchar *in,
			       const gchar *expected,
			       gboolean ensure_crlf_end)
{
	CamelMimeFilter *filter;
	GInputStream *input_stream;
	GInputStream *filter_stream;
	gchar bytes[64];
	gsize bytes_read = 0;
	gboolean success = FALSE;

	if (strlen (expected) >= sizeof (bytes) - 1) {
		g_error ("Local buffer too small (%u bytes) to cover %u bytes",
			(guint) sizeof (bytes), (guint) strlen (expected));
		return FALSE;
	}

	input_stream = g_memory_input_stream_new_from_data (in, strlen (in), NULL);

	filter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_ENCODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS);
	camel_mime_filter_crlf_set_ensure_crlf_end (CAMEL_MIME_FILTER_CRLF (filter), ensure_crlf_end);
	filter_stream = camel_filter_input_stream_new (input_stream, filter);
	g_object_unref (filter);

	if (g_input_stream_read_all (filter_stream, bytes, sizeof (bytes) - 1, &bytes_read, NULL, NULL)) {
		bytes[bytes_read] = '\0';

		if (bytes_read == strlen (expected)) {
			success = memcmp (bytes, expected, bytes_read) == 0;

			if (!success)
				g_error ("Returned text '%s' and expected text '%s' do not match", bytes, expected);
		} else {
			dump_data ("   Wrote", in, strlen (in));
			dump_data ("   Read", bytes, bytes_read);
			dump_data ("   Expected", expected, strlen (expected));
			g_error ("Read %u bytes, but expected %u bytes", (guint) bytes_read, (guint) strlen (expected));
		}
	} else {
		g_error ("Failed to read up to %u bytes from the input stream", (guint) sizeof (bytes));
	}

	g_object_unref (filter_stream);
	g_object_unref (input_stream);

	return success;
}

static void
test_crlf_encode (void)
{
	test_case (CRLF_ENCODE);
}

static void
test_crlf_decode (void)
{
	test_case (CRLF_DECODE);
}

static void
test_crlf_ensure_crlf_end (void)
{
	struct _data {
		const gchar *in;
		const gchar *out_without;
		const gchar *out_with;
	} data[] = {
		{ "", "", "\r\n" },
		{ "a", "a", "a\r\n" },
		{ "a\n", "a\r\n", "a\r\n" },
		{ "a\r\n", "a\r\n", "a\r\n" },
		{ "a\r\nb", "a\r\nb", "a\r\nb\r\n" },
		{ "a\nb", "a\r\nb", "a\r\nb\r\n" },
		{ "a\r\nb\n", "a\r\nb\r\n", "a\r\nb\r\n" },
		{ "a\n\nb", "a\r\n\r\nb", "a\r\n\r\nb\r\n" },
		{ "\n", "\r\n", "\r\n" },
		{ "\r", "\r\n", "\r\n" },
		{ ".", "..", "..\r\n" },
		{ "\n.", "\r\n..", "\r\n..\r\n" },
		{ "\r.", "\r\n..", "\r\n..\r\n" },
		{ "\r\n.", "\r\n..", "\r\n..\r\n" },
		{ "a.b", "a.b", "a.b\r\n" },
		{ "\r.b", "\r\n..b", "\r\n..b\r\n" },
		{ "\n.b", "\r\n..b", "\r\n..b\r\n" },
		{ "\n.\rb", "\r\n..\r\nb", "\r\n..\r\nb\r\n" },
		{ "\n.\nb", "\r\n..\r\nb", "\r\n..\r\nb\r\n" },
		{ "\r.\nb", "\r\n..\r\nb", "\r\n..\r\nb\r\n" },
		{ "\r.\rb", "\r\n..\r\nb", "\r\n..\r\nb\r\n" },
		{ "a\r\nb\rc\nd\n\re\r\nf\ng\n\r\n\r\r\n\n\r",
		  "a\r\nb\r\nc\r\nd\r\n\r\ne\r\nf\r\ng\r\n\r\n\r\n\r\n\r\n\r\n",
		  "a\r\nb\r\nc\r\nd\r\n\r\ne\r\nf\r\ng\r\n\r\n\r\n\r\n\r\n\r\n" },
		{ "a\n\rb\nc\rd\r\ne\n\rf\rg\r\n\r\n\n\r\r\n",
		  "a\r\n\r\nb\r\nc\r\nd\r\ne\r\n\r\nf\r\ng\r\n\r\n\r\n\r\n\r\n",
		  "a\r\n\r\nb\r\nc\r\nd\r\ne\r\n\r\nf\r\ng\r\n\r\n\r\n\r\n\r\n" }
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (data); ii++) {
		if (!test_case_ensure_crlf_end_run (data[ii].in, data[ii].out_without, FALSE))
			break;

		if (!test_case_ensure_crlf_end_run (data[ii].in, data[ii].out_with, TRUE))
			break;
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/CRLF/encode", test_crlf_encode);
	g_test_add_func ("/Camel/CRLF/decode", test_crlf_decode);
	g_test_add_func ("/Camel/CRLF/ensure-crlf-end", test_crlf_ensure_crlf_end);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

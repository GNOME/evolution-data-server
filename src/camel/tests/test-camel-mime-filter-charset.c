/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
 * test-charset.c
 *
 * Test the CamelMimeFilterCharset class
 */

#include <stdio.h>
#include <string.h>

#include "camel-test.h"

#define d(x)

#define CHUNK_SIZE 4096

static void
test_case (const gchar *basename)
{
	GFileInputStream *source_stream;
	GFileInputStream *correct_stream;
	GInputStream *filter_stream;
	CamelMimeFilter *filter;
	GFile *file;
	gssize comp_progress, comp_correct_chunk, comp_filter_chunk;
	gchar comp_correct[CHUNK_SIZE], comp_filter[CHUNK_SIZE];
	gchar *filename, *charset, *work, *out_basename;
	const gchar *ext;
	gint i;
	GError *local_error = NULL;

	ext = strrchr (basename, '.');
	if (ext == NULL)
		return;

	work = g_strdup_printf (
		"Charset filter, test case (%s)", basename);
	g_test_message ("%s", work);
	g_free (work);

	filename = camel_test_get_data_file (basename);

	file = g_file_new_for_path (filename);
	source_stream = g_file_read (file, NULL, &local_error);
	g_object_unref (file);

	/* Sanity check. */
	g_warn_if_fail (
		((source_stream != NULL) && (local_error == NULL)) ||
		((source_stream == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_error (
			"Failed to open input case in \"%s\": %s",
			filename, local_error->message);
		g_error_free (local_error);
		g_free (filename);
		return;
	}
	g_free (filename);

	out_basename = g_strdup_printf ("%.*s.out", (gint) (ext - basename), basename);
	filename = camel_test_get_data_file (out_basename);
	g_free (out_basename);

	file = g_file_new_for_path (filename);
	correct_stream = g_file_read (file, NULL, &local_error);
	g_object_unref (file);

	/* Sanity check. */
	g_warn_if_fail (
		((correct_stream != NULL) && (local_error == NULL)) ||
		((correct_stream == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_error (
			"Failed to open correct output in \"%s\": %s",
			filename, local_error->message);
		g_error_free (local_error);
		g_free (filename);
		return;
	}
	g_free (filename);

	charset = g_strdup (basename + 8);
	ext = strchr (charset, '.');
	if (ext)
		*((gchar *) ext) = '\0';

	filter = camel_mime_filter_charset_new (charset, "UTF-8");
	if (filter == NULL) {
		g_error ("Couldn't create CamelMimeFilterCharset??");
		g_free (charset);
		return;
	}
	filter_stream = camel_filter_input_stream_new (
		G_INPUT_STREAM (source_stream), filter);
	g_clear_object (&filter);

	g_free (charset);

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

		for (i = 0; i < comp_filter_chunk; i++) {
			if (comp_correct[i] != comp_filter[i]) {
				g_error (
					"Difference: correct is %c, filter is %c, "
					"%d bytes into stream",
					comp_correct[i],
					comp_filter[i],
					(gint) (comp_progress + i));
			}
		}

		comp_progress += comp_filter_chunk;
	}

	g_object_unref (correct_stream);
	g_object_unref (source_stream);
	g_object_unref (filter_stream);
}

static void
test_charset_filter (gconstpointer user_data)
{
	const gchar *basename = user_data;

	test_case (basename);
}

static const gchar *test_cases[] = {
	"charset-gb2312.0.in",
	"charset-iso-2022-jp.0.in"
};

gint
main (gint argc,
      gchar **argv)
{
	gint ret;
	guint ii;

	camel_test_init (&argc, &argv);

	for (ii = 0; ii < G_N_ELEMENTS (test_cases); ii++) {
		gchar *test_path;

		test_path = g_strdup_printf ("/Camel/Charset/filter/%s", test_cases[ii]);
		if (g_str_has_suffix (test_path, ".in")) {
			test_path[strlen (test_path) - 3] = '\0';
		}
		g_test_add_data_func (test_path, test_cases[ii], test_charset_filter);
		g_free (test_path);
	}

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

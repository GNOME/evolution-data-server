/*
 * test-crlf.c
 *
 * Test the CamelMimeFilterCharset class
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "camel-test.h"

#define d(x)

#define CHUNK_SIZE 4096

gint
main (gint argc,
      gchar **argv)
{
	gssize comp_progress, comp_correct_chunk, comp_filter_chunk;
	gchar comp_correct[CHUNK_SIZE], comp_filter[CHUNK_SIZE];
	CamelStream *source;
	CamelStream *correct;
	CamelStream *stream;
	CamelMimeFilter *f;
	struct dirent *dent;
	gint i, test = 0;
	DIR *dir;

	camel_test_init (argc, argv);

	dir = opendir (SOURCEDIR);
	if (!dir)
		return 1;

	while ((dent = readdir (dir))) {
		gchar *infile, *outfile, *charset, *work;
		const gchar *ext;

		ext = strrchr (dent->d_name, '.');
		if (!(!strncmp (dent->d_name, "charset-", 8) && ext && !strcmp (ext, ".in")))
			continue;

		work = g_strdup_printf ("Charset filter, test case %d (%s)", test++, dent->d_name);
		camel_test_start (work);
		g_free (work);

		infile = g_strdup_printf ("%s/%s", SOURCEDIR, dent->d_name);
		if (!(source = camel_stream_fs_new_with_name (infile, 0, O_RDONLY, NULL))) {
			camel_test_fail ("Failed to open input case in \"%s\"", infile);
			g_free (outfile);
			continue;
		}
		g_free (infile);

		outfile = g_strdup_printf ("%s/%.*s.out", SOURCEDIR, ext - dent->d_name, dent->d_name);

		if (!(correct = camel_stream_fs_new_with_name (outfile, 0, O_RDONLY, NULL))) {
			camel_test_fail ("Failed to open correct output in \"%s\"", outfile);
			g_free (outfile);
			continue;
		}
		g_free (outfile);

		if (!(stream = camel_stream_filter_new (CAMEL_STREAM (source)))) {
			camel_test_fail ("Couldn't create CamelStreamFilter??");
			continue;
		}

		charset = g_strdup (dent->d_name + 8);
		ext = strchr (charset, '.');
		*((gchar *) ext) = '\0';

		if (!(f = camel_mime_filter_charset_new (charset, "UTF-8"))) {
			camel_test_fail ("Couldn't create CamelMimeFilterCharset??");
			g_free (charset);
			continue;
		}
		g_free (charset);

		camel_stream_filter_add (CAMEL_STREAM_FILTER (stream), f);
		g_object_unref (f);

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

			for (i = 0; i < comp_filter_chunk; i++) {
				if (comp_correct[i] != comp_filter[i]) {
					camel_test_fail ("Difference: correct is %c, filter is %c, "
						"%d bytes into stream",
						comp_correct[i],
						comp_filter[i],
						comp_progress + i);
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
		camel_test_pull ();

		camel_test_end ();
	}

	closedir (dir);

	return 0;
}

/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "camel-test.h"

#include <stdio.h>
#include <string.h>

static gchar *test_dir;

static void
camel_test_rm_rf (const gchar *path)
{
	GDir *dir;
	const gchar *entry;

	dir = g_dir_open (path, 0, NULL);
	if (!dir)
		return;

	while ((entry = g_dir_read_name (dir)) != NULL) {
		gchar *child;

		child = g_build_filename (path, entry, NULL);
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			camel_test_rm_rf (child);
		else
			g_unlink (child);
		g_free (child);
	}

	g_dir_close (dir);
	g_rmdir (path);
}

void
camel_test_init (void)
{
	GError *error = NULL;

	test_dir = g_dir_make_tmp ("camel-test-XXXXXX", &error);
	if (!test_dir)
		g_error ("Failed to create temp directory: %s", error->message);

	camel_init (test_dir, FALSE);
}

void
camel_test_shutdown (void)
{
	camel_shutdown ();

	if (test_dir) {
		camel_test_rm_rf (test_dir);
		g_clear_pointer (&test_dir, g_free);
	}
}

const gchar *
camel_test_get_dir (void)
{
	return test_dir;
}

/* compare strings, ignore whitespace though */
gint string_equal (const gchar *a, const gchar *b)
{
	const gchar *ap, *bp;

	ap = a;
	bp = b;

	while (*ap && *bp) {
		while (*ap == ' ' || *ap == '\n' || *ap == '\t')
			ap++;
		while (*bp == ' ' || *bp == '\n' || *bp == '\t')
			bp++;

		a = ap;
		b = bp;

		while (*ap && *ap != ' ' && *ap != '\n' && *ap != '\t')
			ap++;
		while (*bp && *bp != ' ' && *bp != '\n' && *bp != '\t')
			bp++;

		if (ap - a != bp - a
		    && ap - a > 0
		    && memcmp (a, b, ap - a) != 0) {
			return 0;
		}
	}

	return 1;
}

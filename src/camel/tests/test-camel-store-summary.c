/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include "camel-test.h"

static CamelFolderSummary *
connect_folder_summary (CamelStoreSummary *store_summary)
{
	CamelFolderSummary *folder_summary;

	camel_store_summary_set_filename (store_summary, "/nonexistent/test-camel-store-summary");
	g_assert_nonnull (camel_store_summary_add_from_path (store_summary, "f1"));

	folder_summary = camel_folder_summary_new (NULL);
	camel_folder_summary_set_flags (folder_summary, CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY);
	g_assert_true (camel_store_summary_connect_folder_summary (store_summary, "f1", folder_summary));

	return folder_summary;
}

static void
test_store_summary_gone_first (void)
{
	CamelStoreSummary *store_summary;
	CamelFolderSummary *folder_summary;
	CamelMessageInfo *mi;

	store_summary = camel_store_summary_new ();
	folder_summary = connect_folder_summary (store_summary);

	g_object_unref (store_summary);

	/* only compares the pointer, the store summary is freed */
	g_assert_cmpuint (g_signal_handler_find (folder_summary, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, store_summary), ==, 0);

	mi = camel_message_info_new (folder_summary);
	camel_message_info_set_uid (mi, "1");
	camel_folder_summary_add (folder_summary, mi, FALSE);
	g_object_unref (mi);

	g_object_unref (folder_summary);
}

static void
test_folder_summary_gone_first (void)
{
	CamelStoreSummary *store_summary;

	store_summary = camel_store_summary_new ();
	g_object_unref (connect_folder_summary (store_summary));
	g_object_unref (store_summary);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/StoreSummary/StoreSummaryGoneFirst", test_store_summary_gone_first);
	g_test_add_func ("/Camel/StoreSummary/FolderSummaryGoneFirst", test_folder_summary_gone_first);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}

/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include "camel-test.h"

typedef struct _LastUnrefData {
	guint n_emitted;
	gboolean finalized;
} LastUnrefData;

static void
summary_finalized_cb (gpointer user_data,
		      GObject *where_the_object_was)
{
	LastUnrefData *lud = user_data;

	/* the idle emits for both changes before the summary is freed */
	g_assert_cmpuint (lud->n_emitted, ==, 2);
	lud->finalized = TRUE;
}

static void
info_flags_changed_unref_cb (CamelFolderSummary *summary,
			     const gchar *uid,
			     guint32 new_flags,
			     gpointer user_data)
{
	LastUnrefData *lud = user_data;

	lud->n_emitted++;

	/* the owner drops the last reference while the idle emits */
	if (lud->n_emitted == 1)
		g_object_unref (summary);
}

static void
test_info_flags_changed_last_unref (void)
{
	CamelFolderSummary *summary;
	LastUnrefData lud = { 0, FALSE };
	const gchar *uids[] = { "1", "2" };
	guint ii;

	summary = camel_folder_summary_new (NULL);
	camel_folder_summary_set_flags (summary, CAMEL_FOLDER_SUMMARY_IN_MEMORY_ONLY);

	for (ii = 0; ii < G_N_ELEMENTS (uids); ii++) {
		CamelMessageInfo *mi;

		mi = camel_message_info_new (summary);
		camel_message_info_set_uid (mi, uids[ii]);
		camel_folder_summary_add (summary, mi, TRUE);
		g_object_unref (mi);

		g_assert_true (camel_folder_summary_replace_flags (summary, uids[ii], CAMEL_MESSAGE_SEEN));
	}

	g_object_weak_ref (G_OBJECT (summary), summary_finalized_cb, &lud);
	g_signal_connect (summary, "info-flags-changed", G_CALLBACK (info_flags_changed_unref_cb), &lud);

	while (!lud.finalized && g_main_context_iteration (NULL, FALSE)) {
	}

	g_assert_true (lud.finalized);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/FolderSummary/InfoFlagsChangedLastUnref", test_info_flags_changed_last_unref);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}

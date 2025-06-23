/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include <glib.h>

#include "camel/camel.h"

typedef struct _TestFolderThreadItem {
	gchar *uid;
	gchar *subject;
	guint64 message_id;
	const gchar *references_str; /* space-separated list of encoded message ID-s this message references  */
	gint64 dsent;
	gint64 dreceived;

	GArray *references;
} TestFolderThreadItem;

static TestFolderThreadItem *
test_folder_thread_item_new (const gchar *uid,
			     const gchar *subject,
			     guint64 message_id,
			     const gchar *references_str, /* space-separated list of message ID-s this message references  */
			     gint64 dsent,
			     gint64 dreceived)
{
	TestFolderThreadItem *item;

	item = g_new0 (TestFolderThreadItem, 1);
	item->uid = g_strdup (uid);
	item->subject = g_strdup (subject);
	item->message_id = message_id;
	item->dsent = dsent;
	item->dreceived = dreceived;

	if (references_str) {
		gchar **strv;
		guint ii;

		strv = g_strsplit (references_str, " ", -1);
		g_assert_nonnull (strv);

		item->references = g_array_new (FALSE, FALSE, sizeof (guint64));

		for (ii = 0; strv[ii]; ii++) {
			guint64 ref_id = g_ascii_strtoull (strv[ii], NULL, 10);
			g_array_append_val (item->references, ref_id);
		}

		g_strfreev (strv);
	}

	return item;
}

static void
test_folder_thread_item_free (gpointer ptr)
{
	TestFolderThreadItem *item = ptr;

	if (item) {
		g_free (item->uid);
		g_free (item->subject);
		g_clear_pointer (&item->references, g_array_unref);
		g_free (item);
	}
}

static const gchar *
test_folder_thread_item_get_uid (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->uid;
}

static const gchar *
test_folder_thread_item_get_subject (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->subject;
}

static guint64
test_folder_thread_item_get_message_id (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->message_id;
}

static const GArray *
test_folder_thread_item_get_references (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->references;
}

static gint64
test_folder_thread_item_get_date_sent (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->dsent;
}

static gint64
test_folder_thread_item_get_date_received (gconstpointer ptr)
{
	const TestFolderThreadItem *item = ptr;
	return item->dreceived;
}

static void
add_test_folder_thread_item (GPtrArray *dest, /* TestFolderThreadItem * */
			     const gchar *uid,
			     const gchar *subject,
			     guint64 message_id,
			     const gchar *references, /* space-separated list of encoded message ID-s this message references  */
			     gint64 dsent,
			     gint64 dreceived)
{
	TestFolderThreadItem *item;

	item = test_folder_thread_item_new (uid, subject, message_id, references, dsent, dreceived);
	g_ptr_array_add (dest, item);
}

static guint
test_folder_thread_count_nodes (CamelFolderThreadNode *node)
{
	guint count = 0;

	while (node) {
		CamelFolderThreadNode *child;

		count++;

		child = camel_folder_thread_node_get_child (node);
		if (child)
			count += test_folder_thread_count_nodes (child);

		node = camel_folder_thread_node_get_next (node);
	}

	return count;
}

/* it constructs the CamelFolderThread * preset to work with this test;
   it does not use CamelMessageInfo for simplicity */
static CamelFolderThread *
test_folder_thread_create_new (GPtrArray *items, /* TestFolderThreadItem * */
			       CamelFolderThreadFlags flags)
{
	return camel_folder_thread_new_items (items, flags,
		test_folder_thread_item_get_uid,
		test_folder_thread_item_get_subject,
		test_folder_thread_item_get_message_id,
		test_folder_thread_item_get_references,
		test_folder_thread_item_get_date_sent,
		test_folder_thread_item_get_date_received,
		NULL, NULL);
}

static void
test_folder_shufle_items (GPtrArray *array)
{
	guint ii, sz = array->len / 2;

	for (ii = 0; ii < sz; ii++) {
		gpointer ptr = array->pdata[ii];
		array->pdata[ii] = array->pdata[array->len - ii - 1];
		array->pdata[array->len - ii - 1] = ptr;
	}
}

static void
test_folder_thread_only_leaves (void)
{
	CamelFolderThread *thread;
	CamelFolderThreadNode *root, *node;
	TestFolderThreadItem *item;
	GPtrArray *items;

	items = g_ptr_array_new_with_free_func (test_folder_thread_item_free);

	/* there is no message ID "10", but they all group below one branch, with the oldest being the root */
	add_test_folder_thread_item (items, "2", "s2", 20, "10", 17000020, 170000200);
	add_test_folder_thread_item (items, "3", "s3", 30, "10", 17000030, 170000300);
	add_test_folder_thread_item (items, "4", "s4", 40, "10", 17000040, 170000400);
	add_test_folder_thread_item (items, "5", "s5", 50, "10", 17000050, 170000400);

	thread = test_folder_thread_create_new (items, CAMEL_FOLDER_THREAD_FLAG_NONE);
	g_assert_nonnull (thread);

	root = camel_folder_thread_get_tree (thread);
	g_assert_nonnull (root);
	g_assert_cmpuint (test_folder_thread_count_nodes (root), ==, 4);
	item = camel_folder_thread_node_get_item (root);
	g_assert_nonnull (item);
	g_assert_cmpstr (test_folder_thread_item_get_uid (item), ==, "2");
	g_assert_null (camel_folder_thread_node_get_next (root));
	node = camel_folder_thread_node_get_child (root);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));

	g_clear_object (&thread);

	thread = test_folder_thread_create_new (items, CAMEL_FOLDER_THREAD_FLAG_SORT);
	g_assert_nonnull (thread);

	root = camel_folder_thread_get_tree (thread);
	g_assert_nonnull (root);
	g_assert_cmpuint (test_folder_thread_count_nodes (root), ==, 4);
	item = camel_folder_thread_node_get_item (root);
	g_assert_nonnull (item);
	g_assert_cmpstr (test_folder_thread_item_get_uid (item), ==, "2");
	g_assert_null (camel_folder_thread_node_get_next (root));
	node = camel_folder_thread_node_get_child (root);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));

	g_clear_object (&thread);

	/* order of the items in the array should not matter */
	test_folder_shufle_items (items);

	thread = test_folder_thread_create_new (items, CAMEL_FOLDER_THREAD_FLAG_NONE);
	g_assert_nonnull (thread);

	root = camel_folder_thread_get_tree (thread);
	g_assert_nonnull (root);
	g_assert_cmpuint (test_folder_thread_count_nodes (root), ==, 4);
	item = camel_folder_thread_node_get_item (root);
	g_assert_nonnull (item);
	g_assert_cmpstr (test_folder_thread_item_get_uid (item), ==, "2");
	g_assert_null (camel_folder_thread_node_get_next (root));
	node = camel_folder_thread_node_get_child (root);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));

	g_clear_object (&thread);

	thread = test_folder_thread_create_new (items, CAMEL_FOLDER_THREAD_FLAG_SORT);
	g_assert_nonnull (thread);

	root = camel_folder_thread_get_tree (thread);
	g_assert_nonnull (root);
	g_assert_cmpuint (test_folder_thread_count_nodes (root), ==, 4);
	item = camel_folder_thread_node_get_item (root);
	g_assert_nonnull (item);
	g_assert_cmpstr (test_folder_thread_item_get_uid (item), ==, "2");
	g_assert_null (camel_folder_thread_node_get_next (root));
	node = camel_folder_thread_node_get_child (root);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));
	node = camel_folder_thread_node_get_next (node);
	g_assert_nonnull (node);
	g_assert_null (camel_folder_thread_node_get_child (node));

	g_clear_object (&thread);
	g_ptr_array_unref (items);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/CamelFolderThread/OnlyLeaves", test_folder_thread_only_leaves);

	return g_test_run ();
}

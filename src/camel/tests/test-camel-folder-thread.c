/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
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

/*
 * Thread tree:
 *   A->[] B->[A] C->[B,A] D->[C,A] E->[A] F->[A,B] G->[F] H->[G,C] I->[H,D]
 *
 * Dump what CamelFolderThread produces for various subsets.
 */

#define TID_A 1000
#define TID_B 2000
#define TID_C 3000
#define TID_D 4000
#define TID_E 5000
#define TID_F 6000
#define TID_G 7000
#define TID_H 8000
#define TID_I 9000

static void
dump_thread_node (CamelFolderThreadNode *node,
		  guint depth,
		  GString *out)
{
	while (node) {
		TestFolderThreadItem *item = camel_folder_thread_node_get_item (node);
		guint ii;

		for (ii = 0; ii < depth; ii++) {
			g_string_append (out, "  ");
		}

		if (item)
			g_string_append_printf (out, "%s(%u)\n",
				test_folder_thread_item_get_uid (item), depth);
		else
			g_string_append_printf (out, "<phantom>(%u)\n", depth);

		if (camel_folder_thread_node_get_child (node))
			dump_thread_node (camel_folder_thread_node_get_child (node),
				depth + 1, out);

		node = camel_folder_thread_node_get_next (node);
	}
}

static GPtrArray *
create_thread_items (const gchar *first_uid, ...)
{
	GPtrArray *items;
	va_list ap;
	const gchar *uid;

	items = g_ptr_array_new_with_free_func (test_folder_thread_item_free);

	uid = first_uid;
	va_start (ap, first_uid);

	while (uid) {
		guint64 mid = va_arg (ap, guint64);
		const gchar *refs_str = va_arg (ap, const gchar *);
		gint64 dsent = va_arg (ap, gint64);

		add_test_folder_thread_item (items, uid, uid, mid, refs_str, dsent, dsent);

		uid = va_arg (ap, const gchar *);
	}

	va_end (ap);

	return items;
}

static gchar *
get_thread_dump (GPtrArray *items)
{
	CamelFolderThread *thread;
	CamelFolderThreadNode *root;
	GString *out;

	thread = test_folder_thread_create_new (items, CAMEL_FOLDER_THREAD_FLAG_NONE);
	root = camel_folder_thread_get_tree (thread);

	out = g_string_new (NULL);
	dump_thread_node (root, 0, out);

	g_clear_object (&thread);

	return g_string_free (out, FALSE);
}

static void
test_folder_thread_complex_steps (void)
{
	GPtrArray *items;
	gchar *dump;

	/* refs format: space-separated guint64 values, parent-to-root order */
	#define REFS_B "1000"
	#define REFS_C "2000 1000"
	#define REFS_D "3000 1000"
	#define REFS_F "1000 2000"
	#define REFS_G "6000"
	#define REFS_H "7000 3000"
	#define REFS_I "8000 4000"

	#define VERIFY_STEP(label, expected_str) \
		dump = get_thread_dump (items); \
		g_assert_cmpstr (dump, ==, expected_str); \
		g_free (dump); \
		g_ptr_array_unref (items);

	/* Step 1: B, D, H */
	items = create_thread_items (
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		NULL);
	VERIFY_STEP ("step 1",
		"b(0)\n"
		"  h(1)\n"
		"  d(1)\n");

	/* Step 2: B, C, D, H */
	items = create_thread_items (
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		NULL);
	VERIFY_STEP ("step 2",
		"b(0)\n"
		"  c(1)\n"
		"    h(2)\n"
		"    d(2)\n");

	/* Step 4: B, C, D, F, H */
	items = create_thread_items (
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		NULL);
	VERIFY_STEP ("step 4",
		"b(0)\n"
		"  f(1)\n"
		"    c(2)\n"
		"      h(3)\n"
		"      d(3)\n");

	/* Step 5: B, C, D, F, G, H */
	items = create_thread_items (
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"g", (guint64) TID_G, REFS_G, (gint64) 7000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		NULL);
	VERIFY_STEP ("step 5",
		"b(0)\n"
		"  g(1)\n"
		"    h(2)\n"
		"  f(1)\n"
		"    c(2)\n"
		"      d(3)\n");

	/* Step 7: A, B, C, D, F, G, H */
	items = create_thread_items (
		"a", (guint64) TID_A, NULL, (gint64) 1000,
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"g", (guint64) TID_G, REFS_G, (gint64) 7000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		NULL);
	VERIFY_STEP ("step 7",
		"a(0)\n"
		"  f(1)\n"
		"    g(2)\n"
		"      h(3)\n"
		"  b(1)\n"
		"    c(2)\n"
		"      d(3)\n");

	/* Step 8: A, B, C, D, F, G, H, I */
	items = create_thread_items (
		"a", (guint64) TID_A, NULL, (gint64) 1000,
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"g", (guint64) TID_G, REFS_G, (gint64) 7000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		"i", (guint64) TID_I, REFS_I, (gint64) 9000,
		NULL);
	VERIFY_STEP ("step 8",
		"a(0)\n"
		"  f(1)\n"
		"    g(2)\n"
		"      h(3)\n"
		"        i(4)\n"
		"  b(1)\n"
		"    c(2)\n"
		"      d(3)\n");

	/* Step 9: A, B, C, D, F, G, I (H removed) */
	items = create_thread_items (
		"a", (guint64) TID_A, NULL, (gint64) 1000,
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"g", (guint64) TID_G, REFS_G, (gint64) 7000,
		"i", (guint64) TID_I, REFS_I, (gint64) 9000,
		NULL);
	VERIFY_STEP ("step 9",
		"a(0)\n"
		"  f(1)\n"
		"    g(2)\n"
		"  b(1)\n"
		"    c(2)\n"
		"      d(3)\n"
		"        i(4)\n");

	/* Step 11: A, B, C, D, F, H, I (G removed) */
	items = create_thread_items (
		"a", (guint64) TID_A, NULL, (gint64) 1000,
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		"i", (guint64) TID_I, REFS_I, (gint64) 9000,
		NULL);
	VERIFY_STEP ("step 11",
		"a(0)\n"
		"  f(1)\n"
		"  b(1)\n"
		"    c(2)\n"
		"      h(3)\n"
		"        i(4)\n"
		"      d(3)\n");

	/* Step 12: B, C, D, F, H, I (A removed) */
	items = create_thread_items (
		"b", (guint64) TID_B, REFS_B, (gint64) 2000,
		"c", (guint64) TID_C, REFS_C, (gint64) 3000,
		"d", (guint64) TID_D, REFS_D, (gint64) 4000,
		"f", (guint64) TID_F, REFS_F, (gint64) 6000,
		"h", (guint64) TID_H, REFS_H, (gint64) 8000,
		"i", (guint64) TID_I, REFS_I, (gint64) 9000,
		NULL);
	VERIFY_STEP ("step 12",
		"b(0)\n"
		"  f(1)\n"
		"    c(2)\n"
		"      h(3)\n"
		"        i(4)\n"
		"      d(3)\n");

	#undef VERIFY_STEP
	#undef REFS_B
	#undef REFS_C
	#undef REFS_D
	#undef REFS_F
	#undef REFS_G
	#undef REFS_H
	#undef REFS_I
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/Camel/CamelFolderThread/OnlyLeaves", test_folder_thread_only_leaves);
	g_test_add_func ("/Camel/CamelFolderThread/ComplexSteps", test_folder_thread_complex_steps);

	return g_test_run ();
}

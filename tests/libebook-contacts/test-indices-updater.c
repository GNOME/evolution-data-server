/*
 * SPDX-FileCopyrightText: (C) 2023 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <libebook-contacts/libebook-contacts.h>

#define TEST_TYPE_E_BOOK_INDICES_UPDATER (test_e_book_indices_updater_get_type ())

typedef struct _TestEBookIndicesUpdater {
	EBookIndicesUpdater parent;
} TestEBookIndicesUpdater;

typedef struct _TestEBookIndicesUpdaterClass {
	EBookIndicesUpdaterClass parent_class;
} TestEBookIndicesUpdaterClass;

GType test_e_book_indices_updater_get_type (void) G_GNUC_CONST;

G_DEFINE_TYPE (TestEBookIndicesUpdater, test_e_book_indices_updater, E_TYPE_BOOK_INDICES_UPDATER)

static void
test_e_book_indices_updater_class_init (TestEBookIndicesUpdaterClass *klass)
{
}

static void
test_e_book_indices_updater_init (TestEBookIndicesUpdater *self)
{
}

/* expects integers,-1 for G_MAXUINT, -2 as the terminator */
static void
test_verify_indices (EBookIndicesUpdater *updater,
		     gint first_index,
		     ...)
{
	const EBookIndices *indices = e_book_indices_updater_get_indices (updater);
	gint index = first_index;
	guint ii;
	va_list va;

	g_assert_nonnull (indices);

	va_start (va, first_index);

	for (ii = 0; indices[ii].chr != NULL && index != -2; ii++) {
		guint expected_index = index == -1 ? G_MAXUINT : (guint) index;

		g_assert_cmpuint (expected_index, ==, indices[ii].index);

		index = va_arg (va, gint);
	}

	va_end (va);

	g_assert_cmpint (index, ==, -2);
	g_assert_null (indices[ii].chr);
}

static void
test_indices_updater_set_indices (EBookIndicesUpdater *updater)
{
	const EBookIndices *set_indices;
	const EBookIndices default_indices[] = {
		{ (gchar *) "A", G_MAXUINT },
		{ (gchar *) "B", G_MAXUINT },
		{ (gchar *) "C", G_MAXUINT },
		{ (gchar *) "D", G_MAXUINT },
		{ (gchar *) "E", G_MAXUINT },
		{ (gchar *) "F", G_MAXUINT },
		{ (gchar *) "G", G_MAXUINT },
		{ NULL, G_MAXUINT }
	};
	guint ii;

	e_book_indices_updater_take_indices (updater, e_book_indices_copy (default_indices));

	set_indices = e_book_indices_updater_get_indices (updater);
	g_assert_nonnull (set_indices);

	for (ii = 0; set_indices[ii].chr && default_indices[ii].chr; ii++) {
		g_assert_cmpstr (set_indices[ii].chr, ==, default_indices[ii].chr);
		g_assert_cmpuint (set_indices[ii].index, ==, default_indices[ii].index);
	}

	g_assert_cmpuint (ii, ==, 7);
	g_assert_null (set_indices[ii].chr);
	g_assert_null (default_indices[ii].chr);
}

static void
test_indices_updater_basic_asc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = g_object_new (TEST_TYPE_E_BOOK_INDICES_UPDATER, NULL);

	test_indices_updater_set_indices (updater);
	e_book_indices_set_ascending_sort (updater, TRUE);

	changed = e_book_indices_updater_remove (updater, "non-existent");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add as the first */
	changed = e_book_indices_updater_add (updater, "1", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, -1, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add in the middle */
	changed = e_book_indices_updater_add (updater, "1", 3);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, 0, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add as the last */
	changed = e_book_indices_updater_add (updater, "1", 6);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add two at the same index */
	changed = e_book_indices_updater_add (updater, "1", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add before/after existing */
	changed = e_book_indices_updater_add (updater, "1", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 1);
	g_assert_true (changed);
	test_verify_indices (updater, -1, 0, 1, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "3", 1);
	g_assert_true (changed);
	test_verify_indices (updater, -1, 0, 2, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "3");
	g_assert_true (changed);
	test_verify_indices (updater, -1, 0, 1, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "3", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 1, 2, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "4", 4);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 1, 2, -1, 3, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "5", 4);
	g_assert_false (changed);
	test_verify_indices (updater, 0, 1, 2, -1, 3, -1, -1, -2);

	/* switch sort order forth and back */
	e_book_indices_set_ascending_sort (updater, FALSE);
	test_verify_indices (updater, 4, 3, 2, -1, 0, -1, -1, -2);

	e_book_indices_set_ascending_sort (updater, TRUE);
	test_verify_indices (updater, 0, 1, 2, -1, 3, -1, -1, -2);

	/* cleanup the content by removing ids */
	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 1, -1, 2, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "4");
	g_assert_false (changed);
	test_verify_indices (updater, 0, -1, 1, -1, 2, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "3");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, 1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "5");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_basic_desc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = g_object_new (TEST_TYPE_E_BOOK_INDICES_UPDATER, NULL);

	test_indices_updater_set_indices (updater);
	e_book_indices_set_ascending_sort (updater, FALSE);

	changed = e_book_indices_updater_remove (updater, "non-existent");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add as the first */
	changed = e_book_indices_updater_add (updater, "1", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, -1, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add in the middle */
	changed = e_book_indices_updater_add (updater, "1", 3);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, 0, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add as the last */
	changed = e_book_indices_updater_add (updater, "1", 6);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add two at the same index */
	changed = e_book_indices_updater_add (updater, "1", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	/* add before/after existing */
	changed = e_book_indices_updater_add (updater, "1", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2", 1);
	g_assert_true (changed);
	test_verify_indices (updater, -1, 1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "3", 1);
	g_assert_false (changed);
	test_verify_indices (updater, -1, 1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "3");
	g_assert_false (changed);
	test_verify_indices (updater, -1, 1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "3", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 2, 1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "4", 4);
	g_assert_true (changed);
	test_verify_indices (updater, 3, 2, 1, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "5", 4);
	g_assert_true (changed);
	test_verify_indices (updater, 4, 3, 2, -1, 0, -1, -1, -2);

	/* switch sort order forth and back */
	e_book_indices_set_ascending_sort (updater, TRUE);
	test_verify_indices (updater, 0, 1, 2, -1, 3, -1, -1, -2);

	e_book_indices_set_ascending_sort (updater, FALSE);
	test_verify_indices (updater, 4, 3, 2, -1, 0, -1, -1, -2);

	/* cleanup the content by removing ids */
	changed = e_book_indices_updater_remove (updater, "2");
	g_assert_true (changed);
	test_verify_indices (updater, 3, -1, 2, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "4");
	g_assert_true (changed);
	test_verify_indices (updater, 2, -1, 1, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "3");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 1, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "1");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, 0, -1, -1, -2);

	changed = e_book_indices_updater_remove (updater, "5");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_complex_asc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = g_object_new (TEST_TYPE_E_BOOK_INDICES_UPDATER, NULL);

	test_indices_updater_set_indices (updater);
	e_book_indices_set_ascending_sort (updater, TRUE);

	/* more complex adding */
	changed = e_book_indices_updater_add (updater, "2a", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2b", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2c", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "5a", 5);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, 3, -1, -2);

	changed = e_book_indices_updater_add (updater, "5b", 5);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, 3, -1, -2);

	changed = e_book_indices_updater_add (updater, "0a", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 1, -1, -1, 4, -1, -2);

	changed = e_book_indices_updater_add (updater, "0b", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 2, -1, -1, 5, -1, -2);

	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 2, -1, -1, 5, 7, -2);

	changed = e_book_indices_updater_remove (updater, "6");
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 2, -1, -1, 5, -1, -2);

	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 2, -1, -1, 5, 7, -2);

	changed = e_book_indices_updater_remove (updater, "non-existent");
	g_assert_false (changed);
	test_verify_indices (updater, 0, -1, 2, -1, -1, 5, 7, -2);

	changed = e_book_indices_updater_add (updater, "3", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 2, 5, -1, 6, 8, -2);

	changed = e_book_indices_updater_remove (updater, "0a");
	g_assert_true (changed);
	test_verify_indices (updater, 0, -1, 1, 4, -1, 5, 7, -2);

	changed = e_book_indices_updater_remove (updater, "0b");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, 3, -1, 4, 6, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_complex_desc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = g_object_new (TEST_TYPE_E_BOOK_INDICES_UPDATER, NULL);

	test_indices_updater_set_indices (updater);
	e_book_indices_set_ascending_sort (updater, FALSE);

	/* more complex adding */
	changed = e_book_indices_updater_add (updater, "2a", 2);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2b", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "2c", 2);
	g_assert_false (changed);
	test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);

	changed = e_book_indices_updater_add (updater, "5a", 5);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 1, -1, -1, 0, -1, -2);

	changed = e_book_indices_updater_add (updater, "5b", 5);
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 2, -1, -1, 0, -1, -2);

	changed = e_book_indices_updater_add (updater, "0a", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 5, -1, 2, -1, -1, 0, -1, -2);

	changed = e_book_indices_updater_add (updater, "0b", 0);
	g_assert_false (changed);
	test_verify_indices (updater, 5, -1, 2, -1, -1, 0, -1, -2);

	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 6, -1, 3, -1, -1, 1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "6");
	g_assert_true (changed);
	test_verify_indices (updater, 5, -1, 2, -1, -1, 0, -1, -2);

	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 6, -1, 3, -1, -1, 1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "non-existent");
	g_assert_false (changed);
	test_verify_indices (updater, 6, -1, 3, -1, -1, 1, 0, -2);

	changed = e_book_indices_updater_add (updater, "3", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 7, -1, 4, 3, -1, 1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "0a");
	g_assert_false (changed);
	test_verify_indices (updater, 7, -1, 4, 3, -1, 1, 0, -2);

	changed = e_book_indices_updater_remove (updater, "0b");
	g_assert_true (changed);
	test_verify_indices (updater, -1, -1, 4, 3, -1, 1, 0, -2);

	g_object_unref (updater);
}

static EBookIndicesUpdater *
test_prepare_moves (gboolean ascending_sort)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = g_object_new (TEST_TYPE_E_BOOK_INDICES_UPDATER, NULL);

	test_indices_updater_set_indices (updater);
	e_book_indices_set_ascending_sort (updater, ascending_sort);

	changed = e_book_indices_updater_add (updater, "2a", 2);
	g_assert_true (changed);
	changed = e_book_indices_updater_add (updater, "2b", 2);
	g_assert_false (changed);
	changed = e_book_indices_updater_add (updater, "2c", 2);
	g_assert_false (changed);
	changed = e_book_indices_updater_add (updater, "5a", 5);
	g_assert_true (changed);
	changed = e_book_indices_updater_add (updater, "5b", 5);
	if (ascending_sort)
		g_assert_false (changed);
	else
		g_assert_true (changed);
	changed = e_book_indices_updater_add (updater, "0a", 0);
	g_assert_true (changed);
	changed = e_book_indices_updater_add (updater, "0b", 0);
	if (ascending_sort)
		g_assert_true (changed);
	else
		g_assert_false (changed);
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	changed = e_book_indices_updater_add (updater, "3", 3);
	g_assert_true (changed);

	if (ascending_sort)
		test_verify_indices (updater, 0, -1, 2, 5, -1, 6, 8, -2);
	else
		test_verify_indices (updater, 7, -1, 4, 3, -1, 1, 0, -2);

	return updater;
}

static void
test_indices_updater_moves_asc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = test_prepare_moves (TRUE);

	/* move item 3 to index 1 */
	changed = e_book_indices_updater_add (updater, "3", 1);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 6, 8, -2);

	/* move item 6 to index 3 */
	changed = e_book_indices_updater_add (updater, "6", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, 6, -1, 7, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 6, 8, -2);

	/* move item 6 to index 0 */
	changed = e_book_indices_updater_add (updater, "6", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 3, 4, -1, -1, 7, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 6, 8, -2);

	/* move item 6 to index 2 */
	changed = e_book_indices_updater_add (updater, "6", 2);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 7, -1, -2);

	/* move item 6 to index 3 */
	changed = e_book_indices_updater_add (updater, "6", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, 6, -1, 7, -1, -2);

	/* move item 6 to index 2 */
	changed = e_book_indices_updater_add (updater, "6", 2);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 7, -1, -2);

	/* move item 6 to index 4 */
	changed = e_book_indices_updater_add (updater, "6", 4);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, 6, 7, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 0, 2, 3, -1, -1, 6, 8, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_moves_desc (void)
{
	EBookIndicesUpdater *updater;
	gboolean changed;

	updater = test_prepare_moves (FALSE);

	/* move item 3 to index 1 */
	changed = e_book_indices_updater_add (updater, "3", 1);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, -1, -1, 1, 0, -2);

	/* move item 6 to index 3 */
	changed = e_book_indices_updater_add (updater, "6", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, 2, -1, 0, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, -1, -1, 1, 0, -2);

	/* move item 6 to index 0 */
	changed = e_book_indices_updater_add (updater, "6", 0);
	g_assert_true (changed);
	test_verify_indices (updater, 6, 5, 2, -1, -1, 0, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, -1, -1, 1, 0, -2);

	/* move item 6 to index 2 */
	changed = e_book_indices_updater_add (updater, "6", 2);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 2, -1, -1, 0, -1, -2);

	/* move item 6 to index 3 */
	changed = e_book_indices_updater_add (updater, "6", 3);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, 2, -1, 0, -1, -2);

	/* move item 6 to index 2 */
	changed = e_book_indices_updater_add (updater, "6", 2);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 2, -1, -1, 0, -1, -2);

	/* move item 6 to index 4 */
	changed = e_book_indices_updater_add (updater, "6", 4);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, -1, 2, 0, -1, -2);

	/* move item 6 to index 6 */
	changed = e_book_indices_updater_add (updater, "6", 6);
	g_assert_true (changed);
	test_verify_indices (updater, 7, 6, 3, -1, -1, 1, 0, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_move_all_to_index (guint index,
					const gchar *items[9],
					gboolean ascending_sort)
{
	EBookIndicesUpdater *updater;
	guint ii;

	updater = test_prepare_moves (ascending_sort);

	for (ii = 0; ii < 9; ii++) {
		e_book_indices_updater_add (updater, items[ii], index);
	}

	switch (index) {
	case 0:
		test_verify_indices (updater, 0, -1, -1, -1, -1, -1, -1, -2);
		break;
	case 1:
		test_verify_indices (updater, -1, 0, -1, -1, -1, -1, -1, -2);
		break;
	case 2:
		test_verify_indices (updater, -1, -1, 0, -1, -1, -1, -1, -2);
		break;
	case 3:
		test_verify_indices (updater, -1, -1, -1, 0, -1, -1, -1, -2);
		break;
	case 4:
		test_verify_indices (updater, -1, -1, -1, -1, 0, -1, -1, -2);
		break;
	case 5:
		test_verify_indices (updater, -1, -1, -1, -1, -1, 0, -1, -2);
		break;
	case 6:
		test_verify_indices (updater, -1, -1, -1, -1, -1, -1, 0, -2);
		break;
	default:
		g_assert_not_reached ();
	}

	g_object_unref (updater);
}

static void
test_indices_updater_move_all (gboolean ascending_sort)
{
	/* semi-random orders in the middle of the left-right/right-left order */
	const gchar *items0[9] = { "0a", "0b", "2a", "2b", "2c", "3", "5a", "5b", "6" };
	const gchar *items1[9] = { "0b", "0a", "6", "2b", "2c", "2a", "3", "5a", "5b" };
	const gchar *items2[9] = { "3", "6", "0b", "2b", "2c", "0a", "5a", "5b", "2a" };
	const gchar *items3[9] = { "5b", "5a", "2a", "2b", "2c", "0a", "0b", "3", "6" };
	const gchar *items4[9] = { "6", "5b", "5a", "3", "2c", "2b", "2a", "0b", "0a" };
	guint ii;

	for (ii = 0; ii < 7; ii++) {
		test_indices_updater_move_all_to_index (ii, items0, ascending_sort);
		test_indices_updater_move_all_to_index (ii, items1, ascending_sort);
		test_indices_updater_move_all_to_index (ii, items2, ascending_sort);
		test_indices_updater_move_all_to_index (ii, items3, ascending_sort);
		test_indices_updater_move_all_to_index (ii, items4, ascending_sort);
	}
}

static void
test_indices_updater_move_all_asc (void)
{
	test_indices_updater_move_all (TRUE);
}

static void
test_indices_updater_move_all_desc (void)
{
	test_indices_updater_move_all (FALSE);
}

static void
test_indices_updater_remove_all_in_order (const gchar *items[9],
					  gboolean ascending_sort)
{
	EBookIndicesUpdater *updater;
	guint ii;

	updater = test_prepare_moves (ascending_sort);

	for (ii = 0; ii < 9; ii++) {
		e_book_indices_updater_remove (updater, items[ii]);
	}

	test_verify_indices (updater, -1, -1, -1, -1, -1, -1, -1, -2);

	g_object_unref (updater);
}

static void
test_indices_updater_remove_all (gboolean ascending_sort)
{
	/* semi-random orders in the middle of the left-right/right-left order */
	const gchar *items0[9] = { "0a", "0b", "2a", "2b", "2c", "3", "5a", "5b", "6" };
	const gchar *items1[9] = { "0b", "0a", "6", "2b", "2c", "2a", "3", "5a", "5b" };
	const gchar *items2[9] = { "3", "6", "0b", "2b", "2c", "0a", "5a", "5b", "2a" };
	const gchar *items3[9] = { "5b", "5a", "2a", "2b", "2c", "0a", "0b", "3", "6" };
	const gchar *items4[9] = { "6", "5b", "5a", "3", "2c", "2b", "2a", "0b", "0a" };

	test_indices_updater_remove_all_in_order (items0, ascending_sort);
	test_indices_updater_remove_all_in_order (items1, ascending_sort);
	test_indices_updater_remove_all_in_order (items2, ascending_sort);
	test_indices_updater_remove_all_in_order (items3, ascending_sort);
	test_indices_updater_remove_all_in_order (items4, ascending_sort);
}

static void
test_indices_updater_remove_all_asc (void)
{
	test_indices_updater_remove_all (TRUE);
}

static void
test_indices_updater_remove_all_desc (void)
{
	test_indices_updater_remove_all (FALSE);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/");

	g_test_add_func ("/EBookIndicesUpdater/BasicAsc", test_indices_updater_basic_asc);
	g_test_add_func ("/EBookIndicesUpdater/BasicDesc", test_indices_updater_basic_desc);
	g_test_add_func ("/EBookIndicesUpdater/ComplexAsc", test_indices_updater_complex_asc);
	g_test_add_func ("/EBookIndicesUpdater/ComplexDesc", test_indices_updater_complex_desc);
	g_test_add_func ("/EBookIndicesUpdater/MovesAsc", test_indices_updater_moves_asc);
	g_test_add_func ("/EBookIndicesUpdater/MovesDesc", test_indices_updater_moves_desc);
	g_test_add_func ("/EBookIndicesUpdater/MoveAllAsc", test_indices_updater_move_all_asc);
	g_test_add_func ("/EBookIndicesUpdater/MoveAllDesc", test_indices_updater_move_all_desc);
	g_test_add_func ("/EBookIndicesUpdater/RemoveAllAsc", test_indices_updater_remove_all_asc);
	g_test_add_func ("/EBookIndicesUpdater/RemoveAllDesc", test_indices_updater_remove_all_desc);

	return g_test_run ();
}

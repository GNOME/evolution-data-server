/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <fcntl.h>
#include <string.h>

#include "camel-test.h"

static gchar *
make_index_path (const gchar *name)
{
	return g_build_filename (camel_test_get_dir (), name, NULL);
}

static void
index_add_words_to_name (CamelIndex *idx,
                         const gchar *name,
                         const gchar * const *words)
{
	CamelIndexName *idn;
	gint ii;

	idn = camel_index_add_name (idx, name);
	g_assert_nonnull (idn);
	for (ii = 0; words[ii] != NULL; ii++) {
		camel_index_name_add_word (idn, words[ii]);
	}
	camel_index_write_name (idx, idn);
	g_object_unref (idn);
}

static gboolean
cursor_contains_name (CamelIndexCursor *idc,
                      const gchar *name)
{
	const gchar *val;

	while ((val = camel_index_cursor_next (idc)) != NULL) {
		if (g_strcmp0 (val, name) == 0)
			return TRUE;
	}

	return FALSE;
}

static gint
cursor_count (CamelIndexCursor *idc)
{
	gint count = 0;

	while (camel_index_cursor_next (idc) != NULL) {
		count++;
	}

	return count;
}

static void
test_create_and_check (void)
{
	CamelTextIndex *idx;
	gchar *path;
	gint ret;

	path = make_index_path ("idx-create");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);

	ret = camel_index_sync (CAMEL_INDEX (idx));
	g_assert_cmpint (ret, ==, 0);
	g_object_unref (idx);

	ret = camel_text_index_check (path);
	g_assert_cmpint (ret, ==, 0);

	camel_text_index_remove (path);
	g_free (path);
}

static void
test_add_name_and_has_name (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	gchar *path;

	path = make_index_path ("idx-hasname");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	g_assert_false (camel_index_has_name (ci, "msg1"));

	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_write_name (ci, idn);
	g_object_unref (idn);

	g_assert_true (camel_index_has_name (ci, "msg1"));
	g_assert_false (camel_index_has_name (ci, "msg2"));

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_add_word_and_find (void)
{
	const gchar *words[] = { "hello", "world", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-word");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "hello");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "world");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "nonexistent");
	g_assert_nonnull (idc);
	g_assert_cmpint (cursor_count (idc), ==, 0);
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_add_buffer (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	const gchar *text = "The quick brown fox jumps over the lazy dog";
	gchar *path;

	path = make_index_path ("idx-buffer");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_name_add_buffer (idn, text, strlen (text));
	camel_index_name_add_buffer (idn, NULL, 0);
	camel_index_write_name (ci, idn);
	g_object_unref (idn);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "quick");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "fox");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "lazy");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_words_cursor (void)
{
	const gchar *words_msg1[] = { "alpha", "beta", NULL };
	const gchar *words_msg2[] = { "gamma", "delta", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	GHashTable *found;
	const gchar *word;
	gchar *path;

	path = make_index_path ("idx-words");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words_msg1);
	index_add_words_to_name (ci, "msg2", words_msg2);
	camel_index_sync (ci);

	found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	idc = camel_index_words (ci);
	g_assert_nonnull (idc);
	while ((word = camel_index_cursor_next (idc)) != NULL) {
		g_hash_table_add (found, g_strdup (word));
	}
	g_object_unref (idc);

	g_assert_true (g_hash_table_contains (found, "alpha"));
	g_assert_true (g_hash_table_contains (found, "beta"));
	g_assert_true (g_hash_table_contains (found, "gamma"));
	g_assert_true (g_hash_table_contains (found, "delta"));

	g_hash_table_destroy (found);
	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_delete_name (void)
{
	const gchar *words[] = { "hello", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-delete");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words);
	index_add_words_to_name (ci, "msg2", words);
	camel_index_sync (ci);

	g_assert_true (camel_index_has_name (ci, "msg1"));
	camel_index_delete_name (ci, "msg1");
	g_assert_false (camel_index_has_name (ci, "msg1"));
	g_assert_true (camel_index_has_name (ci, "msg2"));

	camel_index_sync (ci);

	idc = camel_index_find (ci, "hello");
	g_assert_nonnull (idc);
	g_assert_false (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "hello");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg2"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_replace_name (void)
{
	const gchar *words_alpha[] = { "alpha", NULL };
	const gchar *words_beta[] = { "beta", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-replace");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words_alpha);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "alpha");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	index_add_words_to_name (ci, "msg1", words_beta);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "beta");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "alpha");
	g_assert_nonnull (idc);
	g_assert_false (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_persist_after_reopen (void)
{
	const gchar *words_msg1[] = { "persistent", NULL };
	const gchar *words_msg2[] = { "durable", "persistent", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-persist");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words_msg1);
	index_add_words_to_name (ci, "msg2", words_msg2);
	camel_index_sync (ci);
	g_object_unref (idx);

	idx = camel_text_index_new (path, O_RDWR);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	g_assert_true (camel_index_has_name (ci, "msg1"));
	g_assert_true (camel_index_has_name (ci, "msg2"));

	idc = camel_index_find (ci, "persistent");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "persistent");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg2"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "durable");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg2"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_compress (void)
{
	const gchar *words_keep[] = { "keep", NULL };
	const gchar *words_remove[] = { "remove", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-compress");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words_keep);
	index_add_words_to_name (ci, "msg2", words_remove);
	index_add_words_to_name (ci, "msg3", words_keep);
	camel_index_sync (ci);

	camel_index_delete_name (ci, "msg2");
	camel_index_sync (ci);

	g_assert_cmpint (camel_index_compress (ci), ==, 0);

	g_assert_true (camel_index_has_name (ci, "msg1"));
	g_assert_false (camel_index_has_name (ci, "msg2"));
	g_assert_true (camel_index_has_name (ci, "msg3"));

	idc = camel_index_find (ci, "keep");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "keep");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg3"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "remove");
	g_assert_nonnull (idc);
	g_assert_cmpint (cursor_count (idc), ==, 0);
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_rename (void)
{
	const gchar *words[] = { "word", NULL };
	CamelTextIndex *idx;
	gchar *path, *newpath;
	gint ret;

	path = make_index_path ("idx-rename-old");
	newpath = make_index_path ("idx-rename-new");

	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	index_add_words_to_name (CAMEL_INDEX (idx), "msg1", words);
	camel_index_sync (CAMEL_INDEX (idx));
	g_object_unref (idx);

	ret = camel_text_index_rename (path, newpath);
	g_assert_cmpint (ret, ==, 0);

	g_assert_cmpint (camel_text_index_check (newpath), ==, 0);
	g_assert_cmpint (camel_text_index_check (path), ==, -1);

	camel_text_index_remove (newpath);
	g_free (path);
	g_free (newpath);
}

static void
test_remove (void)
{
	CamelTextIndex *idx;
	gchar *path;

	path = make_index_path ("idx-remove");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	camel_index_sync (CAMEL_INDEX (idx));
	g_object_unref (idx);

	g_assert_cmpint (camel_text_index_check (path), ==, 0);
	g_assert_cmpint (camel_text_index_remove (path), ==, 0);
	g_assert_cmpint (camel_text_index_check (path), ==, -1);

	g_free (path);
}

static void
test_find_name_returns_null (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-findname");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_name_add_word (idn, "word");
	camel_index_write_name (ci, idn);
	g_object_unref (idn);
	camel_index_sync (ci);

	idc = camel_index_find_name (ci, "msg1");
	g_assert_null (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_long_word (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	gchar *path;
	gchar long_word[200];
	gchar *text;

	memset (long_word, 'x', sizeof (long_word) - 1);
	long_word[sizeof (long_word) - 1] = '\0';

	path = make_index_path ("idx-longword");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	text = g_strdup_printf ("%s short https://example.com/path/to/resource", long_word);
	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_name_add_buffer (idn, text, strlen (text));
	camel_index_name_add_buffer (idn, NULL, 0);
	camel_index_write_name (ci, idn);
	g_object_unref (idn);
	camel_index_sync (ci);
	g_free (text);

	idc = camel_index_find (ci, long_word);
	g_assert_nonnull (idc);
	g_assert_cmpint (cursor_count (idc), ==, 0);
	g_object_unref (idc);

	idc = camel_index_find (ci, "short");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "https");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "example");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_case_insensitive (void)
{
	const gchar *words[] = { "Hello", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-case");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "hello");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "HELLO");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_multiple_names_same_word (void)
{
	const gchar *words[] = { "common", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	GHashTable *found;
	const gchar *val;
	gint ii;
	gchar *path;

	path = make_index_path ("idx-multi");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	for (ii = 0; ii < 10; ii++) {
		gchar name[16];

		g_snprintf (name, sizeof (name), "msg%d", ii);
		index_add_words_to_name (ci, name, words);
	}
	camel_index_sync (ci);

	found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	idc = camel_index_find (ci, "common");
	g_assert_nonnull (idc);
	while ((val = camel_index_cursor_next (idc)) != NULL) {
		g_hash_table_add (found, g_strdup (val));
	}
	g_object_unref (idc);

	g_assert_cmpuint (g_hash_table_size (found), ==, 10);
	for (ii = 0; ii < 10; ii++) {
		gchar name[16];

		g_snprintf (name, sizeof (name), "msg%d", ii);
		g_assert_true (g_hash_table_contains (found, name));
	}

	g_hash_table_destroy (found);
	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static GString *
generate_unique_words_text (guint n_words,
                            const gchar *prefix)
{
	GString *text;
	guint ii;

	text = g_string_sized_new (n_words * 20);
	for (ii = 0; ii < n_words; ii++) {
		if (ii > 0)
			g_string_append_c (text, ' ');
		g_string_append_printf (text, "%sword%06u", prefix, ii);
	}

	return text;
}

static void
test_large_buffer_single_name (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	GString *text;
	gchar *path;
	guint n_words = 5000;
	guint ii;

	path = make_index_path ("idx-large");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	text = generate_unique_words_text (n_words, "big");

	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_name_add_buffer (idn, text->str, text->len);
	camel_index_name_add_buffer (idn, NULL, 0);
	camel_index_write_name (ci, idn);
	g_object_unref (idn);
	camel_index_sync (ci);

	for (ii = 0; ii < n_words; ii += 500) {
		gchar word[32];

		g_snprintf (word, sizeof (word), "bigword%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, "msg1"));
		g_object_unref (idc);
	}

	g_string_free (text, TRUE);
	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_large_buffer_many_names (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	GHashTable *found;
	const gchar *val;
	gchar *path;
	guint n_names = 200;
	guint words_per_name = 100;
	guint ii;

	path = make_index_path ("idx-largenames");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	for (ii = 0; ii < n_names; ii++) {
		GString *text;
		gchar name[32];
		gchar prefix[16];

		g_snprintf (name, sizeof (name), "uid%04u", ii);
		g_snprintf (prefix, sizeof (prefix), "n%04u", ii);
		text = generate_unique_words_text (words_per_name, prefix);
		g_string_append (text, " sharedterm");

		idn = camel_index_add_name (ci, name);
		g_assert_nonnull (idn);
		camel_index_name_add_buffer (idn, text->str, text->len);
		camel_index_name_add_buffer (idn, NULL, 0);
		camel_index_write_name (ci, idn);
		g_object_unref (idn);

		g_string_free (text, TRUE);
	}
	camel_index_sync (ci);

	found = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	idc = camel_index_find (ci, "sharedterm");
	g_assert_nonnull (idc);
	while ((val = camel_index_cursor_next (idc)) != NULL) {
		g_hash_table_add (found, g_strdup (val));
	}
	g_object_unref (idc);
	g_assert_cmpuint (g_hash_table_size (found), ==, n_names);
	g_hash_table_destroy (found);

	for (ii = 0; ii < n_names; ii += 50) {
		gchar word[32];
		gchar name[32];

		g_snprintf (word, sizeof (word), "n%04uword%06u", ii, 0);
		g_snprintf (name, sizeof (name), "uid%04u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, name));
		g_object_unref (idc);
	}

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_large_buffer_persist_reopen (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	GString *text;
	gchar *path;
	guint n_words = 3000;
	guint ii;

	path = make_index_path ("idx-largepersist");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	text = generate_unique_words_text (n_words, "pst");

	idn = camel_index_add_name (ci, "msg1");
	g_assert_nonnull (idn);
	camel_index_name_add_buffer (idn, text->str, text->len);
	camel_index_name_add_buffer (idn, NULL, 0);
	camel_index_write_name (ci, idn);
	g_object_unref (idn);
	camel_index_sync (ci);
	g_object_unref (idx);

	idx = camel_text_index_new (path, O_RDWR);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	g_assert_true (camel_index_has_name (ci, "msg1"));

	for (ii = 0; ii < n_words; ii += 300) {
		gchar word[32];

		g_snprintf (word, sizeof (word), "pstword%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, "msg1"));
		g_object_unref (idc);
	}

	g_string_free (text, TRUE);
	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_large_buffer_delete_and_compress (void)
{
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	GString *text;
	gchar *path;
	guint n_names = 100;
	guint words_per_name = 50;
	guint ii;

	path = make_index_path ("idx-largecompress");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	for (ii = 0; ii < n_names; ii++) {
		GString *body;
		gchar name[32];
		gchar prefix[16];

		g_snprintf (name, sizeof (name), "uid%04u", ii);
		g_snprintf (prefix, sizeof (prefix), "c%04u", ii);
		body = generate_unique_words_text (words_per_name, prefix);
		g_string_append (body, " commonword");

		idn = camel_index_add_name (ci, name);
		g_assert_nonnull (idn);
		camel_index_name_add_buffer (idn, body->str, body->len);
		camel_index_name_add_buffer (idn, NULL, 0);
		camel_index_write_name (ci, idn);
		g_object_unref (idn);

		g_string_free (body, TRUE);
	}
	camel_index_sync (ci);

	for (ii = 0; ii < n_names; ii += 2)	{
		gchar name[32];

		g_snprintf (name, sizeof (name), "uid%04u", ii);
		camel_index_delete_name (ci, name);
	}
	camel_index_sync (ci);

	g_assert_cmpint (camel_index_compress (ci), ==, 0);

	for (ii = 0; ii < n_names; ii++) {
		gchar name[32];
		gboolean expect;

		g_snprintf (name, sizeof (name), "uid%04u", ii);
		expect = (ii % 2) != 0;
		if (expect)
			g_assert_true (camel_index_has_name (ci, name));
		else
			g_assert_false (camel_index_has_name (ci, name));
	}

	idc = camel_index_find (ci, "commonword");
	g_assert_nonnull (idc);
	ii = cursor_count (idc);
	g_object_unref (idc);
	g_assert_cmpuint (ii, ==, n_names / 2);

	text = generate_unique_words_text (words_per_name, "c0001");
	for (ii = 0; ii < words_per_name; ii++) {
		gchar word[32];

		g_snprintf (word, sizeof (word), "c0001word%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, "uid0001"));
		g_object_unref (idc);
	}
	g_string_free (text, TRUE);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_boundary_entries (gconstpointer data)
{
	guint n_entries = GPOINTER_TO_UINT (data);
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexName *idn;
	CamelIndexCursor *idc;
	gchar path_suffix[64];
	gchar word[32];
	gchar name[32];
	gchar *path;
	guint n_names, ii, word_count;

	g_snprintf (path_suffix, sizeof (path_suffix), "idx-boundary-%u", n_entries);
	path = make_index_path (path_suffix);
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	/* Use two names so that removal of one still leaves entries
	 * in the partition table, exercising the split structure. */
	n_names = 2;
	for (ii = 0; ii < n_names; ii++) {
		guint jj;

		g_snprintf (name, sizeof (name), "msg%u", ii);
		idn = camel_index_add_name (ci, name);
		g_assert_nonnull (idn);
		for (jj = 0; jj < n_entries; jj++) {
			g_snprintf (word, sizeof (word), "bnd%06u", jj);
			camel_index_name_add_word (idn, word);
		}
		camel_index_write_name (ci, idn);
		g_object_unref (idn);
	}
	camel_index_sync (ci);

	/* Verify 1:1 mapping: word count must equal entry count */
	word_count = 0;
	idc = camel_index_words (ci);
	g_assert_nonnull (idc);
	while (camel_index_cursor_next (idc) != NULL) {
		word_count++;
	}
	g_object_unref (idc);
	g_assert_cmpuint (word_count, ==, n_entries);

	/* Verify every word is findable for both names */
	for (ii = 0; ii < n_entries; ii++) {
		g_snprintf (word, sizeof (word), "bnd%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_cmpint (cursor_count (idc), ==, (gint) n_names);
		g_object_unref (idc);
	}

	/* Remove one name -- partition blocks stay split, entries
	 * for msg1 are removed from the key-file data blocks but
	 * the partition key entries (word -> keyid) remain because
	 * msg0 still references those words. */
	camel_index_delete_name (ci, "msg1");
	camel_index_sync (ci);

	for (ii = 0; ii < n_entries; ii++) {
		g_snprintf (word, sizeof (word), "bnd%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, "msg0"));
		g_object_unref (idc);

		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_false (cursor_contains_name (idc, "msg1"));
		g_object_unref (idc);
	}

	/* Reopen and verify persistence across partition splits */
	g_object_unref (idx);
	idx = camel_text_index_new (path, O_RDWR);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	for (ii = 0; ii < n_entries; ii++) {
		g_snprintf (word, sizeof (word), "bnd%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_true (cursor_contains_name (idc, "msg0"));
		g_object_unref (idc);
	}

	/* Remove the remaining name and compress, which rebuilds
	 * the partition table from scratch */
	camel_index_delete_name (ci, "msg0");
	camel_index_sync (ci);
	g_assert_cmpint (camel_index_compress (ci), ==, 0);

	/* All words should now return empty cursors */
	for (ii = 0; ii < n_entries; ii += MAX (1, n_entries / 20)) {
		g_snprintf (word, sizeof (word), "bnd%06u", ii);
		idc = camel_index_find (ci, word);
		g_assert_nonnull (idc);
		g_assert_cmpint (cursor_count (idc), ==, 0);
		g_object_unref (idc);
	}

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static gchar *
normalize_reverse (CamelIndex *index,
                   const gchar *word,
                   gpointer user_data)
{
	gsize len = strlen (word);
	gchar *rev = g_malloc (len + 1);
	gsize ii;

	for (ii = 0; ii < len; ii++) {
		rev[ii] = word[len - 1 - ii];
	}
	rev[len] = '\0';

	return rev;
}

static void
test_set_normalize (void)
{
	const gchar *words[] = { "Hello", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	CamelIndexCursor *idc;
	gchar *path;

	path = make_index_path ("idx-normalize");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	camel_index_set_normalize (ci, normalize_reverse, NULL);

	index_add_words_to_name (ci, "msg1", words);
	camel_index_sync (ci);

	idc = camel_index_find (ci, "Hello");
	g_assert_nonnull (idc);
	g_assert_true (cursor_contains_name (idc, "msg1"));
	g_object_unref (idc);

	idc = camel_index_find (ci, "hello");
	g_assert_nonnull (idc);
	g_assert_cmpint (cursor_count (idc), ==, 0);
	g_object_unref (idc);

	g_object_unref (idx);
	camel_text_index_remove (path);
	g_free (path);
}

static void
test_delete_index (void)
{
	const gchar *words[] = { "data", NULL };
	CamelTextIndex *idx;
	CamelIndex *ci;
	gchar *path;
	gint ret;

	path = make_index_path ("idx-delete-index");
	idx = camel_text_index_new (path, O_RDWR | O_CREAT);
	g_assert_nonnull (idx);
	ci = CAMEL_INDEX (idx);

	index_add_words_to_name (ci, "msg1", words);
	camel_index_sync (ci);
	g_assert_true (camel_index_has_name (ci, "msg1"));

	ret = camel_index_delete (ci);
	g_assert_cmpint (ret, ==, 0);

	ret = camel_index_delete (ci);
	g_assert_cmpint (ret, ==, -1);

	g_object_unref (idx);

	g_assert_cmpint (camel_text_index_check (path), ==, -1);

	g_free (path);
}

gint
main (gint argc,
      gchar **argv)
{
	static const guint boundary_counts[] = {
		125, 126, 127, 128, 129, 130,
		253, 254, 255, 256, 257,
		510, 511, 512, 513, 514
	};
	gint ret;
	guint ii;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/TextIndex/create-and-check", test_create_and_check);
	g_test_add_func ("/Camel/TextIndex/add-name-and-has-name", test_add_name_and_has_name);
	g_test_add_func ("/Camel/TextIndex/add-word-and-find", test_add_word_and_find);
	g_test_add_func ("/Camel/TextIndex/add-buffer", test_add_buffer);
	g_test_add_func ("/Camel/TextIndex/words-cursor", test_words_cursor);
	g_test_add_func ("/Camel/TextIndex/delete-name", test_delete_name);
	g_test_add_func ("/Camel/TextIndex/replace-name", test_replace_name);
	g_test_add_func ("/Camel/TextIndex/persist-after-reopen", test_persist_after_reopen);
	g_test_add_func ("/Camel/TextIndex/compress", test_compress);
	g_test_add_func ("/Camel/TextIndex/rename", test_rename);
	g_test_add_func ("/Camel/TextIndex/remove", test_remove);
	g_test_add_func ("/Camel/TextIndex/find-name-returns-null", test_find_name_returns_null);
	g_test_add_func ("/Camel/TextIndex/long-word", test_long_word);
	g_test_add_func ("/Camel/TextIndex/case-insensitive", test_case_insensitive);
	g_test_add_func ("/Camel/TextIndex/multiple-names-same-word", test_multiple_names_same_word);
	g_test_add_func ("/Camel/TextIndex/set-normalize", test_set_normalize);
	g_test_add_func ("/Camel/TextIndex/delete-index", test_delete_index);
	g_test_add_func ("/Camel/TextIndex/large-buffer-single-name", test_large_buffer_single_name);
	g_test_add_func ("/Camel/TextIndex/large-buffer-many-names", test_large_buffer_many_names);
	g_test_add_func ("/Camel/TextIndex/large-buffer-persist-reopen", test_large_buffer_persist_reopen);
	g_test_add_func ("/Camel/TextIndex/large-buffer-delete-and-compress", test_large_buffer_delete_and_compress);

	for (ii = 0; ii < G_N_ELEMENTS (boundary_counts); ii++) {
		gchar *test_path;

		test_path = g_strdup_printf ("/Camel/TextIndex/boundary/%u-entries", boundary_counts[ii]);
		g_test_add_data_func (test_path, GUINT_TO_POINTER (boundary_counts[ii]), test_boundary_entries);
		g_free (test_path);
	}

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

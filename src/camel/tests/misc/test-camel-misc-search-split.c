/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <camel/camel-search-private.h>

#include "camel-test.h"

static struct {
	const gchar *word;
	gint count;
	struct {
		const gchar *word;
		gint type;
	} splits[5];
} split_tests[] = {
	{ "simple", 1, { { "simple", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "two words", 2, { { "two", CAMEL_SEARCH_WORD_SIMPLE }, {"words" , CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "compl;ex", 1, { { "compl;ex", CAMEL_SEARCH_WORD_COMPLEX } } },
	{ "compl;ex simple", 2, { { "compl;ex", CAMEL_SEARCH_WORD_COMPLEX} , {"simple", CAMEL_SEARCH_WORD_SIMPLE} } },
	{ "\"quoted\"", 1, { { "quoted", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "\"quoted double\"", 1, { { "quoted double", CAMEL_SEARCH_WORD_COMPLEX } } },
	{ "\"quoted double\" compl;ex", 2, { { "quoted double", CAMEL_SEARCH_WORD_COMPLEX }, { "compl;ex", CAMEL_SEARCH_WORD_COMPLEX } } },
	{ "\"quoted gdouble \\\" escaped\"", 1, { { "quoted gdouble \" escaped", CAMEL_SEARCH_WORD_COMPLEX } } },
	{ "\"quoted\\\"double\" \\\" escaped\\\"", 3, { { "quoted\"double", CAMEL_SEARCH_WORD_COMPLEX }, {"\"", CAMEL_SEARCH_WORD_COMPLEX}, { "escaped\"", CAMEL_SEARCH_WORD_COMPLEX } } },
	{ "\\\"escaped", 1, { { "\"escaped", CAMEL_SEARCH_WORD_COMPLEX } } },
};

static struct {
	const gchar *word;
	gint count;
	struct {
		const gchar *word;
		gint type;
	} splits[5];
} simple_tests[] = {
	{ "simple", 1, { {"simple", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "simpleCaSe", 1, { { "simplecase", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "two words", 2, { { "two", CAMEL_SEARCH_WORD_SIMPLE }, { "words", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "two wordscAsE", 2, { { "two", CAMEL_SEARCH_WORD_SIMPLE} ,  { "wordscase", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "compl;ex", 2, { { "compl", CAMEL_SEARCH_WORD_SIMPLE }, { "ex", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "compl;ex simple", 3, { { "compl", CAMEL_SEARCH_WORD_SIMPLE }, { "ex", CAMEL_SEARCH_WORD_SIMPLE }, { "simple", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "\"quoted compl;ex\" simple", 4, { { "quoted", CAMEL_SEARCH_WORD_SIMPLE}, { "compl", CAMEL_SEARCH_WORD_SIMPLE }, { "ex", CAMEL_SEARCH_WORD_SIMPLE }, { "simple", CAMEL_SEARCH_WORD_SIMPLE } } },
	{ "\\\" \"quoted\"compl;ex\" simple", 4, { { "quoted", CAMEL_SEARCH_WORD_SIMPLE}, { "compl", CAMEL_SEARCH_WORD_SIMPLE }, { "ex", CAMEL_SEARCH_WORD_SIMPLE }, { "simple", CAMEL_SEARCH_WORD_SIMPLE } } },
};

static void
test_search_splitting (void)
{
	struct _camel_search_words *words;
	gint i, j;

	for (i = 0; i < G_N_ELEMENTS (split_tests); i++) {
		words = camel_search_words_split ((const guchar *) split_tests[i].word);
		g_assert_true (words != NULL);
		if (words->len != split_tests[i].count)
			g_error ("split test %d '%s': words->len = %d, expected %d",
				i, split_tests[i].word, words->len, split_tests[i].count);

		for (j = 0; j < words->len; j++) {
			if (strcmp (split_tests[i].splits[j].word, words->words[j]->word) != 0)
				g_error ("split test %d '%s' word %d: got '%s', expected '%s'",
					i, split_tests[i].word, j,
					words->words[j]->word, split_tests[i].splits[j].word);
			g_assert_true (split_tests[i].splits[j].type == words->words[j]->type);
		}

		camel_search_words_free (words);
	}
}

static void
test_search_splitting_simple (void)
{
	struct _camel_search_words *words, *tmp;
	gint i, j;

	for (i = 0; i < G_N_ELEMENTS (simple_tests); i++) {
		tmp = camel_search_words_split ((const guchar *) simple_tests[i].word);
		g_assert_true (tmp != NULL);

		words = camel_search_words_simple (tmp);
		g_assert_true (words != NULL);
		if (words->len != simple_tests[i].count)
			g_error ("simple split test %d '%s': words->len = %d, expected %d",
				i, simple_tests[i].word, words->len, simple_tests[i].count);

		for (j = 0; j < words->len; j++) {
			if (strcmp (simple_tests[i].splits[j].word, words->words[j]->word) != 0)
				g_error ("simple split test %d '%s' word %d: got '%s', expected '%s'",
					i, simple_tests[i].word, j,
					words->words[j]->word, simple_tests[i].splits[j].word);
			g_assert_true (simple_tests[i].splits[j].type == words->words[j]->type);
		}

		camel_search_words_free (words);
		camel_search_words_free (tmp);
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	g_test_add_func ("/Camel/SearchSplit/split", test_search_splitting);
	g_test_add_func ("/Camel/SearchSplit/simple", test_search_splitting_simple);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}

/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <libedataserver/libedataserver.h>
#include <locale.h>

typedef struct {
  ECollator *collator;

} CollatorFixture;

static void
print_locale (CollatorFixture *fixture,
              const gchar *locale)
{
  const gchar *const *labels;
  gint n_labels, i;

  if (g_getenv ("TEST_DEBUG") == NULL)
	  return;

  /* This does not effect the test results, only ensures
   * that if a locale is installed on the given system, the
   * locale specific characters get printed properly on the console.
   */
  setlocale (LC_ALL, locale);

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, NULL, NULL, NULL);

  g_print ("Printing alphabet Index: ");

  for (i = 0; i < n_labels; i++)
    {
      if (i > 0)
	g_print (", ");

      g_print ("%d: '%s'", i, labels[i]);
    }

  g_print ("\n");
}

static void
collator_test_setup (CollatorFixture *fixture,
                     gconstpointer data)
{
  const gchar *locale = (const gchar *) data;
  GError *error = NULL;

  fixture->collator = e_collator_new (locale, &error);

  if (!fixture->collator)
    g_error ("Failed to create collator for locale '%s': %s",
	     locale, error->message);

  print_locale (fixture, locale);
}

static void
collator_test_teardown (CollatorFixture *fixture,
                        gconstpointer data)
{
  e_collator_unref (fixture->collator);
}

static void
test_en_US (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;

  e_collator_get_index_labels (fixture->collator,
			       &n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 28);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 27);
  g_assert_cmpint (inflow, ==, -1);

  /* M is the 13th letter, the 0 index is the underflow bucket */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Monster"), ==, 13);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "mini"), ==, 13);

  /* E is the 5th letter, the 0 index is the underflow bucket... test variants of 'E' */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "elegant"), ==, 5);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "ELEPHANT"), ==, 5);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "émily"), ==, 5);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Énergie"), ==, 5);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "ègene"), ==, 5);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Èlementery"), ==, 5);
}

static void
test_el_GR (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;

  e_collator_get_index_labels (fixture->collator,
			       &n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 52);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 51);
  g_assert_cmpint (inflow, ==, -1);

  /* E is the 5th letter, the 0 index is the underflow bucket... Greek sorts 'ε' as an 'E' */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "εβδομάδα"), ==, 5);

  /* Δ is the 4th letter, the 0 index is the underflow bucket... */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Δευτέρα"), ==, 4);

  /* In greek 'D' does not sort under the 'Δ' bucket, instead it's in the 'D' bucket */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Damsel"), ==, 28);

  /* 'Τ' is the 19th letter */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Τρίτη"), ==, 19);

  /* Texas doesn't start with 'T' ! --> 'T' bucket */
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Texas"), ==, 44);
}

static void
test_ru_RU (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;
  const gchar *const *labels;

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 58);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 57);
  g_assert_cmpint (inflow, ==, -1);

  g_assert_cmpstr (labels[5], ==, "Д");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "друг"), ==, 5);

  g_assert_cmpstr (labels[4], ==, "Г");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "говорить"), ==, 4);

  g_assert_cmpstr (labels[24], ==, "Ч");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "человек"), ==, 24);
}

static void
test_ja_JP (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;
  const gchar *const *labels;

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 39);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 38);
  g_assert_cmpint (inflow, ==, 27);

  g_assert_cmpstr (labels[33], ==, "は");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "はじめまして"), ==, 33);

  g_assert_cmpstr (labels[30], ==, "さ");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "それはいいですね。"), ==, 30);

  g_assert_cmpstr (labels[35], ==, "や");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "ゆっくりしゃべってくれますか"), ==, 35);
}

static void
test_zh_CN (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;
  const gchar *const *labels;

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 28);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 27);
  g_assert_cmpint (inflow, ==, -1);

  /* Chinese and Latin words end up in the 'D' bucket */
  g_assert_cmpstr (labels[4], ==, "D");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "东"), ==, 4);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "david"), ==, 4);

  g_assert_cmpstr (labels[10], ==, "J");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "今天"), ==, 10);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Jeffry"), ==, 10);

  g_assert_cmpstr (labels[26], ==, "Z");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "早上"), ==, 26);
  g_assert_cmpint (e_collator_get_index (fixture->collator, "Zack"), ==, 26);
}

static void
test_ko_KR (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;
  const gchar *const *labels;

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 43);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 42);
  g_assert_cmpint (inflow, ==, 15);

  g_assert_cmpstr (labels[1], ==, "ㄱ");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "고새기"), ==, 1);

  g_assert_cmpstr (labels[8], ==, "ㅇ");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "안성아"), ==, 8);

  g_assert_cmpstr (labels[13], ==, "ㅍ");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "피자 모근 시간"), ==, 13);
}

static void
test_ar_TN (CollatorFixture *fixture,
            gconstpointer data)
{
  gint n_labels, underflow, inflow, overflow;
  const gchar *const *labels;

  labels = e_collator_get_index_labels (fixture->collator,
					&n_labels, &underflow, &inflow, &overflow);

  g_assert_cmpint (n_labels, ==, 56);
  g_assert_cmpint (underflow, ==, 0);
  g_assert_cmpint (overflow, ==, 55);
  g_assert_cmpint (inflow, ==, -1);

  g_assert_cmpstr (labels[12], ==, "س");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "سلام"), ==, 12);

  g_assert_cmpstr (labels[18], ==, "ع");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "عيد ميلاد سعيد"), ==, 18);

  g_assert_cmpstr (labels[23], ==, "ل");
  g_assert_cmpint (e_collator_get_index (fixture->collator, "لاأدري"), ==, 23);
}

static void
test_cs_CZ (CollatorFixture *fixture,
            gconstpointer data)
{
	gint n_labels, underflow, inflow, overflow;
	const gchar *const *labels;

	labels = e_collator_get_index_labels (fixture->collator, &n_labels, &underflow, &inflow, &overflow);

	g_assert_cmpint (n_labels, ==, 33);
	g_assert_cmpint (underflow, ==, 0);
	g_assert_cmpint (overflow, ==, 32);
	g_assert_cmpint (inflow, ==, -1);

	g_assert_cmpstr (labels[4], ==, "Č");
	g_assert_cmpint (e_collator_get_index (fixture->collator, "Čenda"), ==, 4);

	g_assert_cmpstr (labels[7], ==, "F");
	g_assert_cmpint (e_collator_get_index (fixture->collator, "Franta"), ==, 7);

	g_assert_cmpstr (labels[10], ==, "CH");
	g_assert_cmpint (e_collator_get_index (fixture->collator, "Chantal"), ==, 10);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECollator/en_US", CollatorFixture, "en_US.UTF-8",
		collator_test_setup, test_en_US, collator_test_teardown);

	g_test_add (
		"/ECollator/el_GR", CollatorFixture, "el_GR.UTF-8",
		collator_test_setup, test_el_GR, collator_test_teardown);

	g_test_add (
		"/ECollator/ru_RU", CollatorFixture, "ru_RU.UTF-8",
		collator_test_setup, test_ru_RU, collator_test_teardown);

	g_test_add (
		"/ECollator/ja_JP", CollatorFixture, "ja_JP.UTF-8",
		collator_test_setup, test_ja_JP, collator_test_teardown);

	g_test_add (
		"/ECollator/zh_CN", CollatorFixture, "zh_CN.UTF-8",
		collator_test_setup, test_zh_CN, collator_test_teardown);

	g_test_add (
		"/ECollator/ko_KR", CollatorFixture, "ko_KR.UTF-8",
		collator_test_setup, test_ko_KR, collator_test_teardown);

	g_test_add (
		"/ECollator/ar_TN", CollatorFixture, "ar_TN.UTF-8",
		collator_test_setup, test_ar_TN, collator_test_teardown);

	g_test_add (
		"/ECollator/cs_CZ", CollatorFixture, "cs_CZ.UTF-8",
		collator_test_setup, test_cs_CZ, collator_test_teardown);

	return g_test_run ();
}

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

#include "e-test-server-utils.h"

#include <libedataserver/libedataserver.h>

static ETestServerClosure test_closure = { E_TEST_SERVER_NONE, NULL, 0, FALSE, NULL };

static void
test_webdav_href_compare (ETestServerFixture *fixture,
			  gconstpointer user_data)
{
	struct _hrefs {
		const gchar *href1;
		const gchar *href2;
		gboolean same;
	} hrefs[] = {
	/* 0 */	{ "http://www.gnome.org/", "http://www.gnome.org/", TRUE },
		{ "https://www.gnome.org/", "http://www.gnome.org/", TRUE },
		{ "http://user@www.gnome.org/", "https://www.gnome.org/", TRUE },
		{ "http://www.gnome.org/index", "http://www.gnome.org/", FALSE },
		{ "http://www.GNOME.org/index", "http://www.gnome.org/index", TRUE },
		{ "http://www.GNOME.org/Index", "http://www.gnome.org/index", FALSE },
		{ "http://www.gnome.org/index/", "http://www.gnome.org/index", FALSE },
		{ "http://www.gnome.org/path/collection/data.ext", "http://www.gnome.org/path/collection/data.ext", TRUE },
		{ "https://www.gnome.org/path/collection/data.ext", "http://www.gnome.org/path/collection/data.ext", TRUE },
		{ "http://user@www.gnome.org/path/collection/data.ext", "http://www.gnome.org/path/collection/data.ext", TRUE },
	/* 10 */{ "http://www.gnome.org/Path/collection/data.ext", "http://www.gnome.org/path/collection/data.ext", FALSE },
		{ "http://www.gnome.org/path/Collection/data.ext", "http://www.gnome.org/path/collection/data.ext", FALSE },
		{ "http://www.gnome.org/path/collection/Data.ext", "http://www.gnome.org/path/collection/data.ext", FALSE },
		{ "http://www.GNOME.org/path/collection/data.ext", "http://www.gnome.org/path/collection/data.ext", TRUE },
		{ "http://www.gnome.org/path/collection/data.ext", "http://www.server.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org", "https://www.gnome.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org/", "https://www.gnome.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org/path", "https://www.gnome.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org/path/", "https://www.gnome.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org/path/collection", "https://www.gnome.org/path/collection/data.ext", FALSE },
	/* 20 */{ "https://www.gnome.org/path/collection/", "https://www.gnome.org/path/collection/data.ext", FALSE },
		{ "https://www.gnome.org/path/user@no.where/", "http://www.gnome.org/path/user@no.where/", TRUE },
		{ "https://www.gnome.org/path/user%40no.where/", "http://www.gnome.org/path/user@no.where/", TRUE },
		{ "https://www.gnome.org/path/user%40no.where/", "http://www.gnome.org/path/user@no.where", FALSE },
		{ "https://www.gnome.org/user%40no.where/", "http://www.gnome.org/path/user@no.where", FALSE },
		{ "https://www.gnome.org/user%40no.where", "http://www.gnome.org/user%40no%2Ewhere", TRUE },
		{ "https://www.gnome.org/user%40no.where", "http://www.gnome.org/user%40no%2ewhere", TRUE },
		{ "https://www.gnome.org/path/user%40no.where/path", "http://www.gnome.org/path/user%40no%2Ewhere/path", TRUE },
		{ "https://user@www.gnome.org/path/user%40no.where/path", "http://www.gnome.org/path/user%40no%2Ewhere/path", TRUE },
		{ "https://user@www.gnome.org/path/user%40no.where/path", "http://www.gnome.org/path/user%40no%2Ewhere/path", TRUE },
	/* 30 */{ "https://user@www.gnome.org/path/user@no.where/path", "http://www.gnome.org/path/user%40no%2Ewhere/path", TRUE },
		{ "https://user@www.gnome.org/path/user@no.where/path", "http://no@www.gnome.org/path/user@no%2Ewhere/path", TRUE },
		{ "https://www.gnome.org/path%", "https://www.gnome.org/path%", TRUE },
		{ "https://www.gnome.org/path%g", "https://www.gnome.org/path%g", TRUE },
		{ "https://www.gnome.org/path%ah", "https://www.gnome.org/path%ah", TRUE },
		{ "https://www.gnome.org/path%32", "https://www.gnome.org/path%32", TRUE },
		{ "https://www.gnome.org/path%20%2e", "https://www.gnome.org/path .", TRUE },
		{ "https://Xww.gnome.org", "https://www.gnome.org", FALSE },
		{ "http://www.gnome.org/a%2e%2e%2e/b", "https://www.gnome.org/a.../b", TRUE },
		{ "http://www.gnome.org/a%2e%2e%2eb", "https://www.gnome.org/a...b", TRUE },
	/* 40 */{ "http://www.gnome.org/a%2e%2e%2e//", "https://www.gnome.org/a.../", FALSE },
		{ "https://www.gnome.ORG", "https://www.gnome.org", TRUE },
		{ "https://www.gnome.orG", "https://www.gnome.org", TRUE },
		{ "https://www.gnome.org/2", "https://www.gnome.org/234", FALSE }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (hrefs); ii++) {
		if (hrefs[ii].same) {
			g_assert_cmpint (ii, ==, e_webdav_session_util_item_href_equal (hrefs[ii].href1, hrefs[ii].href2) ? ii : -1);
			g_assert_cmpint (ii, ==, e_webdav_session_util_item_href_equal (hrefs[ii].href2, hrefs[ii].href1) ? ii : -1);
		} else {
			g_assert_cmpint (ii, !=, e_webdav_session_util_item_href_equal (hrefs[ii].href1, hrefs[ii].href2) ? ii : -1);
			g_assert_cmpint (ii, !=, e_webdav_session_util_item_href_equal (hrefs[ii].href2, hrefs[ii].href1) ? ii : -1);
		}
	}
}

static void
test_parse_date (ETestServerFixture *fixture,
		 gconstpointer user_data)
{
	struct _tests {
		const gchar *value;
		gint expected_year;
		gshort expected_month;
		gshort expected_day;
		gboolean two_digit_year;
	} tests[] = {
		{ "12/30/1980", 1980, 12, 30, FALSE },
		{ "01/02/2020", 2020, 01, 02, FALSE },
		{ "12/29/80",   1980, 12, 29, TRUE },
		{ "12/28/02",   2002, 12, 28, TRUE },
		{ "12/27/133",   133, 12, 27, FALSE },
		{ "12/26/99",   1999, 12, 26, TRUE },
		{ "11/25/00",   2000, 11, 25, TRUE }
	};
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (tests); ii++) {
		struct tm tm;
		gboolean two_digit_year = FALSE;

		g_assert_cmpint (e_time_parse_date_ex (tests[ii].value, &tm, &two_digit_year), ==, E_TIME_PARSE_OK);
		g_assert_cmpint (tm.tm_year + 1900, ==, tests[ii].expected_year);
		g_assert_cmpint (tm.tm_mon + 1, ==, tests[ii].expected_month);
		g_assert_cmpint (tm.tm_mday, ==, tests[ii].expected_day);
		g_assert_cmpint ((two_digit_year ? 1 : 0), ==, (tests[ii].two_digit_year ? 1 : 0));
	}
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/issues");

	g_test_add (
		"/libedataserver-test/WebDAVhrefCompare",
		ETestServerFixture, &test_closure,
		e_test_server_utils_setup,
		test_webdav_href_compare,
		e_test_server_utils_teardown);
	g_test_add (
		"/libedataserver-test/ParseDate",
		ETestServerFixture, &test_closure,
		e_test_server_utils_setup,
		test_parse_date,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}

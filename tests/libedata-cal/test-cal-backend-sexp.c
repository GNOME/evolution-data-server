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

#include <libedata-cal/libedata-cal.h>

static void
test_query (const gchar *query)
{
	ECalBackendSExp *sexp = e_cal_backend_sexp_new (query);
	time_t start, end;
	gboolean generator;

	sexp = e_cal_backend_sexp_new (query);
	g_assert_nonnull (sexp);

	generator = e_cal_backend_sexp_evaluate_occur_times (sexp, &start, &end);

	if (generator) {
		printf ("%s: %" G_GINT64_FORMAT "- %" G_GINT64_FORMAT "\n", query, (gint64) start, (gint64) end);
	} else {
		printf ("%s: no time prunning possible\n", query);
	}

	g_object_unref (sexp);
}

gint
main (gint argc,
      gchar **argv)
{
	/* e_sexp_add_variable(f, 0, "test", NULL); */

	if (argc <= 4 || !argv[5]) {
		test_query ("(occur-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\"))");
		test_query ("(due-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\"))");
		test_query ("(has-alarms-in-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\"))");
		test_query ("(completed-before? (make-time \"20080727T220000Z\") )");

		test_query ("(and (occur-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\")) #t)");
		test_query ("(or (occur-in-time-range? (make-time \"20080727T220000Z\")(make-time \"20080907T220000Z\")) #t)");

		test_query ("(and (contains? \"substring\") (has-categories? \"blah\"))");
		test_query ("(or (occur-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\")) (contains? \"substring\"))");

		test_query ("(and (and (occur-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\"))"
			" (or (contains? \"substring\") (has-categories? \"blah\"))) (has-alarms?))");

		test_query ("(or (and (occur-in-time-range? (make-time \"20080727T220000Z\") (make-time \"20080907T220000Z\"))"
			" (or (contains? \"substring\") (has-categories? \"blah\"))) (has-alarms?))");
	} else {
		test_query (argv[5]);
	}

	return 0;
}

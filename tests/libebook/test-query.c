#include <string.h>
#include <libebook/libebook.h>

#define QUERY_STRING1
#define QUERY_STRING2

static void
test_sexp (gconstpointer data)
{
	EBookQuery *query = e_book_query_from_string (data);
	char *sexp = e_book_query_to_string (query);

	g_assert_cmpstr (data, ==, sexp);

	g_free (sexp);
	e_book_query_unref (query);
}

gint
main (gint argc,
      gchar **argv)
{
	g_type_init ();

	g_test_init (&argc, &argv, NULL);

	g_test_add_data_func ("/libebook/test-query/sexp/exists",
	                      "(exists \"full_name\")",
	                      test_sexp);
	g_test_add_data_func ("/libebook/test-query/sexp/contains",
	                      "(contains \"full_name\" \"Miguel\")",
	                      test_sexp);

	/* XXX come on, add more here */

	return g_test_run ();
}

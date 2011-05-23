#include <string.h>
#include <libebook/e-book-query.h>

#define QUERY_STRING1
#define QUERY_STRING2

static const gchar * queries[] = {
	"(exists \"full_name\")",
	"(contains \"full_name\" \"Miguel\")"

	/* XXX come on, add more here */
};

gint
main (gint argc, gchar **argv)
{
	gint i;
	gboolean failure = FALSE;

	for (i = 0; i < G_N_ELEMENTS (queries); i++) {
		EBookQuery *query = e_book_query_from_string (queries[i]);
		gchar *str;

		str = e_book_query_to_string (query);

		if (strcmp (str, queries[i])) {
			g_warning ("failed on query: %s", queries[i]);

			failure = TRUE;
		}
	}

	if (!failure)
	  g_message ("all tests passed");

	return 0;
}

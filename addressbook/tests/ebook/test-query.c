
#include <libgnome/gnome-init.h>
#include <string.h>
#include <libebook/e-book.h>

#define QUERY_STRING1 
#define QUERY_STRING2 

static char* queries[] = {
	"(exists \"full_name\")",
	"(contains \"full_name\" \"Miguel\")"

	/* XXX come on, add more here */
};

int
main (int argc, char **argv)
{
	int i;
	gboolean failure = FALSE;

	gnome_program_init("test-query", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	for (i = 0; i < G_N_ELEMENTS (queries); i ++) {
		EBookQuery *query = e_book_query_from_string (queries[i]);
		char *str;
		
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

#include <locale.h>
#include <string.h>

#include <libebook/libebook.h>

#define QUERY_STRING1
#define QUERY_STRING2

typedef struct {
	EBookQuery *query;
	gchar *locale;
	gchar *sexp;
} TestData;

static void
normalize_space (gchar *str)
{
	while (*str) {
		gchar *tail;

		if (*str++ != ' ')
			continue;

		tail = str;

		while (*tail == ' ')
			++tail;

		memmove (str, tail, strlen (tail) + 1);
	}
}

static void
test_query (gconstpointer data)
{
	const TestData *const test = data;
	EBookQuery *query;
	gchar *sexp;

	setlocale (LC_ADDRESS, test->locale);
	sexp = e_book_query_to_string (test->query);
	normalize_space (sexp);

	g_assert_cmpstr (test->sexp, ==, sexp);
	g_free (sexp);

	query = e_book_query_from_string (test->sexp);
	g_assert (query != NULL);

	sexp = e_book_query_to_string (query);
	e_book_query_unref (query);
	normalize_space (sexp);

	g_assert_cmpstr (test->sexp, ==, sexp);
	g_free (sexp);
}

static void
test_data_free (gpointer data)
{
	TestData *const test = data;

	e_book_query_unref (test->query);
	g_free (test->sexp);
	g_free (test->locale);

	g_slice_free (TestData, test);
}

static void
add_query_test (const gchar *path,
                EBookQuery *query,
                const gchar *sexp)
{
	TestData *data = g_slice_new (TestData);

	data->locale = g_strdup (setlocale (LC_ADDRESS, NULL));
	data->sexp = g_strdup (sexp);
	data->query = query;

	g_test_add_data_func_full (path, data, test_query, test_data_free);
}

gint
main (gint argc,
      gchar **argv)
{
	g_type_init ();

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	setlocale (LC_ADDRESS, "en_US.UTF-8");

	add_query_test (
		"/libebook/test-query/sexp/all",
		e_book_query_any_field_contains (NULL),
		"(contains \"x-evolution-any-field\" \"\")");

	add_query_test (
		"/libebook/test-query/sexp/any",
		e_book_query_any_field_contains ("liberty"),
		"(contains \"x-evolution-any-field\" \"liberty\")");

	add_query_test (
		"/libebook/test-query/sexp/not",
		e_book_query_not (
			e_book_query_any_field_contains ("liberty"), TRUE),
		"(not (contains \"x-evolution-any-field\" \"liberty\"))");

	add_query_test (
		"/libebook/test-query/sexp/and",
		e_book_query_andv (
			e_book_query_any_field_contains ("liberty"),
			e_book_query_any_field_contains ("friendship"),
			NULL),
		"(and (contains \"x-evolution-any-field\" \"liberty\")"
		" (contains \"x-evolution-any-field\" \"friendship\")"
		" )");

	add_query_test (
		"/libebook/test-query/sexp/or",
		e_book_query_orv (
			e_book_query_any_field_contains ("liberty"),
			e_book_query_any_field_contains ("friendship"),
			NULL),
		"(or (contains \"x-evolution-any-field\" \"liberty\")"
		" (contains \"x-evolution-any-field\" \"friendship\")"
		" )");

	add_query_test (
		"/libebook/test-query/sexp/exists",
		e_book_query_field_exists (
			E_CONTACT_FULL_NAME),
			"(exists \"full_name\")");

	add_query_test (
		"/libebook/test-query/sexp/contains",
		e_book_query_field_test (
			E_CONTACT_FULL_NAME,
			E_BOOK_QUERY_CONTAINS,
			"Miguel"),
		"(contains \"full_name\" \"Miguel\")");

	add_query_test (
		"/libebook/test-query/sexp/is",
		e_book_query_field_test (
			E_CONTACT_GIVEN_NAME,
			E_BOOK_QUERY_IS,
			"Miguel"),
		"(is \"given_name\" \"Miguel\")");

	add_query_test (
		"/libebook/test-query/sexp/beginswith",
		e_book_query_field_test (
			E_CONTACT_FULL_NAME,
			E_BOOK_QUERY_BEGINS_WITH,
			"Mig"),
		"(beginswith \"full_name\" \"Mig\")");

	add_query_test (
		"/libebook/test-query/sexp/endswith",
		e_book_query_field_test (
			E_CONTACT_TEL,
			E_BOOK_QUERY_ENDS_WITH,
			"5423789"),
		"(endswith \"phone\" \"5423789\")");

	add_query_test (
		"/libebook/test-query/sexp/eqphone/us",
		e_book_query_orv (
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"+1-2215423789"),
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"2215423789"),
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER,
				"5423789"),
				NULL),
		"(or (eqphone \"phone\" \"+1-2215423789\" \"en_US.UTF-8\")"
		" (eqphone_national \"phone\" \"2215423789\" \"en_US.UTF-8\")"
		" (eqphone_short \"phone\" \"5423789\" \"en_US.UTF-8\")"
		" )");

	setlocale (LC_ADDRESS, "en_GB.UTF-8");

	add_query_test (
		"/libebook/test-query/sexp/eqphone/gb",
		e_book_query_orv (
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_PHONE_NUMBER,
				"+1-2215423789"),
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_NATIONAL_PHONE_NUMBER,
				"2215423789"),
			e_book_query_field_test (
				E_CONTACT_TEL,
				E_BOOK_QUERY_EQUALS_SHORT_PHONE_NUMBER,
				"5423789"),
			NULL),
		"(or (eqphone \"phone\" \"+1-2215423789\" \"en_GB.UTF-8\")"
		" (eqphone_national \"phone\" \"2215423789\" \"en_GB.UTF-8\")"
		" (eqphone_short \"phone\" \"5423789\" \"en_GB.UTF-8\")"
		" )");

	return g_test_run ();
}

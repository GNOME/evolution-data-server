#include <libebook/e-book.h>

gint main (gint argc, gchar **argv)
{
	GError *err = NULL;
	EBook *book = NULL;

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create local addressbook\n");
		exit(0);
	}

	if (!e_book_open (book, FALSE, NULL)) {
		printf ("failed to open local addressbook\n");
		exit(0);
	}

	printf ("removing nonexistant contact\n");
	if (!e_book_remove_contact (book, "kk", &err)) {
		printf ("error %d removing contact: %s\n", err->code, err->message);
		g_clear_error (&err);
	}

	return 0;
}

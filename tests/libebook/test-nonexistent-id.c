#include <stdlib.h>
#include <libebook/libebook.h>

gint
main (gint argc,
      gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	GError *err = NULL;
	EBook *book = NULL;

	printf ("loading addressbook\n");
	book = e_book_new_system_addressbook (NULL);
	if (!book) {
		printf ("failed to create local addressbook\n");
		exit (0);
	}

	if (!e_book_open (book, FALSE, NULL)) {
		printf ("failed to open local addressbook\n");
		exit (0);
	}

	printf ("removing nonexistant contact\n");
	if (!e_book_remove_contact (book, "kk", &err)) {
		printf ("error %d removing contact: %s\n", err->code, err->message);
		g_clear_error (&err);
	}
#endif /* ACCOUNT_MGMT */

	return 0;
}

#include <libgnome/gnome-init.h>
#include <libebook/e-book.h>

int main (int argc, char **argv) 
{
	GError *err = NULL;
	EBook *book = NULL;

	gnome_program_init("test-nonexistent-id", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	book = e_book_new ();

	printf ("loading addressbook\n");
	if (!e_book_load_local_addressbook (book, NULL)) {
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

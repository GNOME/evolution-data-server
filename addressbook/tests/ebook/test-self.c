/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libebook/e-book.h>

int
main (int argc, char **argv)
{
	EBook *book;
	EContact *contact;
	GError *error = NULL;
	char *vcard;

	gnome_program_init("test-ebook", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	printf ("getting the self contact\n");

	if (!e_book_get_self (&contact, &book, &error)) {
		printf ("error %d getting self: %s\n", error->code, error->message);
		g_clear_error (&error);
		return -1;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	printf ("self contact = \n%s\n", vcard);
	g_free (vcard);

	g_object_unref (contact);
	g_object_unref (book);

	bonobo_main_quit();

	return 0;
}

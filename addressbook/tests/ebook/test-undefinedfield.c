
#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <libebook/e-book.h>

int
main (int argc, char **argv)
{
	EContact *contact;
	gpointer test;

	gnome_program_init("test-undefinedfield", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	contact = e_contact_new ();

	printf ("testing e_contact_get of something suitably out of range\n");
	test = e_contact_get (contact, 6000 /* something suitably high. */);
	if (test)
	  printf ("failed, e_contact_get return non-NULL\n");
	else
	  printf ("passed\n");

	printf ("testing e_contact_get of something we don't handle yet\n");
	test = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);
	if (test)
	  printf ("failed, e_contact_get return non-NULL\n");
	else
	  printf ("passed\n");

	exit (0);
}

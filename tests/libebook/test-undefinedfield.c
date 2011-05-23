#include <stdlib.h>
#include <libebook/e-contact.h>

gint
main (gint argc, gchar **argv)
{
	EContact *contact;
	gpointer test;

	g_type_init ();

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

#include <stdlib.h>
#include <string.h>
#include <libebook/e-contact.h>

#define TEST_ID "test-uid"

gint
main (gint argc, gchar **argv)
{
	EContact *contact;

	g_type_init ();

	contact = e_contact_new ();

	e_contact_set (contact, E_CONTACT_UID, TEST_ID);

	if (!strcmp (e_contact_get_const (contact, E_CONTACT_UID), TEST_ID))
	  printf ("passed\n");
	else
	  printf ("failed\n");

	return 0;
}

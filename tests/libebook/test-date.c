#include <stdlib.h>
#include <libebook/e-contact.h>

gint
main (gint argc, gchar **argv)
{
	EContact *contact;
	EContactDate date, *dp;

	g_type_init ();

	contact = e_contact_new ();

	date.year = 1999;
	date.month = 3;
	date.day = 3;

	e_contact_set (contact, E_CONTACT_BIRTH_DATE, &date);

	printf ("vcard = \n%s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));

	dp = e_contact_get (contact, E_CONTACT_BIRTH_DATE);

	if (dp->year != date.year
	    || dp->month != date.month
	    || dp->day != date.day)
	  printf ("failed\n");
	else
	  printf ("passed\n");

	return 0;
}

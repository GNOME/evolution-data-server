
#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <string.h>
#include <libebook/e-book.h>

int
main (int argc, char **argv)
{
	EContact *contact;
	GList *category_list;
	char *categories;

	gnome_program_init("test-categories", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	printf ("--- Testing setting CATEGORY_LIST\n");
	contact = e_contact_new ();

	category_list = NULL;
	category_list = g_list_append (category_list, "Birthday");
	category_list = g_list_append (category_list, "Business");
	category_list = g_list_append (category_list, "Competition");

	e_contact_set (contact, E_CONTACT_CATEGORY_LIST, category_list);

	category_list = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);

	printf ("vcard = \n%s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));

	categories = e_contact_get (contact, E_CONTACT_CATEGORIES);

	if (strcmp (categories, "Birthday,Business,Competition"))
	  printf ("failed conversion from list to string\n");
	else
	  printf ("passed conversion from list to string\n");

	g_object_unref (contact);

	printf ("--- Testing setting CATEGORIES\n");
	contact = e_contact_new ();

	e_contact_set (contact, E_CONTACT_CATEGORIES, "Birthday,Business,Competition");

	printf ("vcard = \n%s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));

	category_list = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);
	if (g_list_length (category_list) != 3)
	  printf ("failed conversion from string to list\n");
	else {
	  if (!strcmp ("Birthday", (char*)g_list_nth_data (category_list, 0)) &&
	      !strcmp ("Business", (char*)g_list_nth_data (category_list, 1)) &&
	      !strcmp ("Competition", (char*)g_list_nth_data (category_list, 2)))
	    printf ("passed conversion from string to list\n");
	  else
	    printf ("failed conversion from string to list\n");
	}
	return 0;
}


#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <stdlib.h>
#include <string.h>
#include <libebook/e-book.h>

int
main (int argc, char **argv)
{
	EContact *contact;
	GList *category_list, *l;
	char *categories;

	gnome_program_init("test-categories", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	contact = e_contact_new ();

	category_list = NULL;
	category_list = g_list_append (category_list, "Birthday");
	category_list = g_list_append (category_list, "Business");
	category_list = g_list_append (category_list, "Competition");

	e_contact_set (contact, E_CONTACT_CATEGORY_LIST, category_list);

	category_list = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);
	for (l = category_list; l; l = l->next) {
	  printf ("category: %s\n", (char*)l->data);
	}

	printf ("vcard = \n%s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));

	categories = e_contact_get (contact, E_CONTACT_CATEGORIES);

	if (strcmp (categories, "Birthday,Business,Competition"))
	  printf ("failed\n");
	else
	  printf ("passed\n");
	
	return 0;
}

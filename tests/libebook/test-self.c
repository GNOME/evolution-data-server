/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

gint
main (gint argc,
      gchar **argv)
{
#if 0  /* ACCOUNT_MGMT */
	EBook *book;
	EContact *contact;
	GError *error = NULL;
	gchar *vcard;

	g_type_init ();

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
#endif /* ACCOUNT_MGMT */

	return 0;
}

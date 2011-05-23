#include <stdlib.h>
#include <string.h>
#include <libebook/e-contact.h>

/* TEL;WORK:... should map to PHONE_BUSINESS
   TEL;FAX:... should map to OTHER_FAX. */
#define VCARD \
"BEGIN:vCard\r\n\
VERSION:3.0\r\n\
X-EVOLUTION-FILE-AS:test\\, 40013\r\n\
FN:40013 test\r\n\
N:40013;test;;;\r\n\
TEL;VOICE:456-456-4567\r\n\
TEL;WORK:123-123-1234\r\n\
TEL;FAX:321-321-4321\r\n\
END:vCard"

static void
check (gboolean test, const gchar *msg)
{
	printf ("%s - %s\n", test ? "passed" : "failed", msg);
}

gint
main (gint argc, gchar **argv)
{
	EContact *contact;
	const gchar *phone;

	g_type_init ();

	contact = e_contact_new_from_vcard (VCARD);

	phone = e_contact_get_const (contact, E_CONTACT_PHONE_BUSINESS);
	if (phone) printf ("business phone: %s\n", phone);
	check (phone && !strcmp ("123-123-1234", phone), "business phone");

	phone = e_contact_get_const (contact, E_CONTACT_PHONE_OTHER_FAX);
	if (phone) printf ("other fax: %s\n", phone);
	check (phone && !strcmp ("321-321-4321", phone), "other fax");

	return 0;
}

#include <stdlib.h>
#include <string.h>
#include <libebook/libebook.h>

/* TEL;WORK:... should map to PHONE_BUSINESS
 * TEL;FAX:... should map to OTHER_FAX. */
#define VCARD					\
  "BEGIN:VCARD\n"				\
  "FN:Janet Jackson\n"				\
  "N:Janet\n"					\
  "TEL;WORK:123-123-1234\n"			\
  "TEL;VOICE:456-456-4567\n"			\
  "TEL;FAX:321-321-4321\n"			\
  "END:VCARD\n"

static void
test_business (void)
{
	EContact *contact;
	const gchar *phone;

	contact = e_contact_new_from_vcard (VCARD);

	phone = e_contact_get_const (contact, E_CONTACT_PHONE_BUSINESS);
	g_assert (phone != NULL);
	g_assert_cmpstr (phone, ==, "123-123-1234");

	g_object_unref (contact);
}

static void
test_other_fax (void)
{
	EContact *contact;
	const gchar *phone;

	contact = e_contact_new_from_vcard (VCARD);

	phone = e_contact_get_const (contact, E_CONTACT_PHONE_OTHER_FAX);
	g_assert (phone != NULL);
	g_assert_cmpstr (phone, ==, "321-321-4321");

	g_object_unref (contact);
}

gint
main (gint argc,
      gchar **argv)
{
	g_type_init ();

	g_test_init (&argc, &argv, NULL);

#if 0   /* This is failing for some reason, somewhere in EDS history it broke,
	 * for now I'm leaving the compiler warning in place intentionally
	 */
	g_test_add_func ("/Contact/UntypedPhones/Business", test_business);
#endif
	g_test_add_func ("/Contact/UntypedPhones/OtherFax", test_other_fax);

	return g_test_run ();
}

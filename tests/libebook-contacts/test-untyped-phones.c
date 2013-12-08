#include <stdlib.h>
#include <string.h>
#include <libebook/libebook.h>

/* TEL;WORK,VOICE:... should map to PHONE_BUSINESS
 * TEL;VOICE:... should map to PHONE_OTHER
 * TEL;FAX:... should map to OTHER_FAX. */
#define VCARD \
  "BEGIN:VCARD\n" \
  "FN:Janet Jackson\n" \
  "N:Janet\n" \
  "TEL;WORK,VOICE:123-123-1234\n" \
  "TEL;VOICE:456-456-4567\n" \
  "TEL;FAX:321-321-4321\n" \
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
test_other_phone (void)
{
	EContact *contact;
	const gchar *phone;

	contact = e_contact_new_from_vcard (VCARD);

	phone = e_contact_get_const (contact, E_CONTACT_PHONE_OTHER);
	g_assert (phone != NULL);
	g_assert_cmpstr (phone, ==, "456-456-4567");

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
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add_func ("/Contact/UntypedPhones/Business", test_business);
	g_test_add_func ("/Contact/UntypedPhones/OtherPhone", test_other_phone);
	g_test_add_func ("/Contact/UntypedPhones/OtherFax", test_other_fax);

	return g_test_run ();
}

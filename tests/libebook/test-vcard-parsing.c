#include <libebook/libebook.h>

static gboolean
compare_single_value (EVCard *vcard,
                      const gchar *attrname,
                      const gchar *expected_value)
{
	EVCardAttribute *attr;
	gchar *str;

	g_return_val_if_fail (vcard != NULL, FALSE);
	g_return_val_if_fail (attrname != NULL, FALSE);
	g_return_val_if_fail (expected_value != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, attrname);
	g_return_val_if_fail (attr != NULL, FALSE);

	str = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (str, expected_value) == 0, FALSE);

	g_free (str);

	return TRUE;
}

static gboolean
has_only_one (EVCard *vcard,
              const gchar *attrname)
{
	gboolean found = FALSE;
	GList *iter;

	for (iter = e_vcard_get_attributes (vcard); iter; iter = iter->next) {
		EVCardAttribute *attr = iter->data;

		if (g_strcmp0 (e_vcard_attribute_get_name (attr), attrname) == 0) {
			if (found)
				return FALSE;
			found = TRUE;
		}
	}

	return found;
}

static gboolean
test_vcard (const gchar *vcard_str)
{
	EVCard *vc1, *vc2;
	gchar *str;

	/* Do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "UID");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);
	g_object_unref (vc1);

	/* do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	/* Setting the UID does not cause vCard parsing */
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	/* Retrieving the UID should not cause vCard parsing either */
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	/* Getting FN attribute WILL cause parsing */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	g_object_unref (vc1);

	/* parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == FALSE, FALSE);
	e_vcard_remove_attributes (vc1, NULL, "UID");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	vc2 = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (vc2, "UID", "other-uid"), FALSE);

	g_object_unref (vc2);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_return_val_if_fail (e_vcard_is_parsed (vc1) == TRUE, FALSE);
	g_return_val_if_fail (compare_single_value (vc1, "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (vc1, EVC_FORMAT_VCARD_30);
	vc2 = e_vcard_new_from_string (str);
	g_return_val_if_fail (e_vcard_is_parsed (vc2) == FALSE, FALSE);
	g_free (str);

	g_return_val_if_fail (compare_single_value (vc2, "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (vc1, "UID"), FALSE);
	g_return_val_if_fail (has_only_one (vc2, "UID"), FALSE);

	g_object_unref (vc2);
	g_object_unref (vc1);

	return TRUE;
}

static gboolean
test_econtact (const gchar *vcard_str)
{
	EContact *c1, *c2;
	gchar *str;

	/* do not parse */
	c1 = e_contact_new_from_vcard (vcard_str);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_UID);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);
	g_object_unref (c1);

	/* not parsed again */
	c1 = e_contact_new_from_vcard (vcard_str);
	e_contact_set (c1, E_CONTACT_UID, "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c2)) == FALSE, FALSE);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c1), "UID"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c2), "UID"), FALSE);

	g_object_unref (c2);
	g_object_unref (c1);

	/* do not parse */
	c1 = e_contact_new_from_vcard_with_uid (vcard_str, "other-uid");
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_to_string (E_VCARD (c1), EVC_FORMAT_VCARD_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c1), "UID"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c2), "UID"), FALSE);

	g_object_unref (c2);
	g_object_unref (c1);

	return TRUE;
}

gint
main (gint argc,
      gchar **argv)
{
	const gchar
		*test_vcard_no_uid_str = 
			"BEGIN:VCARD\r\n"
			"VERSION:3.0\r\n"
			"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
			"FN:zyx mix\r\n"
			"N:zyx;mix;;;\r\n"
			"END:VCARD",

		*test_vcard_with_uid_str = 
			"BEGIN:VCARD\r\n"
			"VERSION:3.0\r\n"
			"UID:some-uid\r\n"
			"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
			"FN:zyx mix\r\n"
			"N:zyx;mix;;;\r\n"
			"END:VCARD";

	g_type_init ();

	g_print ("Testing vCard without UID...\n");
	g_return_val_if_fail (test_vcard (test_vcard_no_uid_str), 1);
	g_print ("Passed.\n");

	g_print ("Testing vCard with UID set...\n");
	g_return_val_if_fail (test_vcard (test_vcard_with_uid_str), 1);
	g_print ("Passed.\n");

	g_print ("Testing EContact without UID...\n");
	g_return_val_if_fail (test_econtact (test_vcard_no_uid_str), 1);
	g_print ("Passed.\n");

	g_print ("Testing EContact with UID set...\n");
	g_return_val_if_fail (test_econtact (test_vcard_with_uid_str), 1);
	g_print ("Passed.\n");

	g_print ("Bye.\n");

	return 0;
}

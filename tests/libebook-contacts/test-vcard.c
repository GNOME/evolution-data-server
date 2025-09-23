/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <libebook/libebook.h>
#include <string.h>

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

static void
test_vcard (const gchar *vcard_str)
{
	EVCard *vc1, *vc2;
	gchar *str;

	/* Do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	str = e_vcard_convert_to_string (vc1, E_VCARD_VERSION_30);
	g_assert_nonnull (str);
	g_assert_cmpstr (str, ==, vcard_str);
	g_assert_false (e_vcard_is_parsed (vc1));

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "UID");
	g_assert_true (e_vcard_is_parsed (vc1));
	str = e_vcard_convert_to_string (vc1, E_VCARD_VERSION_30);
	g_assert_nonnull (str);
	g_assert_cmpstr (str, ==, vcard_str);

	g_free (str);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_assert_true (e_vcard_is_parsed (vc1));
	str = e_vcard_convert_to_string (vc1, E_VCARD_VERSION_30);
	g_assert_nonnull (str);
	g_assert_cmpstr (str, ==, vcard_str);

	g_free (str);
	g_object_unref (vc1);

	/* do not parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	/* Setting the UID does not cause vCard parsing */
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_assert_false (e_vcard_is_parsed (vc1));
	/* Retrieving the UID should not cause vCard parsing either */
	g_assert_true (compare_single_value (vc1, "UID", "other-uid"));
	g_assert_false (e_vcard_is_parsed (vc1));
	/* Getting FN attribute WILL cause parsing */
	e_vcard_get_attribute (vc1, "FN");
	g_assert_true (e_vcard_is_parsed (vc1));
	g_object_unref (vc1);

	/* parse */
	vc1 = e_vcard_new_from_string (vcard_str);
	g_assert_false (e_vcard_is_parsed (vc1));
	e_vcard_remove_attributes (vc1, NULL, "UID");
	g_assert_true (e_vcard_is_parsed (vc1));
	e_vcard_append_attribute_with_value (vc1, e_vcard_attribute_new (NULL, "UID"), "other-uid");
	g_assert_true (compare_single_value (vc1, "UID", "other-uid"));
	str = e_vcard_convert_to_string (vc1, E_VCARD_VERSION_30);
	vc2 = e_vcard_new_from_string (str);
	g_free (str);

	g_assert_true (compare_single_value (vc2, "UID", "other-uid"));

	g_object_unref (vc2);

	/* parse */
	e_vcard_get_attribute (vc1, "FN");
	g_assert_true (e_vcard_is_parsed (vc1));
	g_assert_true (compare_single_value (vc1, "UID", "other-uid"));
	str = e_vcard_convert_to_string (vc1, E_VCARD_VERSION_30);
	vc2 = e_vcard_new_from_string (str);
	g_assert_false (e_vcard_is_parsed (vc2));
	g_free (str);

	g_assert_true (compare_single_value (vc2, "UID", "other-uid"));
	g_assert_true (has_only_one (vc1, "UID"));
	g_assert_true (has_only_one (vc2, "UID"));

	g_object_unref (vc2);
	g_object_unref (vc1);
}

static gboolean
test_econtact (const gchar *vcard_str)
{
	EContact *c1, *c2;
	gchar *str;

	/* do not parse */
	c1 = e_contact_new_from_vcard (vcard_str);
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == FALSE, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_UID);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (g_ascii_strcasecmp (str, vcard_str) == 0, FALSE);

	g_free (str);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
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
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
	c2 = e_contact_new_from_vcard (str);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c2)) == FALSE, FALSE);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
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
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);

	g_object_unref (c2);

	/* parse */
	e_contact_get_const (c1, E_CONTACT_FULL_NAME);
	g_return_val_if_fail (compare_single_value (E_VCARD (c1), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (e_vcard_is_parsed (E_VCARD (c1)) == TRUE, FALSE);
	str = e_vcard_convert_to_string (E_VCARD (c1), E_VCARD_VERSION_30);
	c2 = e_contact_new_from_vcard (str);
	g_free (str);

	g_return_val_if_fail (compare_single_value (E_VCARD (c2), "UID", "other-uid"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c1), "UID"), FALSE);
	g_return_val_if_fail (has_only_one (E_VCARD (c2), "UID"), FALSE);

	g_object_unref (c2);
	g_object_unref (c1);

	return TRUE;
}

static gboolean
test_vcard_qp_2_1_parsing (const gchar *vcard_str,
                           const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	gchar *value;

	vcard = e_vcard_new_from_string (vcard_str);
	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (value != NULL, FALSE);

	g_return_val_if_fail (g_strcmp0 (value, expected_text) == 0, FALSE);

	g_object_unref (vcard);
	g_free (value);

	return TRUE;
}

static gboolean
test_vcard_qp_2_1_saving (const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	EVCardAttributeParam *param;
	gchar *str, *encoded_value;
	GString *decoded;

	vcard = e_vcard_new ();
	attr = e_vcard_attribute_new (NULL, "FN");
	param = e_vcard_attribute_param_new ("ENCODING");
	e_vcard_attribute_param_add_value (param, "quoted-printable");
	e_vcard_attribute_add_param (attr, param);

	e_vcard_attribute_add_value_decoded (attr, expected_text, strlen (expected_text));

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	encoded_value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (encoded_value != NULL, FALSE);
	/* it's the encoded value, thus it cannot match */
	g_return_val_if_fail (g_strcmp0 (encoded_value, expected_text) != 0, FALSE);
	g_free (encoded_value);

	e_vcard_add_attribute (vcard, attr);

	str = e_vcard_convert_to_string (vcard, E_VCARD_VERSION_21);
	g_object_unref (vcard);

	g_return_val_if_fail (str != NULL, FALSE);

	vcard = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	g_object_unref (vcard);

	return TRUE;
}

static gboolean
test_vcard_qp_3_0_saving (const gchar *expected_text)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	EVCardAttributeParam *param;
	gchar *str, *value, *encoded_value;
	GString *decoded;

	vcard = e_vcard_new ();
	attr = e_vcard_attribute_new (NULL, "FN");
	param = e_vcard_attribute_param_new ("ENCODING");
	e_vcard_attribute_param_add_value (param, "quoted-printable");
	e_vcard_attribute_add_param (attr, param);

	e_vcard_attribute_add_value_decoded (attr, expected_text, strlen (expected_text));
	e_vcard_add_attribute (vcard, attr);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	encoded_value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (encoded_value != NULL, FALSE);
	/* it's the encoded value, thus it cannot match */
	g_return_val_if_fail (g_strcmp0 (encoded_value, expected_text) != 0, FALSE);

	str = e_vcard_convert_to_string (vcard, E_VCARD_VERSION_30);

	g_object_unref (vcard);

	g_return_val_if_fail (str != NULL, FALSE);

	vcard = e_vcard_new_from_string (str);
	g_free (str);

	g_return_val_if_fail (vcard != NULL, FALSE);

	attr = e_vcard_get_attribute (vcard, "FN");
	g_return_val_if_fail (attr != NULL, FALSE);

	decoded = e_vcard_attribute_get_value_decoded (attr);
	g_return_val_if_fail (decoded != NULL, FALSE);
	g_return_val_if_fail (g_strcmp0 (decoded->str, expected_text) == 0, FALSE);
	g_string_free (decoded, TRUE);

	value = e_vcard_attribute_get_value (attr);
	g_return_val_if_fail (value != NULL, FALSE);
	/* either base64 or a free form, but not quoted-printable for sure */
	g_return_val_if_fail (g_strcmp0 (value, encoded_value) != 0, FALSE);
	g_free (value);

	g_object_unref (vcard);
	g_free (encoded_value);

	return TRUE;
}

static void
test_vcard_quoted_printable (void)
{
	const gchar *expected_text = "ActualValue ěščřžýáíéúůóöĚŠČŘŽÝÁÍÉÚŮÓÖ§ "
				     "1234567890 1234567890 1234567890 1234567890 1234567890";
	const gchar *vcard_2_1_str =
		"BEGIN:VCARD\r\n"
		"VERSION:2.1\r\n"
		"FN;ENCODING=quoted-printable:ActualValue=20=C4=9B=C5=A1"
		  "=C4=8D=C5=99=C5=BE=C3=BD=C3=A1=C3=AD=C3=A9=C3=BA=C5=AF=C3"
		  "=B3=C3=B6=C4=9A=C5=A0=C4=8C=C5=98=C5=BD=C3=9D=C3=81=C3=8D"
		  "=C3=89=C3=9A=C5=AE=C3=93=C3=96=C2=A7=201234567890=2012345"
		  "67890=201234567890=201234567890=201234567890\r\n"
		"END:VCARD\r\n";

	g_assert_true (test_vcard_qp_2_1_parsing (vcard_2_1_str, expected_text));
	g_assert_true (test_vcard_qp_2_1_saving (expected_text));
	g_assert_true (test_vcard_qp_3_0_saving (expected_text));
}

static void
test_vcard_charset (void)
{
	const gchar *vcard_str =
		"BEGIN:VCARD\r\n"
		"VERSION:2.1\r\n"
		"N;LANGUAGE=ru;CHARSET=windows-1251:\xCF\xF3\xEF\xEA\xE8\xED;\xC2\xE0\xF1\xE8\xEB\xE8\xE9\r\n"
		"FN;CHARSET=windows-1251:\xC2\xE0\xF1\xE8\xEB\xE8\xE9 \xCF\xF3\xEF\xEA\xE8\xED\r\n"
		"TEL;WORK;VOICE:12-34-56\r\n"
		"ADR;WORK;PREF;CHARSET=windows-1251:;;\xCB\xE5\xED\xE8\xED\xE0 \xE4\x2E 1\r\n"
		"LABEL;WORK;PREF;CHARSET=windows-1251:\xCB\xE5\xED\xE8\xED\xE0 \xE4\x2E 1\r\n"
		"EMAIL;PREF;INTERNET:test@test.ru\r\n"
		"REV:20230906T132316Z\r\n"
		"END:VCARD\r\n";
	const gchar *expected_vcard_str =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"N;LANGUAGE=ru:Пупкин;Василий\r\n"
		"FN:Василий Пупкин\r\n"
		"TEL;TYPE=WORK,VOICE:12-34-56\r\n"
		"ADR;TYPE=WORK,PREF:;;Ленина д. 1\r\n"
		"LABEL;TYPE=WORK,PREF:Ленина д. 1\r\n"
		"EMAIL;TYPE=PREF,INTERNET:test@test.ru\r\n"
		"REV:20230906T132316Z\r\n"
		"END:VCARD";
	const gchar *expected_N_1 = "Пупкин";
	const gchar *expected_N_2 = "Василий";
	const gchar *expected_FN = "Василий Пупкин";
	const gchar *expected_ADR = "Ленина д. 1";
	const gchar *expected_LABEL = "Ленина д. 1";
	EVCard *vcard;
	EVCardAttribute *attr;
	GList *values;
	GString *value;
	gchar *tmp;

	vcard = e_vcard_new_from_string (vcard_str);
	g_assert_nonnull (vcard);

	attr = e_vcard_get_attribute (vcard, EVC_N);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values_decoded (attr);
	g_assert_cmpint (g_list_length (values), ==, 2);
	g_assert_cmpstr (((GString *) values->data)->str, ==, expected_N_1);
	g_assert_cmpstr (((GString *) values->next->data)->str, ==, expected_N_2);

	attr = e_vcard_get_attribute (vcard, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values_decoded (attr);
	g_assert_cmpint (g_list_length (values), ==, 1);
	g_assert_cmpstr (((GString *) values->data)->str, ==, expected_FN);

	attr = e_vcard_get_attribute (vcard, EVC_ADR);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values_decoded (attr);
	g_assert_cmpint (g_list_length (values), ==, 3);
	g_assert_cmpstr (((GString *) values->next->next->data)->str, ==, expected_ADR);

	attr = e_vcard_get_attribute (vcard, EVC_LABEL);
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_cmpstr (value->str, ==, expected_LABEL);
	g_string_free (value, TRUE);

	tmp = e_vcard_convert_to_string (vcard, E_VCARD_VERSION_30);
	g_assert_nonnull (tmp);
	g_assert_true (g_utf8_validate (tmp, -1, NULL));
	g_assert_cmpstr (tmp, ==, expected_vcard_str);
	g_free (tmp);

	g_clear_object (&vcard);
}


#define verify_attr_simple(_name, _expected) { \
	EVCardAttribute *_attr; \
	GString *_value; \
	_attr = e_vcard_get_attribute (vcard, _name); \
	g_assert_nonnull (_attr); \
	_value = e_vcard_attribute_get_value_decoded (_attr); \
	g_assert_nonnull (_value); \
	g_assert_cmpstr (_value->str, ==, _expected); \
	g_string_free (_value, TRUE); \
	}

static void
test_vcard_charset_mixed (void)
{
	const gchar *vcard_str =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"X-WIN1251;LANGUAGE=ru;CHARSET=windows-1251:\xCF\xF3\xEF\xEA\xE8\xED,\xC2\xE0\xF1\xE8\xEB\xE8\xE9\r\n"
		"X-CP1250;CHARSET=cp1250:\xEC\x9A\xE8\xF8\x9E\xFD\xE1\xED\xE9\xFA\xF9\xA7\r\n"
		"X-UTF8;CHARSET=UTF-8:§ĚŠČŘŽÝÁÍÉÚŮ\r\n"
		"X-ASCII;CHARSET=us-ascii:qwertyuiop\r\n"
		"X-NONE:qwerty§ĚŠČŘŽÝÁÍÉÚŮuiop\r\n"
		"END:VCARD\r\n";
	EVCard *vcard;

	vcard = e_vcard_new_from_string (vcard_str);
	g_assert_nonnull (vcard);

	verify_attr_simple ("X-WIN1251", "Пупкин,Василий");
	verify_attr_simple ("X-CP1250", "ěščřžýáíéúů§");
	verify_attr_simple ("X-UTF8", "§ĚŠČŘŽÝÁÍÉÚŮ");
	verify_attr_simple ("X-ASCII", "qwertyuiop");
	verify_attr_simple ("X-NONE", "qwerty§ĚŠČŘŽÝÁÍÉÚŮuiop");

	g_clear_object (&vcard);
}

static void
test_vcard_charset_broken (void)
{
	const gchar *vcard_str =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n" /* ascii with no group */
		"group.X-ATTR1:good group and attr\r\n"
		"g\xFF" "roup.X-ATTR2:broken group\r\n"
		"group.X-AT\xFFTR3:broken attr\r\n"
		"g\xFF" "roup.X-AT\xFF" "TR4:broken group and attr\r\n"
		"X-ATTR5:brok\xFF" "en value\r\n"
		"X-ATTR6;CHARSET=cp1250:broken value wi\xFF" "th charset \xEC\x9A\xE8\xF8\x9E\xFD\xE1\xED\xE9\xFA\xF9\xA7\r\n"
		"X-ATTR7;X-PARAM=žšř:utf-8 param value\r\n"
		"X-ATTR8;X-PARAMžšř=zsr:utf-8 param name\r\n"
		"X-ATTR9;X-PARAMžšř=žšř:utf-8 param name and value\r\n"
		"X-ATTR10;X-PA\xFF" "RAM=zsr:broken param name\r\n"
		"X-ATTR11;X-PARAM=zs\xFF" "r:broken param value\r\n"
		"X-ATTR12;X-PA\xFF" "RAM=zs\xFF" "r:broken param name and value\r\n"
		"X-ATTR13;X-PARAM1=1p;X-PA\xFF" "RAM2=2p;X-PARAM3=3\xFF" "p;X-PA\xFF" "RAM4=4\xFF" "p:multiple params\r\n"
		"END:VCARD\r\n";
	EVCard *vcard;
	EVCardAttribute *attr;
	GList *param;
	GString *value;

	vcard = e_vcard_new_from_string (vcard_str);
	g_assert_nonnull (vcard);

	/* attributes with broken name or parameter are dropped */
	g_assert_cmpint (g_list_length (e_vcard_get_attributes (vcard)), ==, 11);

	verify_attr_simple ("VERSION", "3.0");

	attr = e_vcard_get_attribute (vcard, "X-ATTR1");
	g_assert_nonnull (attr);
	g_assert_cmpstr (e_vcard_attribute_get_group (attr), ==, "group");
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "good group and attr");
	g_string_free (value, TRUE);

	attr = e_vcard_get_attribute (vcard, "X-ATTR5");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "brok�en value");
	g_string_free (value, TRUE);

	attr = e_vcard_get_attribute (vcard, "X-ATTR6");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "broken value wi˙th charset ěščřžýáíéúů§");
	g_string_free (value, TRUE);

	attr = e_vcard_get_attribute (vcard, "X-ATTR7");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "utf-8 param value");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 1);
	param = e_vcard_attribute_get_param (attr, "X-PARAM");
	g_assert_cmpint (g_list_length (param), ==, 1);
	g_assert_cmpstr (param->data, ==, "žšř");

	attr = e_vcard_get_attribute (vcard, "X-ATTR8");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "utf-8 param name");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	attr = e_vcard_get_attribute (vcard, "X-ATTR9");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "utf-8 param name and value");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	attr = e_vcard_get_attribute (vcard, "X-ATTR10");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "broken param name");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	attr = e_vcard_get_attribute (vcard, "X-ATTR11");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "broken param value");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 1);
	param = e_vcard_attribute_get_param (attr, "X-PARAM");
	g_assert_cmpint (g_list_length (param), ==, 1);
	g_assert_cmpstr (param->data, ==, "zs�r");

	attr = e_vcard_get_attribute (vcard, "X-ATTR12");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "broken param name and value");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	attr = e_vcard_get_attribute (vcard, "X-ATTR13");
	g_assert_nonnull (attr);
	value = e_vcard_attribute_get_value_decoded (attr);
	g_assert_nonnull (value);
	g_assert_true (g_utf8_validate (value->str, value->len, NULL));
	g_assert_cmpstr (value->str, ==, "multiple params");
	g_string_free (value, TRUE);
	g_assert_cmpint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 2);
	param = e_vcard_attribute_get_param (attr, "X-PARAM1");
	g_assert_cmpint (g_list_length (param), ==, 1);
	g_assert_cmpstr (param->data, ==, "1p");
	param = e_vcard_attribute_get_param (attr, "X-PARAM3");
	g_assert_cmpint (g_list_length (param), ==, 1);
	g_assert_cmpstr (param->data, ==, "3�p");

	g_clear_object (&vcard);
}

static const gchar *test_vcard_no_uid_str =
	"BEGIN:VCARD\r\n"
	"VERSION:3.0\r\n"
	"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
	"FN:zyx mix\r\n"
	"N:zyx;mix;;;\r\n"
	"END:VCARD";

static const gchar *test_vcard_with_uid_str =
	"BEGIN:VCARD\r\n"
	"VERSION:3.0\r\n"
	"UID:some-uid\r\n"
	"EMAIL;TYPE=OTHER:zyx@no.where\r\n"
	"FN:zyx mix\r\n"
	"N:zyx;mix;;;\r\n"
	"END:VCARD";

static void
test_vcard_with_uid (void)
{
	test_vcard (test_vcard_with_uid_str);
}

static void
test_vcard_without_uid (void)
{
	test_vcard (test_vcard_no_uid_str);
}

static void
test_contact_with_uid (void)
{
	g_assert_true (test_econtact (test_vcard_with_uid_str));
}

static void
test_contact_without_uid (void)
{
	g_assert_true (test_econtact (test_vcard_no_uid_str));
}

static void
test_phone_params_and_value (EContact *contact,
			     EContactField field_id,
			     const gchar *expected_value,
			     const gchar *expected_value_type)
{
	GList *attributes, *params, *link;
	EVCardAttribute *attr = NULL;

	g_assert_true (E_IS_CONTACT (contact));
	g_assert_true (expected_value != NULL);
	g_assert_true (expected_value_type != NULL);

	g_assert_nonnull (e_contact_get_const (contact, field_id));
	g_assert_cmpstr (e_contact_get_const (contact, field_id), ==, expected_value);

	attributes = e_vcard_get_attributes_by_name (E_VCARD (contact), e_contact_vcard_attribute (field_id));

	g_assert_cmpuint (g_list_length (attributes), ==, 3);

	for (link = attributes; link; link = g_list_next (link)) {
		gchar *value;

		attr = link->data;

		g_assert_true (attr != NULL);

		value = e_vcard_attribute_get_value (attr);

		g_assert_true (value != NULL);

		if (g_strcmp0 (value, expected_value) == 0) {
			g_free (value);
			break;
		}

		g_free (value);
		attr = NULL;
	}

	g_assert_true (attr != NULL);

	g_assert_true (e_vcard_attribute_get_name (attr) != NULL);

	params = e_vcard_attribute_get_params (attr);
	g_assert_cmpuint (g_list_length (params), ==, 2);

	for (link = params; link; link = g_list_next (link)) {
		EVCardAttributeParam *param = link->data;
		const gchar *name;

		g_assert_true (param != NULL);

		name = e_vcard_attribute_param_get_name (param);

		g_assert_true (name != NULL);
		g_assert_true (g_ascii_strcasecmp (name, EVC_TYPE) == 0 ||
			  g_ascii_strcasecmp (name, EVC_X_E164) == 0);

		if (g_ascii_strcasecmp (name, EVC_X_E164) == 0) {
			GList *values;
			const gchar *value;

			values = e_vcard_attribute_param_get_values (param);

			g_assert_true (values != NULL);
			g_assert_true (values->next == NULL || values->next->next == NULL);

			value = values->data;

			g_assert_true (value != NULL);
			g_assert_cmpstr (value, ==, expected_value);

			if (values->next) {
				value = values->next->data;

				if (value != NULL)
					g_assert_cmpstr (value, ==, "");
			}
		} else {
			GList *values;
			const gchar *value1, *value2;

			values = e_vcard_attribute_param_get_values (param);

			g_assert_true (values != NULL);
			g_assert_true (values->next != NULL);
			g_assert_true (values->next->next == NULL);

			value1 = values->data;
			value2 = values->next->data;

			g_assert_true (value1 != NULL);
			g_assert_true (value2 != NULL);
			g_assert_cmpstr (value1, ==, expected_value_type);
			g_assert_cmpstr (value2, ==, "VOICE");
		}
	}

	g_list_free (attributes);
}

static void
test_contact_empty_value (void)
{
	EContact *contact;

	contact = e_contact_new_from_vcard (
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"UID:some-uid\r\n"
		"REV:2017-01-12T11:34:36Z(0)\r\n"
		"FN:zyx\r\n"
		"N:zyx;;;;\r\n"
		"EMAIL;TYPE=WORK:work@no.where\r\n"
		"TEL;X-EVOLUTION-E164=00123456789,;TYPE=WORK,VOICE:00123456789\r\n"
		"TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-E164=11123456789,:11123456789\r\n"
		"TEL;X-EVOLUTION-E164=002233445566;TYPE=HOME,VOICE:002233445566\r\n"
		"END:VCARD\r\n");

	g_assert_true (E_IS_CONTACT (contact));

	test_phone_params_and_value (contact, E_CONTACT_PHONE_BUSINESS, "00123456789", "WORK");
	test_phone_params_and_value (contact, E_CONTACT_PHONE_BUSINESS_2, "11123456789", "WORK");
	test_phone_params_and_value (contact, E_CONTACT_PHONE_HOME, "002233445566", "HOME");

	g_clear_object (&contact);
}

static void
test_parsing_date_time (void)
{
	struct _values {
		const gchar *str;
		const gchar *str_expected;
		gushort year;
		gushort month;
		gushort day;
		gushort hour;
		gushort minute;
		gushort second;
		gint utc_offset;
		EContactDateTimeFlags flags;
		gboolean has_date;
		gboolean has_time;
	} values[] = {
	#define NONE_DATE 0
	#define NONE_TIME G_MAXUSHORT
	#define NONE_OFFSET G_MAXINT
	#define date(_str, _yy, _mm, _dd) { _str, _str, _yy, _mm, _dd, NONE_TIME, NONE_TIME, NONE_TIME, NONE_OFFSET, E_CONTACT_DATE_TIME_FLAG_NONE, TRUE, FALSE }
	#define time_bare(_str, _hh, _mm, _ss, _utc_offset) { _str, _str, NONE_DATE, NONE_DATE, NONE_DATE, _hh, _mm, _ss, _utc_offset, E_CONTACT_DATE_TIME_FLAG_NONE, FALSE, TRUE }
	#define time_explicit(_str, _hh, _mm, _ss, _utc_offset) { _str, _str, NONE_DATE, NONE_DATE, NONE_DATE, _hh, _mm, _ss, _utc_offset, E_CONTACT_DATE_TIME_FLAG_TIME, FALSE, TRUE }
	#define time_in_date_time(_str, _hh, _mm, _ss, _utc_offset) { _str, _str, NONE_DATE, NONE_DATE, NONE_DATE, _hh, _mm, _ss, _utc_offset, E_CONTACT_DATE_TIME_FLAG_DATE_TIME, FALSE, TRUE }
	#define date_time(_str, _yy, _MM, _dd, _hh, _mm, _ss, _utc_offset) { _str, _str, _yy, _MM, _dd, _hh, _mm, _ss, _utc_offset, E_CONTACT_DATE_TIME_FLAG_DATE_TIME, TRUE, TRUE }
		time_bare ("10", 10, NONE_TIME, NONE_TIME, NONE_OFFSET),
		time_bare ("--00", NONE_TIME, NONE_TIME, 0, NONE_OFFSET),
		time_bare ("-2200", NONE_TIME, 22, 0, NONE_OFFSET),
		time_bare ("102200", 10, 22, 0, NONE_OFFSET),
		time_bare ("102200Z", 10, 22, 0, 0),
		time_bare ("102200+0830", 10, 22, 0, 830),
		time_bare ("102200-11", 10, 22, 0, -1100),
		time_explicit ("1022", 10, 22, NONE_TIME, NONE_OFFSET),
		time_in_date_time ("T10", 10, NONE_TIME, NONE_TIME, NONE_OFFSET),
		time_in_date_time ("T--00", NONE_TIME, NONE_TIME, 0, NONE_OFFSET),
		time_in_date_time ("T1022", 10, 22, NONE_TIME, NONE_OFFSET),
		time_in_date_time ("T-2200", NONE_TIME, 22, 0, NONE_OFFSET),
		time_in_date_time ("T102200", 10, 22, 0, NONE_OFFSET),
		time_in_date_time ("T102200Z", 10, 22, 0, 0),
		time_in_date_time ("T102200-0830", 10, 22, 0, -830),
		time_in_date_time ("T102200+11", 10, 22, 0, 1100),
		date ("1985", 1985, NONE_DATE, NONE_DATE),
		date ("19850412", 1985, 04, 12),
		date ("---12", NONE_DATE, NONE_DATE, 12),
		date ("--0412", NONE_DATE, 4, 12),
		date ("1985-04", 1985, 4, NONE_DATE),
		date_time ("---22T14", NONE_DATE, NONE_DATE, 22, 14, NONE_TIME, NONE_TIME, NONE_OFFSET),
		date_time ("---22T1410", NONE_DATE, NONE_DATE, 22, 14, 10, NONE_TIME, NONE_OFFSET),
		date_time ("---22T141020", NONE_DATE, NONE_DATE, 22, 14, 10, 20, NONE_OFFSET),
		date_time ("---22T141020Z", NONE_DATE, NONE_DATE, 22, 14, 10, 20, 0),
		date_time ("---22T141020-08", NONE_DATE, NONE_DATE, 22, 14, 10, 20, -800),
		date_time ("---22T141020+11", NONE_DATE, NONE_DATE, 22, 14, 10, 20, +1100),
		date_time ("---22T141020-1030", NONE_DATE, NONE_DATE, 22, 14, 10, 20, -1030),
		date_time ("---22T141020+1120", NONE_DATE, NONE_DATE, 22, 14, 10, 20, +1120),
		date_time ("--0604T14", NONE_DATE, 6, 4, 14, NONE_TIME, NONE_TIME, NONE_OFFSET),
		date_time ("--0604T1410", NONE_DATE, 6, 4, 14, 10, NONE_TIME, NONE_OFFSET),
		date_time ("--0604T141020", NONE_DATE, 6, 4, 14, 10, 20, NONE_OFFSET),
		date_time ("--0604T141020Z", NONE_DATE, 6, 4, 14, 10, 20, 0),
		date_time ("--0604T141020-08", NONE_DATE, 6, 4, 14, 10, 20, -800),
		date_time ("--0604T141020+11", NONE_DATE, 6, 4, 14, 10, 20, +1100),
		date_time ("--0604T141020-1030", NONE_DATE, 6, 4, 14, 10, 20, -1030),
		date_time ("--0604T141020+1120", NONE_DATE, 6, 4, 14, 10, 20, +1120),
		date_time ("--1025T1122", NONE_DATE, 10, 25, 11, 22, NONE_TIME, NONE_OFFSET),
		date_time ("1985T14", 1985, NONE_DATE, NONE_DATE, 14, NONE_TIME, NONE_TIME, NONE_OFFSET),
		date_time ("1985T1410", 1985, NONE_DATE, NONE_DATE, 14, 10, NONE_TIME, NONE_OFFSET),
		date_time ("1985T141020", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, NONE_OFFSET),
		date_time ("1985T141020Z", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, 0),
		date_time ("1985T141020-08", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, -800),
		date_time ("1985T141020+11", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, +1100),
		date_time ("1985T141020-1030", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, -1030),
		date_time ("1985T141020+1120", 1985, NONE_DATE, NONE_DATE, 14, 10, 20, +1120),
		date_time ("1985-04T14", 1985, 4, NONE_DATE, 14, NONE_TIME, NONE_TIME, NONE_OFFSET),
		date_time ("1985-04T1410", 1985, 4, NONE_DATE, 14, 10, NONE_TIME, NONE_OFFSET),
		date_time ("1985-04T141020", 1985, 4, NONE_DATE, 14, 10, 20, NONE_OFFSET),
		date_time ("1985-04T141020Z", 1985, 4, NONE_DATE, 14, 10, 20, 0),
		date_time ("1985-04T141020-08", 1985, 4, NONE_DATE, 14, 10, 20, -800),
		date_time ("1985-04T141020+11", 1985, 4, NONE_DATE, 14, 10, 20, +1100),
		date_time ("1985-04T141020-1030", 1985, 4, NONE_DATE, 14, 10, 20, -1030),
		date_time ("1985-04T141020+1120", 1985, 4, NONE_DATE, 14, 10, 20, +1120),
		date_time ("19850604T14", 1985, 6, 4, 14, NONE_TIME, NONE_TIME, NONE_OFFSET),
		date_time ("19850604T1410", 1985, 6, 4, 14, 10, NONE_TIME, NONE_OFFSET),
		date_time ("19850604T141020", 1985, 6, 4, 14, 10, 20, NONE_OFFSET),
		date_time ("19850604T141020Z", 1985, 6, 4, 14, 10, 20, 0),
		date_time ("19850604T141020-08", 1985, 6, 4, 14, 10, 20, -800),
		date_time ("19850604T141020+11", 1985, 6, 4, 14, 10, 20, +1100),
		date_time ("19850604T141020-1030", 1985, 6, 4, 14, 10, 20, -1030),
		date_time ("19850604T141020+1120", 1985, 6, 4, 14, 10, 20, +1120),
		date_time ("19961025T112233", 1996, 10, 25, 11, 22, 33, NONE_OFFSET),
		{ "1985-04-12", "19850412", 1985, 4, 12, NONE_TIME, NONE_TIME, NONE_TIME, NONE_OFFSET, E_CONTACT_DATE_TIME_FLAG_NONE, TRUE, FALSE }
	#undef date
	#undef time_explicit
	#undef time_bare
	#undef date_time
	#undef NONE_DATE
	#undef NONE_TIME
	#undef NONE_OFFSET
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (values); ii++) {
		EContactDateTime *dt, *dt2;
		gchar *str;

		dt = e_contact_date_time_from_string (values[ii].str, values[ii].flags);
		g_assert_nonnull (dt);
		g_assert_cmpuint (dt->year, ==, values[ii].year);
		g_assert_cmpuint (dt->month, ==, values[ii].month);
		g_assert_cmpuint (dt->day, ==, values[ii].day);
		g_assert_cmpuint (dt->hour, ==, values[ii].hour);
		g_assert_cmpuint (dt->minute, ==, values[ii].minute);
		g_assert_cmpuint (dt->second, ==, values[ii].second);
		g_assert_cmpuint (dt->utc_offset, ==, values[ii].utc_offset);
		g_assert_cmpuint (e_contact_date_time_has_date (dt) ? 1 : 0, ==, values[ii].has_date ? 1 : 0);
		g_assert_cmpuint (e_contact_date_time_has_time (dt) ? 1 : 0, ==, values[ii].has_time ? 1 : 0);

		str = e_contact_date_time_to_string (dt, E_VCARD_VERSION_40, values[ii].flags);
		g_assert_cmpstr (values[ii].str_expected, ==, str);
		dt2 = e_contact_date_time_from_string (str, values[ii].flags);
		g_assert_nonnull (dt2);
		g_assert_true (e_contact_date_time_equal (dt, dt2));

		e_contact_date_time_free (dt2);
		e_contact_date_time_free (dt);
		g_free (str);
	}
}

static void
test_construction_vcard_attribute_with_group (void)
{
	EVCardAttribute *attr1, *attr2, *attr3;

	attr1 = e_vcard_attribute_new (NULL, "X-TEST");
	attr2 = e_vcard_attribute_new ("", "X-TEST");
	attr3 = e_vcard_attribute_new ("GROUP", "X-TEST");

	g_assert_cmpstr (e_vcard_attribute_get_group (attr1), ==, NULL);
	g_assert_cmpstr (e_vcard_attribute_get_group (attr2), ==, NULL);
	g_assert_cmpstr (e_vcard_attribute_get_group (attr3), ==, "GROUP");

	e_vcard_attribute_free (attr3);
	e_vcard_attribute_free (attr2);
	e_vcard_attribute_free (attr1);
}

static void
test_convert (const gchar *vcard_data,
	      EVCardVersion from_version,
	      EVCardVersion to_version,
	      guint expected_attrs,
	      void (* test_attrs_func) (EVCard *converted, gpointer user_data),
	      gpointer user_data)
{
	EVCard *vcard, *converted;
	EVCardAttribute *attr;
	GList *values;
	guint step;

	vcard = e_vcard_new_from_string (vcard_data);
	g_assert_nonnull (vcard);
	g_assert_cmpint (e_vcard_get_version (vcard), ==, from_version);
	converted = e_vcard_convert (vcard, from_version);
	g_assert_null (converted);

	for (step = 0; step < 2; step++) {
		if (step == 0) {
			converted = e_vcard_convert (vcard, to_version);
			g_assert_nonnull (converted);
		} else {
			gchar *str;

			g_assert_nonnull (converted);

			str = e_vcard_convert_to_string (converted, to_version);
			g_assert_nonnull (str);
			g_object_unref (converted);
			converted = e_vcard_new_from_string (str);
			g_assert_nonnull (converted);

			g_free (str);
		}

		g_assert_cmpint (e_vcard_get_version (converted), ==, to_version);
		g_assert_cmpuint (g_list_length (e_vcard_get_attributes (converted)), ==, expected_attrs);

		attr = e_vcard_get_attribute (converted, EVC_VERSION);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		switch (to_version) {
		case E_VCARD_VERSION_21:
			g_assert_cmpstr (values->data, ==, "2.1");
			break;
		case E_VCARD_VERSION_30:
			g_assert_cmpstr (values->data, ==, "3.0");
			break;
		case E_VCARD_VERSION_40:
			g_assert_cmpstr (values->data, ==, "4.0");
			break;
		default:
			g_assert_not_reached ();
			break;
		}
		g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

		test_attrs_func (converted, user_data);
	}

	g_object_unref (converted);
	g_object_unref (vcard);
}

static void
convert_to_21_test_attrs (EVCard *converted,
			  gpointer user_data)
{
	const gchar *expected_fn = user_data;
	EVCardAttribute *attr;
	GList *values;

	attr = e_vcard_get_attribute (converted, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, expected_fn);
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 3);
	values = e_vcard_attribute_get_param (attr, "X-ParaM");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "x");
	values = e_vcard_attribute_get_param (attr, "x-parAM2");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "2");
	values = e_vcard_attribute_get_param (attr, EVC_VALUE);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "text");

	attr = e_vcard_get_attribute (converted, EVC_BDAY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "2025-11-27");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);
}

static void
test_convert_to_21 (void)
{
	const gchar *vcard_30 =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 3.0\r\n"
		"BDAY:2025-11-27\r\n"
		"SOURCE;VALUE=uri;x-param=y:https://no.where/vcard.vcf\r\n"
		"NICKNAME;VALUE=text;x-param=y:nickie\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_40 =
		"BEGIN:VCARD\r\n"
		"VERSION:4.0\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 4.0\r\n"
		"BDAY:20251127\r\n"
		"KIND;odd-param=Odd;X-param=b:individual\r\n"
		"NICKNAME;VALUE=text;x-param=y:nickie\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_30, E_VCARD_VERSION_30, E_VCARD_VERSION_21, 3,
		convert_to_21_test_attrs, (gpointer) "test vCard 3.0");

	test_convert (vcard_40, E_VCARD_VERSION_40, E_VCARD_VERSION_21, 3,
		convert_to_21_test_attrs, (gpointer) "test vCard 4.0");
}

static void
convert_to_30_test_attrs (EVCard *converted,
			  gpointer user_data)
{
	const gchar *expected_fn = user_data;
	EVCardAttribute *attr;
	GList *values;

	attr = e_vcard_get_attribute (converted, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, expected_fn);
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 3);
	values = e_vcard_attribute_get_param (attr, "X-ParaM");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "x");
	values = e_vcard_attribute_get_param (attr, "x-parAM2");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "2");
	values = e_vcard_attribute_get_param (attr, EVC_VALUE);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "text");

	attr = e_vcard_get_attribute (converted, EVC_BDAY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "2025-11-27");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	if (strstr (expected_fn, "4.0")) {
		attr = e_vcard_get_attribute (converted, EVC_X_EVOLUTION_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "individual");
		g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 1);
		values = e_vcard_attribute_get_param (attr, "X-PARAM");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "b");

		attr = e_vcard_get_attribute (converted, EVC_NICKNAME);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "nickie");
		g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 2);
		values = e_vcard_attribute_get_param (attr, "X-PARAM");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "y");
		values = e_vcard_attribute_get_param (attr, "value");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "text");
	}
}

static void
test_convert_to_30 (void)
{
	const gchar *vcard_21 =
		"BEGIN:VCARD\r\n"
		"VERSION:2.1\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 2.1\r\n"
		"BDAY:2025-11-27\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_40 =
		"BEGIN:VCARD\r\n"
		"VERSION:4.0\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 4.0\r\n"
		"BDAY:20251127\r\n"
		"KIND;odd-param=Odd;X-param=b:individual\r\n"
		"NICKNAME;VALUE=text;x-param=y:nickie\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_21, E_VCARD_VERSION_21, E_VCARD_VERSION_30, 3,
		convert_to_30_test_attrs, (gpointer) "test vCard 2.1");

	test_convert (vcard_40, E_VCARD_VERSION_40, E_VCARD_VERSION_30, 5,
		convert_to_30_test_attrs, (gpointer) "test vCard 4.0");
}

static void
convert_to_40_test_attrs (EVCard *converted,
			  gpointer user_data)
{
	const gchar *expected_fn = user_data;
	EVCardAttribute *attr;
	GList *values;

	attr = e_vcard_get_attribute (converted, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, expected_fn);
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 3);
	values = e_vcard_attribute_get_param (attr, "X-ParaM");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "x");
	values = e_vcard_attribute_get_param (attr, "x-parAM2");
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "2");
	values = e_vcard_attribute_get_param (attr, EVC_VALUE);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "text");

	attr = e_vcard_get_attribute (converted, EVC_BDAY);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "20251127");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	if (strstr (expected_fn, "3.0")) {
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "individual");
		g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 1);
		values = e_vcard_attribute_get_param (attr, "X-PARAM");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "b");

		attr = e_vcard_get_attribute (converted, EVC_NICKNAME);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "nickie");
		g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 2);
		values = e_vcard_attribute_get_param (attr, "X-PARAM");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "y");
		values = e_vcard_attribute_get_param (attr, "value");
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, "text");
	}
}

static void
test_convert_to_40 (void)
{
	const gchar *vcard_21 =
		"BEGIN:VCARD\r\n"
		"VERSION:2.1\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 2.1\r\n"
		"BDAY:2025-11-27\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_30 =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"FN;unknown-param=u;x-Param=x;X-param2=2;VALUE=text:test vCard 3.0\r\n"
		"BDAY:2025-11-27\r\n"
		"X-EVOLUTION-KIND;odd-param=Odd;X-param=b:individual\r\n"
		"NICKNAME;VALUE=text;x-param=y:nickie\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_21, E_VCARD_VERSION_21, E_VCARD_VERSION_40, 3,
		convert_to_40_test_attrs, (gpointer) "test vCard 2.1");

	test_convert (vcard_30, E_VCARD_VERSION_30, E_VCARD_VERSION_40, 5,
		convert_to_40_test_attrs, (gpointer) "test vCard 3.0");
}

static void
convert_quirks_adr_test_attrs (EVCard *converted,
			       gpointer user_data)
{
	EVCardVersion to_version = e_vcard_get_version (converted);
	const gchar *work_values[] = { "WorkP.O.Box", "line2\nline3", "WorkAddress", "WorkCity", "WorkState", "WorkZIp", "WorkCountry", NULL };
	const gchar *work_label = "JobCompany\nWorkAddress\nline2\nline3\nPO BOX WorkP.O.Box\nWorkCity,  WorkState WorkZIp\n\nWORKCOUNTRY";
	const gchar *home_values[] = { "HomeP.O.Box", "", "Home Address", "HomeCity", "HomeState", "HomeZIP", "HomeCountry", NULL };
	const gchar *home_label = "Home Address\nPO BOX HomeP.O.Box\nHomeCity,  HomeState HomeZIP\n\nHOMECOUNTRY";
	const gchar *other_values[] = { "OtherP.O.Box", "", "OtherAddress", "OtherCity", "OtherState", "OtherZip", "OtherCountry", NULL };
	const gchar *other_label = "OtherAddress\nOtherCity, OtherState\nOtherZip\nOtherP.O.Box\nOtherCountry";
	EVCardAttribute *attr;
	GList *values, *link;

	attr = e_vcard_get_attribute (converted, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "name");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	for (link = e_vcard_get_attributes (converted); link; link = g_list_next (link)) {
		attr = link->data;

		if (to_version == E_VCARD_VERSION_40) {
			GList *val_link;
			const gchar **adr_values = NULL;
			const gchar *adr_label = NULL;
			guint ii;

			if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_ADR) != 0)
				continue;

			if (e_vcard_attribute_has_type (attr, "woRK")) {
				adr_values = work_values;
				adr_label = work_label;
			} else if (e_vcard_attribute_has_type (attr, "HomE")) {
				adr_values = home_values;
				adr_label = home_label;
			} else if (e_vcard_attribute_has_type (attr, "OTher")) {
				adr_values = other_values;
				adr_label = other_label;
			} else {
				g_assert_not_reached ();
			}

			values = e_vcard_attribute_get_values (attr);
			g_assert_cmpuint (g_list_length (values), ==, g_strv_length ((gchar **) adr_values));

			for (ii = 0, val_link = values; adr_values[ii] && val_link; ii++, val_link = g_list_next (val_link)) {
				const gchar *val = val_link->data;

				g_assert_cmpstr (adr_values[ii], ==, val);
			}

			values = e_vcard_attribute_get_param (attr, EVC_LABEL);
			g_assert_cmpuint (g_list_length (values), ==, 1);
			g_assert_cmpstr (values->data, ==, adr_label);
		} else {
			if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_ADR) == 0) {
				GList *val_link;
				const gchar **adr_values = NULL;
				guint ii;

				if (e_vcard_attribute_has_type (attr, "woRK"))
					adr_values = work_values;
				else if (e_vcard_attribute_has_type (attr, "HomE"))
					adr_values = home_values;
				else if (e_vcard_attribute_has_type (attr, "OTher"))
					adr_values = other_values;
				else
					g_assert_not_reached ();

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, g_strv_length ((gchar **) adr_values));

				for (ii = 0, val_link = values; adr_values[ii] && val_link; ii++, val_link = g_list_next (val_link)) {
					const gchar *val = val_link->data;

					g_assert_cmpstr (adr_values[ii], ==, val);
				}
			} else if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_LABEL) == 0) {
				const gchar *adr_label = NULL;

				if (e_vcard_attribute_has_type (attr, "woRK"))
					adr_label = work_label;
				else if (e_vcard_attribute_has_type (attr, "HomE"))
					adr_label = home_label;
				else if (e_vcard_attribute_has_type (attr, "OTher"))
					adr_label = other_label;
				else
					g_assert_not_reached ();

				values = e_vcard_attribute_get_values (attr);
				g_assert_cmpuint (g_list_length (values), ==, 1);
				g_assert_cmpstr (values->data, ==, adr_label);
			}
		}
	}
}

static void
test_convert_quirks_adr (void)
{
	const gchar *vcard_30 =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"FN:name\r\n"
		"ADR;TYPE=WORK:WorkP.O.Box;line2\\nline3;WorkAddress;WorkCity;WorkState;WorkZ\r\n"
		" Ip;WorkCountry\r\n"
		"LABEL;TYPE=WORK:JobCompany\\nWorkAddress\\nline2\\nline3\\nPO BOX WorkP.O.Box\\n\r\n"
		" WorkCity\\,  WorkState WorkZIp\\n\\nWORKCOUNTRY\r\n"
		"ADR;TYPE=HOME:HomeP.O.Box;;Home Address;HomeCity;HomeState;HomeZIP;HomeCoun\r\n"
		" try\r\n"
		"LABEL;TYPE=HOME:Home Address\\nPO BOX HomeP.O.Box\\nHomeCity\\,  HomeState Hom\r\n"
		" eZIP\\n\\nHOMECOUNTRY\r\n"
		"ADR;TYPE=OTHER:OtherP.O.Box;;OtherAddress;OtherCity;OtherState;OtherZip;Oth\r\n"
		" erCountry\r\n"
		"LABEL;TYPE=OTHER:OtherAddress\\nOtherCity\\, OtherState\\nOtherZip\\nOtherP.O.B\r\n"
		" ox\\nOtherCountry\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_40 =
		"BEGIN:VCARD\r\n"
		"VERSION:4.0\r\n"
		"FN:name\r\n"
		"ADR;LABEL=\"JobCompany\\nWorkAddress\\nline2\\nline3\\nPO BOX WorkP.O.Box\\nWorkC\r\n"
		" ity\\,  WorkState WorkZIp\\n\\nWORKCOUNTRY\";TYPE=WORK:WorkP.O.Box;line2\\nline\r\n"
		" 3;WorkAddress;WorkCity;WorkState;WorkZIp;WorkCountry\r\n"
		"ADR;LABEL=\"Home Address\\nPO BOX HomeP.O.Box\\nHomeCity\\,  HomeState HomeZIP\\\r\n"
		" n\\nHOMECOUNTRY\";TYPE=HOME:HomeP.O.Box;;Home Address;HomeCity;HomeState;Hom\r\n"
		" eZIP;HomeCountry\r\n"
		"ADR;LABEL=\"OtherAddress\\nOtherCity\\, OtherState\\nOtherZip\\nOtherP.O.Box\\nOt\r\n"
		" herCountry\";TYPE=OTHER:OtherP.O.Box;;OtherAddress;OtherCity;OtherState;Oth\r\n"
		" erZip;OtherCountry\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_30, E_VCARD_VERSION_30, E_VCARD_VERSION_40, 5,
		convert_quirks_adr_test_attrs, NULL);

	test_convert (vcard_40, E_VCARD_VERSION_40, E_VCARD_VERSION_30, 8,
		convert_quirks_adr_test_attrs, NULL);
}

static void
convert_quirks_anniversary_test_attrs (EVCard *converted,
				       gpointer user_data)
{
	const gchar *evc_field = user_data;
	EVCardAttribute *attr;
	GList *values;

	attr = e_vcard_get_attribute (converted, evc_field);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	if (e_vcard_get_version (converted) == E_VCARD_VERSION_40)
		g_assert_cmpstr (values->data, ==, "20201030");
	else
		g_assert_cmpstr (values->data, ==, "2020-10-30");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);
}

static void
test_convert_quirks_anniversary (void)
{
	const gchar *vcard_30 =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"X-EVOLUTION-ANNIVERSARY:2020-10-30\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_40 =
		"BEGIN:VCARD\r\n"
		"VERSION:4.0\r\n"
		"ANNIVERSARY:20201030\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_30, E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_anniversary_test_attrs, (gpointer) EVC_ANNIVERSARY);

	test_convert (vcard_40, E_VCARD_VERSION_40, E_VCARD_VERSION_30, 2,
		convert_quirks_anniversary_test_attrs, (gpointer) EVC_X_ANNIVERSARY);
}

typedef struct _QuirksImppValues {
	const gchar *attr_name;
	const gchar *attr_value;
	const gchar *param_name;
	const gchar *param_value;
} QuirksImppValues;

static void
convert_quirks_impp_test_attrs (EVCard *converted,
				gpointer user_data)
{
	const QuirksImppValues *expected_values = user_data;
	EVCardAttribute *attr;
	GList *values, *link;
	guint index;

	attr = e_vcard_get_attribute (converted, EVC_FN);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, "name");
	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);

	index = 0;

	for (link = e_vcard_get_attributes (converted); link; link = g_list_next (link)) {
		attr = link->data;

		if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_VERSION) == 0 ||
		    g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_FN) == 0)
			continue;

		g_assert_cmpint (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), expected_values[index].attr_name), ==, 0);

		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, expected_values[index].attr_value), ==, 0);

		if (expected_values[index].param_name) {
			values = e_vcard_attribute_get_param (attr, expected_values[index].param_name);
			g_assert_cmpuint (g_list_length (values), ==, 1);
			g_assert_cmpstr (values->data, ==, expected_values[index].param_value);
			g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 1);
		} else {
			g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, 0);
		}

		index++;
	}
}

static void
test_convert_quirks_impp (void)
{
	/* in the same order as in the vCard data */
	QuirksImppValues values_30[] = {
		#define item(_attr_name, _attr_value) { _attr_name, _attr_value, NULL, NULL }
		#define item2(_attr_name, _attr_value, _param_name, _param_value) { _attr_name, _attr_value, _param_name, _param_value }
		item2 ("X-AIM", "aim1", "X-EVOLUTION-UI-SLOT", "1"),
		item2 ("X-JABBER", "jabber1", "X-EVOLUTION-UI-SLOT", "2"),
		item2 ("X-YAHOO", "yahoo1", "X-EVOLUTION-UI-SLOT", "3"),
		item2 ("X-GADUGADU", "gadugadu1", "X-EVOLUTION-UI-SLOT", "4"),
		item ("X-MSN", "msn1"),
		item ("X-ICQ", "icq1"),
		item ("X-GROUPWISE", "groupwise1"),
		item ("X-SKYPE", "skype1"),
		item ("X-TWITTER", "twitter1"),
		item ("X-GOOGLE-TALK", "googletalk1"),
		item ("X-MATRIX", "matrix1"),
		item ("X-ICQ", "icq2"),
		item ("X-Evolution-IMPP", "otherone:nickname")
		#undef item
		#undef item2
	}, values_40[] = {
		#define item(_attr_value) { "IMPP", _attr_value, NULL, NULL }
		#define item2(_attr_value, _param_name, _param_value) { "IMPP", _attr_value, _param_name, _param_value }
		item2 ("aim:aim1", "X-EVOLUTION-UI-SLOT", "1"),
		item2 ("jabber:jabber1", "X-EVOLUTION-UI-SLOT", "2"),
		item2 ("yahoo:yahoo1", "X-EVOLUTION-UI-SLOT", "3"),
		item2 ("gadugadu:gadugadu1", "X-EVOLUTION-UI-SLOT", "4"),
		item ("msn:msn1"),
		item ("ICQ:icq1"),
		item ("groupwise:groupwise1"),
		item ("skype:skype1"),
		item ("twitter:twitter1"),
		item ("googletalk:googletalk1"),
		item ("matrix:matrix1"),
		item ("icq:icq2"),
		item ("otherone:nickname")
		#undef item
		#undef item2
	};
	const gchar *vcard_30 =
		"BEGIN:VCARD\r\n"
		"VERSION:3.0\r\n"
		"FN:name\r\n"
		"X-AIM;X-EVOLUTION-UI-SLOT=1:aim1\r\n"
		"X-JABBER;X-EVOLUTION-UI-SLOT=2:jabber1\r\n"
		"X-YAHOO;X-EVOLUTION-UI-SLOT=3:yahoo1\r\n"
		"X-GADUGADU;X-EVOLUTION-UI-SLOT=4:gadugadu1\r\n"
		"X-MSN:msn1\r\n"
		"X-ICQ:icq1\r\n"
		"X-GROUPWISE:groupwise1\r\n"
		"X-SKYPE:skype1\r\n"
		"X-TWITTER:twitter1\r\n"
		"X-GOOGLE-TALK:googletalk1\r\n"
		"X-MATRIX:matrix1\r\n"
		"X-ICQ:icq2\r\n"
		"X-Evolution-IMPP:otherone:nickname\r\n"
		"END:VCARD\r\n";
	const gchar *vcard_40 =
		"BEGIN:VCARD\r\n"
		"VERSION:4.0\r\n"
		"FN:name\r\n"
		"IMPP;X-EVOLUTION-UI-SLOT=1:aim:aim1\r\n"
		"IMPP;X-EVOLUTION-UI-SLOT=2:jabber:jabber1\r\n"
		"IMPP;X-EVOLUTION-UI-SLOT=3:yahoo:yahoo1\r\n"
		"IMPP;X-EVOLUTION-UI-SLOT=4:gadugadu:gadugadu1\r\n"
		"IMPP:msn:msn1\r\n"
		"IMPP:ICQ:icq1\r\n"
		"IMPP:groupwise:groupwise1\r\n"
		"IMPP:skype:skype1\r\n"
		"impp:twitter:twitter1\r\n"
		"IMPP:googletalk:googletalk1\r\n"
		"IMPP:matrix:matrix1\r\n"
		"IMPP:icq:icq2\r\n"
		"IMPP:otherone:nickname\r\n"
		"END:VCARD\r\n";

	test_convert (vcard_30, E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2 + G_N_ELEMENTS (values_40),
		convert_quirks_impp_test_attrs, values_40);

	test_convert (vcard_40, E_VCARD_VERSION_40, E_VCARD_VERSION_30, 2 + G_N_ELEMENTS (values_30),
		convert_quirks_impp_test_attrs, values_30);
}

typedef enum {
	QUIRKS_KIND_STATE_NONE_30,
	QUIRKS_KIND_STATE_X_LIST_TRUE,
	QUIRKS_KIND_STATE_X_LIST_FALSE,
	QUIRKS_KIND_STATE_X_KIND_GROUP,
	QUIRKS_KIND_STATE_X_KIND_OTHER,
	QUIRKS_KIND_STATE_X_KIND_BEFORE_X_LIST,
	QUIRKS_KIND_STATE_X_LIST_BEFORE_X_KIND,
	QUIRKS_KIND_STATE_NONE_40,
	QUIRKS_KIND_STATE_KIND_GROUP,
	QUIRKS_KIND_STATE_KIND_OTHER
} QuirksKindStates;

static void
convert_quirks_kind_test_attrs (EVCard *converted,
				gpointer user_data)
{
	QuirksKindStates state = GPOINTER_TO_INT (user_data);
	EVCardAttribute *attr;
	GList *values;

	switch (state) {
	case QUIRKS_KIND_STATE_NONE_30:
		break;
	case QUIRKS_KIND_STATE_X_LIST_TRUE:
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "group"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_X_LIST_FALSE:
		break;
	case QUIRKS_KIND_STATE_X_KIND_GROUP:
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "group"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_X_KIND_OTHER:
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "other"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_X_KIND_BEFORE_X_LIST:
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "other"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_X_LIST_BEFORE_X_KIND:
		attr = e_vcard_get_attribute (converted, EVC_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "group"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_NONE_40:
		break;
	case QUIRKS_KIND_STATE_KIND_GROUP:
		attr = e_vcard_get_attribute (converted, EVC_X_EVOLUTION_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "group"), ==, 0);

		attr = e_vcard_get_attribute (converted, EVC_X_LIST);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "TRUE"), ==, 0);
		break;
	case QUIRKS_KIND_STATE_KIND_OTHER:
		attr = e_vcard_get_attribute (converted, EVC_X_EVOLUTION_KIND);
		g_assert_nonnull (attr);
		values = e_vcard_attribute_get_values (attr);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_nonnull (values->data);
		g_assert_cmpint (g_ascii_strcasecmp (values->data, "other"), ==, 0);
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
test_convert_quirks_kind (void)
{
	#define VCARD_STR(_ver, _val) \
		"BEGIN:VCARD\r\n" \
		"VERSION:" _ver "\r\n" \
		_val "\r\n" \
		"END:VCARD\r\n"

	test_convert (VCARD_STR ("3.0", ""), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 1,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_NONE_30));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-LIST:true"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_LIST_TRUE));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-LIST:false"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 1,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_LIST_FALSE));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-KIND:GrouP"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_KIND_GROUP));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-KIND:other"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_KIND_OTHER));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-KIND:other\r\nX-EVOLUTION-LIST:TRUE"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_KIND_BEFORE_X_LIST));

	test_convert (VCARD_STR ("3.0", "X-EVOLUTION-LIST:TRUE\r\nX-EVOLUTION-KIND:other"), E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_X_LIST_BEFORE_X_KIND));

	test_convert (VCARD_STR ("4.0", ""), E_VCARD_VERSION_40, E_VCARD_VERSION_30, 1,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_NONE_40));

	test_convert (VCARD_STR ("4.0", "KIND:GrouP"), E_VCARD_VERSION_40, E_VCARD_VERSION_30, 3,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_KIND_GROUP));

	test_convert (VCARD_STR ("4.0", "KIND:other"), E_VCARD_VERSION_40, E_VCARD_VERSION_30, 2,
		convert_quirks_kind_test_attrs, GINT_TO_POINTER (QUIRKS_KIND_STATE_KIND_OTHER));

	#undef VCARD_STR
}

typedef struct _PhotoLogoCase {
	const gchar *vcard;
	const gchar *evc_field;
	const gchar *expected_type;
	const gchar *expected_encoding;
	const gchar *expected_value_type;
	const gchar *expected_value;
} PhotoLogoCase;

static void
convert_quirks_photo_logo_test_attrs (EVCard *converted,
				      gpointer user_data)
{
	PhotoLogoCase *case_data = user_data;
	EVCardAttribute *attr;
	GList *values;
	guint n_expected_params = 0;

	attr = e_vcard_get_attribute (converted, case_data->evc_field);
	g_assert_nonnull (attr);
	values = e_vcard_attribute_get_values (attr);
	g_assert_cmpuint (g_list_length (values), ==, 1);
	g_assert_cmpstr (values->data, ==, case_data->expected_value);

	if (case_data->expected_type) {
		values = e_vcard_attribute_get_param (attr, EVC_TYPE);
		g_assert_nonnull (values);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, case_data->expected_type);
		n_expected_params++;
	}

	if (case_data->expected_encoding) {
		values = e_vcard_attribute_get_param (attr, EVC_ENCODING);
		g_assert_nonnull (values);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, case_data->expected_encoding);
		n_expected_params++;
	}

	if (case_data->expected_value_type) {
		values = e_vcard_attribute_get_param (attr, EVC_VALUE);
		g_assert_nonnull (values);
		g_assert_cmpuint (g_list_length (values), ==, 1);
		g_assert_cmpstr (values->data, ==, case_data->expected_value_type);
		n_expected_params++;
	}

	g_assert_cmpuint (g_list_length (e_vcard_attribute_get_params (attr)), ==, n_expected_params);
}

static void
test_convert_quirks_photo_logo (void)
{
	#define VCARD_STR(_ver, _val) \
		"BEGIN:VCARD\r\n" \
		"VERSION:" _ver "\r\n" \
		_val "\r\n" \
		"END:VCARD\r\n"
	PhotoLogoCase cases_30[] = {
		#define item(_attr,_vlin,_tp,_enc,_vtp,_vlout) { VCARD_STR ("3.0", _attr _vlin), _attr, _tp, _enc, _vtp, _vlout }
		item (EVC_PHOTO, ";TYPE=png;ENCODING=b:aGVsbG8K", NULL, NULL, NULL, "data:image/png;base64,aGVsbG8K"),
		item (EVC_PHOTO, ";VALUE=uri:https://exmaple.com/image.jpg", NULL, NULL, "uri", "https://exmaple.com/image.jpg"),
		item (EVC_LOGO, ";TYPE=png;ENCODING=b:aGVsbG8K", NULL, NULL, NULL, "data:image/png;base64,aGVsbG8K"),
		item (EVC_LOGO, ";VALUE=uri:https://exmaple.com/image.jpg", NULL, NULL, "uri", "https://exmaple.com/image.jpg")
		#undef item
	}, cases_40[] = {
		#define item(_attr,_vlin,_tp,_enc,_vtp,_vlout) { VCARD_STR ("4.0", _attr _vlin), _attr, _tp, _enc, _vtp, _vlout }
		item (EVC_PHOTO, ":data:image/png;base64,aGVsbG8K", "png", "b", NULL, "aGVsbG8K"),
		item (EVC_PHOTO, ":https://exmaple.com/image.jpg", NULL, NULL, "uri", "https://exmaple.com/image.jpg"),
		item (EVC_LOGO, ":data:image/png;base64,aGVsbG8K", "png", "b", NULL, "aGVsbG8K"),
		item (EVC_LOGO, ":https://exmaple.com/image.jpg", NULL, NULL, "uri", "https://exmaple.com/image.jpg")
		#undef item
	};
	#undef VCARD_STR
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (cases_30); ii++) {
		test_convert (cases_30[ii].vcard, E_VCARD_VERSION_30, E_VCARD_VERSION_40, 2,
			convert_quirks_photo_logo_test_attrs, &(cases_30[ii]));
	}

	for (ii = 0; ii < G_N_ELEMENTS (cases_40); ii++) {
		test_convert (cases_40[ii].vcard, E_VCARD_VERSION_40, E_VCARD_VERSION_30, 2,
			convert_quirks_photo_logo_test_attrs, &(cases_40[ii]));
	}
}

static void
test_misc_nth_value (void)
{
	EVCardAttribute *attr;
	gchar *value;

	attr = e_vcard_attribute_new (NULL, "attr");
	g_assert_nonnull (attr);
	g_assert_cmpuint (e_vcard_attribute_get_n_values (attr), ==, 0);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, NULL);

	e_vcard_attribute_add_value (attr, "1st");
	g_assert_cmpuint (e_vcard_attribute_get_n_values (attr), ==, 1);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "1st");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 1), ==, NULL);
	g_assert_true (e_vcard_attribute_is_single_valued (attr));
	value = e_vcard_attribute_get_value (attr);
	g_assert_cmpstr (value, ==, "1st");
	g_free (value);

	e_vcard_attribute_add_value (attr, "2nd");
	g_assert_cmpuint (e_vcard_attribute_get_n_values (attr), ==, 2);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "1st");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 1), ==, "2nd");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 2), ==, NULL);
	g_assert_false (e_vcard_attribute_is_single_valued (attr));

	e_vcard_attribute_add_value (attr, "3rd");
	g_assert_cmpuint (e_vcard_attribute_get_n_values (attr), ==, 3);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "1st");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 1), ==, "2nd");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 2), ==, "3rd");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 3), ==, NULL);
	g_assert_false (e_vcard_attribute_is_single_valued (attr));

	e_vcard_attribute_free (attr);
}

typedef struct _ForeachTestData {
	guint n_called;
	guint stop_counter;
	const gchar *remove_email;
	gboolean has_eml1;
	gboolean has_eml2;
	gboolean has_eml3;
} ForeachTestData;

static void
foreach_test_data_init (ForeachTestData *data,
			guint stop_counter,
			const gchar *remove_email)
{
	data->n_called = 0;
	data->stop_counter = stop_counter;
	data->remove_email = remove_email;
	data->has_eml1 = FALSE;
	data->has_eml2 = FALSE;
	data->has_eml3 = FALSE;
}

static gboolean
test_count_attrs_cb (EVCard *vcard,
		     EVCardAttribute *attr,
		     gpointer user_data)
{
	ForeachTestData *data = user_data;

	data->n_called++;

	if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_EMAIL) == 0) {
		const gchar *value;

		value = e_vcard_attribute_get_nth_value (attr, 0);

		if (g_strcmp0 (value, "eml1") == 0)
			data->has_eml1 = TRUE;
		else if (g_strcmp0 (value, "eml2") == 0)
			data->has_eml2 = TRUE;
		else if (g_strcmp0 (value, "eml3") == 0)
			data->has_eml3 = TRUE;
	}

	return TRUE;
}

static gboolean
test_stop_counter_cb (EVCard *vcard,
		      EVCardAttribute *attr,
		      gpointer user_data)
{
	ForeachTestData *data = user_data;

	data->n_called++;

	if (!data->stop_counter)
		return FALSE;

	data->stop_counter--;

	return data->stop_counter > 0;
}

static gboolean
test_add_cb (EVCard *vcard,
	     EVCardAttribute *attr,
	     gpointer user_data)
{
	ForeachTestData *data = user_data;

	data->n_called++;

	if (g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_EMAIL) == 0) {
		const gchar *value = e_vcard_attribute_get_nth_value (attr, 0);
		gchar *new_value;

		new_value = g_strconcat (value, "-copy", NULL);

		if (strchr (value, '1') == 0)
			e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), new_value);
		else
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), new_value);

		g_free (new_value);
	}

	return TRUE;
}

static gboolean
test_remove_email_cb (EVCard *vcard,
		      EVCardAttribute *attr,
		      gpointer user_data)
{
	ForeachTestData *data = user_data;

	data->n_called++;

	return g_ascii_strcasecmp (e_vcard_attribute_get_name (attr), EVC_EMAIL) == 0 && (!data->remove_email ||
		g_strcmp0 (data->remove_email, e_vcard_attribute_get_nth_value (attr, 0)) == 0);
}

static void
test_misc_foreach (void)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	GList *attrs;
	ForeachTestData data;
	guint n_removed;

	vcard = e_vcard_new ();

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 0);
	attrs = e_vcard_get_attributes_by_name (vcard, EVC_VERSION);
	g_assert_cmpuint (g_list_length (attrs), ==, 0);

	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, "VErsiON"), "4.0");
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_FN), "name");

	attrs = e_vcard_get_attributes_by_name (vcard, EVC_VERSION);
	g_assert_cmpuint (g_list_length (attrs), ==, 1);
	attr = attrs->data;
	g_assert_nonnull (attr);
	g_assert_cmpstr (e_vcard_attribute_get_name (attr), ==, "VErsiON");
	g_list_free (attrs);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 2);
	g_assert_false (data.has_eml1);
	g_assert_false (data.has_eml2);
	g_assert_false (data.has_eml3);

	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), "eml1");
	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), "eml2");
	e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), "eml3");

	attrs = e_vcard_get_attributes_by_name (vcard, "eMaiL");
	g_assert_cmpuint (g_list_length (attrs), ==, 3);
	attr = attrs->data;
	g_assert_nonnull (attr);
	g_assert_cmpstr (e_vcard_attribute_get_name (attr), ==, EVC_EMAIL);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "eml2");
	attr = attrs->next->data;
	g_assert_nonnull (attr);
	g_assert_cmpstr (e_vcard_attribute_get_name (attr), ==, EVC_EMAIL);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "eml1");
	attr = attrs->next->next->data;
	g_assert_nonnull (attr);
	g_assert_cmpstr (e_vcard_attribute_get_name (attr), ==, EVC_EMAIL);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attr, 0), ==, "eml3");
	g_list_free (attrs);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 5);
	g_assert_true (data.has_eml1);
	g_assert_true (data.has_eml2);
	g_assert_true (data.has_eml3);

	foreach_test_data_init (&data, 3, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_stop_counter_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 3);
	g_assert_cmpuint (data.stop_counter, ==, 0);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_WILL_MODIFY, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 5);
	g_assert_true (data.has_eml1);
	g_assert_true (data.has_eml2);
	g_assert_true (data.has_eml3);

	foreach_test_data_init (&data, 3, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_WILL_MODIFY, test_stop_counter_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 3);
	g_assert_cmpuint (data.stop_counter, ==, 0);

	foreach_test_data_init (&data, 0, "eml2");
	n_removed = e_vcard_foreach_remove (vcard, test_remove_email_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 5);
	g_assert_cmpuint (n_removed, ==, 1);

	/* not there now */
	foreach_test_data_init (&data, 0, "eml2");
	n_removed = e_vcard_foreach_remove (vcard, test_remove_email_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 4);
	g_assert_cmpuint (n_removed, ==, 0);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 4);
	g_assert_true (data.has_eml1);
	g_assert_false (data.has_eml2);
	g_assert_true (data.has_eml3);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_WILL_MODIFY, test_add_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 4);

	foreach_test_data_init (&data, 0, NULL);
	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 6);
	g_assert_true (data.has_eml1);
	g_assert_false (data.has_eml2);
	g_assert_true (data.has_eml3);

	foreach_test_data_init (&data, 0, NULL);
	n_removed = e_vcard_foreach_remove (vcard, test_remove_email_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 6);
	g_assert_cmpuint (n_removed, ==, 4);
	foreach_test_data_init (&data, 0, NULL);

	e_vcard_foreach (vcard, E_VCARD_FOREACH_FLAG_NONE, test_count_attrs_cb, &data);
	g_assert_cmpuint (data.n_called, ==, 2);
	g_assert_false (data.has_eml1);
	g_assert_false (data.has_eml2);
	g_assert_false (data.has_eml3);

	g_clear_object (&vcard);
}

static void
test_misc_append_attributes_list (void)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	GList *attrs = NULL, *stored;

	vcard = e_vcard_new ();

	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_attribute_add_value (attr, "eml2");
	attrs = g_list_prepend (attrs, attr);
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_attribute_add_value (attr, "eml1");
	attrs = g_list_prepend (attrs, attr);

	e_vcard_append_attributes (vcard, attrs);

	stored = e_vcard_get_attributes (vcard);
	g_assert_cmpuint (g_list_length (stored), ==, 2);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->data, 0), ==, "eml1");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->next->data, 0), ==, "eml2");
	g_assert_true (stored->data != attrs->data);
	g_assert_true (stored->next->data != attrs->next->data);
	g_list_free_full (attrs, (GDestroyNotify) e_vcard_attribute_free);
	attrs = NULL;

	attr = e_vcard_attribute_new (NULL, EVC_FN);
	e_vcard_attribute_add_value (attr, "fn2");
	attrs = g_list_prepend (attrs, attr);
	attr = e_vcard_attribute_new (NULL, EVC_FN);
	e_vcard_attribute_add_value (attr, "fn1");
	attrs = g_list_prepend (attrs, attr);

	e_vcard_append_attributes_take (vcard, attrs);

	g_assert_cmpuint (g_list_length (stored), ==, 4);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->data, 0), ==, "eml1");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->next->data, 0), ==, "eml2");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->next->next->data, 0), ==, "fn1");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (stored->next->next->next->data, 0), ==, "fn2");
	g_assert_true (stored->next->next->data == attrs->data);
	g_assert_true (stored->next->next->next->data == attrs->next->data);

	g_clear_object (&vcard);
}

static void
test_misc_add_attributes (void)
{
	EVCard *vcard;
	GList *attrs;

	vcard = e_vcard_new ();

	e_vcard_add_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), "eml2");
	e_vcard_add_attribute_with_value_take (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), g_strdup ("eml1"));

	attrs = e_vcard_get_attributes (vcard);
	g_assert_cmpuint (g_list_length (attrs), ==, 2);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->data, 0), ==, "eml1");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->next->data, 0), ==, "eml2");

	e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), "eml3");
	e_vcard_append_attribute_with_value_take (vcard, e_vcard_attribute_new (NULL, EVC_EMAIL), g_strdup ("eml4"));

	g_assert_cmpuint (g_list_length (attrs), ==, 4);
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->data, 0), ==, "eml1");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->next->data, 0), ==, "eml2");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->next->next->data, 0), ==, "eml3");
	g_assert_cmpstr (e_vcard_attribute_get_nth_value (attrs->next->next->next->data, 0), ==, "eml4");

	g_clear_object (&vcard);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://gitlab.gnome.org/GNOME/evolution-data-server/-/issues/");

	g_test_add_func ("/Parsing/VCard/WithUID", test_vcard_with_uid);
	g_test_add_func ("/Parsing/VCard/WithoutUID", test_vcard_without_uid);
	g_test_add_func ("/Parsing/VCard/QuotedPrintable", test_vcard_quoted_printable);
	g_test_add_func ("/Parsing/VCard/Charset", test_vcard_charset);
	g_test_add_func ("/Parsing/VCard/CharsetMixed", test_vcard_charset_mixed);
	g_test_add_func ("/Parsing/VCard/CharsetBroken", test_vcard_charset_broken);
	g_test_add_func ("/Parsing/Contact/WithUID", test_contact_with_uid);
	g_test_add_func ("/Parsing/Contact/WithoutUID", test_contact_without_uid);
	g_test_add_func ("/Parsing/Contact/EmptyValue", test_contact_empty_value);
	g_test_add_func ("/Parsing/DateTime", test_parsing_date_time);
	g_test_add_func ("/Construction/VCardAttribute/WithGroup", test_construction_vcard_attribute_with_group);
	g_test_add_func ("/Convert/To21", test_convert_to_21);
	g_test_add_func ("/Convert/To30", test_convert_to_30);
	g_test_add_func ("/Convert/To40", test_convert_to_40);
	g_test_add_func ("/Convert/QuirksADR", test_convert_quirks_adr);
	g_test_add_func ("/Convert/QuirksANNIVERSARY", test_convert_quirks_anniversary);
	g_test_add_func ("/Convert/QuirksIMPP", test_convert_quirks_impp);
	g_test_add_func ("/Convert/QuirksKIND", test_convert_quirks_kind);
	g_test_add_func ("/Convert/QuirksPHOTOLOGO", test_convert_quirks_photo_logo);
	g_test_add_func ("/Misc/NthValue", test_misc_nth_value);
	g_test_add_func ("/Misc/Foreach", test_misc_foreach);
	g_test_add_func ("/Misc/AppendAttributesList", test_misc_append_attributes_list);
	g_test_add_func ("/Misc/AddAttributes", test_misc_add_attributes);

	return g_test_run ();
}

/* phone-numbers.c - Phone number tests
 *
 * Copyright (C) 2012 Philip Withnall
 *
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
 * Authors: Philip Withnall <philip@tecnocode.co.uk>
 */

#include <libebook/libebook.h>
#include <gdata/gdata.h>

#include "e-book-google-utils.h"

static GHashTable/*<string, string>*/ *
build_groups_by_name (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static GHashTable/*<string, string>*/ *
build_system_groups_by_id (void)
{
	GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_hash_table_insert (table, g_strdup (GDATA_CONTACTS_GROUP_CONTACTS), g_strdup ("contacts-group-id"));
	return table;
}

static gchar *
create_group_null (const gchar *category_name,
                   gpointer user_data,
                   GError **error)
{
	/* Must never be reached. */
	g_assert_not_reached ();
}

#define ENTRY_FROM_VCARD(entry, VCARD_PROPS) G_STMT_START { \
	EContact *contact; \
	GHashTable *groups_by_name, *system_groups_by_id; \
\
	groups_by_name = build_groups_by_name (); \
	system_groups_by_id = build_system_groups_by_id (); \
\
	contact = e_contact_new_from_vcard ( \
		"BEGIN:VCARD" "\n" \
		"VERSION:3.0" "\n" \
		"UID:foobar-baz" "\n" \
		"FN:Foobar Baz" "\n" \
		VCARD_PROPS \
		"END:VCARD" \
	); \
\
	entry = gdata_entry_new_from_e_contact (contact, groups_by_name, system_groups_by_id, create_group_null, NULL); \
	g_assert (entry != NULL); \
\
	g_hash_table_unref (system_groups_by_id); \
	g_hash_table_unref (groups_by_name); \
\
	g_object_unref (contact); \
} G_STMT_END

/* Include both an X-GOOGLE_LABEL and a TYPE attribute in the vCard and test that exactly one of them is copied to the entry. */
static void
test_label_and_type (void)
{
	GDataEntry *entry;
	GDataGDPhoneNumber *phone_number;

	g_test_bug ("675712");

	ENTRY_FROM_VCARD (entry, "TEL;X-GOOGLE-LABEL=VOICE;TYPE=PREF;X-EVOLUTION-UI-SLOT=1:+0123456789" "\n");

	/* Check that the entry has exactly one phone number, and that it contains exactly one of the rel and label properties. */
	phone_number = gdata_contacts_contact_get_primary_phone_number (GDATA_CONTACTS_CONTACT (entry));

	g_assert_cmpstr (gdata_gd_phone_number_get_relation_type (phone_number), ==, NULL);
	g_assert_cmpstr (gdata_gd_phone_number_get_label (phone_number), ==, "VOICE");

	g_object_unref (entry);
}

/* Include neither an X-GOOGLE_LABEL nor a TYPE attribute in the vCard and test that a suitable default appears in the entry. */
static void
test_label_nor_type (void)
{
	GDataEntry *entry;
	GDataGDPhoneNumber *phone_number;

	g_test_bug ("675712");

	ENTRY_FROM_VCARD (entry, "TEL;X-EVOLUTION-UI-SLOT=1:+0123456789" "\n");

	/* Check that the entry has exactly one phone number, and that it contains exactly one of the rel and label properties. */
	phone_number = gdata_contacts_contact_get_primary_phone_number (GDATA_CONTACTS_CONTACT (entry));

	g_assert_cmpstr (gdata_gd_phone_number_get_relation_type (phone_number), ==, GDATA_GD_PHONE_NUMBER_OTHER);
	g_assert_cmpstr (gdata_gd_phone_number_get_label (phone_number), ==, NULL);

	g_object_unref (entry);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("https://bugzilla.gnome.org/");

	g_test_add_func ("/phone-numbers/label-and-type", test_label_and_type);
	g_test_add_func ("/phone-numbers/label-nor-type", test_label_nor_type);

	return g_test_run ();
}

/* util.c - Google contact backend utility functions.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010 Philip Withnall
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#include <config.h>
#include <string.h>
#include <libsoup/soup.h>
#include <gdata/gdata.h>
#include <gdata/services/contacts/gdata-contacts-contact.h>
#include "util.h"

#define GOOGLE_PRIMARY_PARAM "X-EVOLUTION-UI-SLOT"
#define GOOGLE_LABEL_PARAM "X-GOOGLE-LABEL"
#define GDATA_ENTRY_XML_ATTR "X-GDATA-ENTRY-XML"
#define GDATA_ENTRY_LINK_ATTR "X-GDATA-ENTRY-LINK"

static void add_attribute_from_gdata_gd_email_address (EVCard *vcard, GDataGDEmailAddress *email);
static void add_attribute_from_gdata_gd_im_address (EVCard *vcard, GDataGDIMAddress *im);
static void add_attribute_from_gdata_gd_phone_number (EVCard *vcard, GDataGDPhoneNumber *number);
static void add_attribute_from_gdata_gd_postal_address (EVCard *vcard, GDataGDPostalAddress *address);
static void add_attribute_from_gdata_gd_organization (EVCard *vcard, GDataGDOrganization *org);

static GDataGDEmailAddress *gdata_gd_email_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDIMAddress *gdata_gd_im_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPhoneNumber *gdata_gd_phone_number_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPostalAddress *gdata_gd_postal_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDOrganization *gdata_gd_organization_from_attribute (EVCardAttribute *attr, gboolean *primary);

static gboolean is_known_google_im_protocol (const gchar *protocol);

GDataEntry *
_gdata_entry_new_from_e_contact (EContact *contact)
{
	GDataEntry *entry = GDATA_ENTRY (gdata_contacts_contact_new (NULL));

	if (_gdata_entry_update_from_e_contact (entry, contact))
		return entry;

	g_object_unref (entry);

	return NULL;
}

gboolean
_gdata_entry_update_from_e_contact (GDataEntry *entry, EContact *contact)
{
	GList *attributes, *iter;
	EContactName *name_struct = NULL;
	gboolean have_email_primary = FALSE;
	gboolean have_im_primary = FALSE;
	gboolean have_phone_primary = FALSE;
	gboolean have_postal_primary = FALSE;
	gboolean have_org_primary = FALSE;
	const gchar *title, *role, *note;
	#ifdef HAVE_GDATA_07
	EContactDate *bdate;
	const gchar *url;
	#endif

	attributes = e_vcard_get_attributes (E_VCARD (contact));

	/* N and FN */
	name_struct = e_contact_get (contact, E_CONTACT_NAME);
	if (name_struct) {
		GDataGDName *name;
		const gchar *given = NULL, *family = NULL;

		if (name_struct->given && *(name_struct->given) != '\0')
			given = name_struct->given;
		if (name_struct->family && *(name_struct->family) != '\0')
			family = name_struct->family;

		name = gdata_gd_name_new (given, family);
		if (name_struct->additional && *(name_struct->additional) != '\0')
			gdata_gd_name_set_additional_name (name, name_struct->additional);
		if (name_struct->prefixes && *(name_struct->prefixes) != '\0')
			gdata_gd_name_set_prefix (name, name_struct->prefixes);
		if (name_struct->suffixes && *(name_struct->suffixes) != '\0')
			gdata_gd_name_set_suffix (name, name_struct->suffixes);
		gdata_gd_name_set_full_name (name, e_contact_get (contact, E_CONTACT_FULL_NAME));

		gdata_contacts_contact_set_name (GDATA_CONTACTS_CONTACT (entry), name);
		g_object_unref (name);
	}

	/* NOTE */
	note = e_contact_get (contact, E_CONTACT_NOTE);
	if (note)
		gdata_entry_set_content (entry, note);
	else
		gdata_entry_set_content (entry, NULL);

	/* Clear out all the old attributes */
	gdata_contacts_contact_remove_all_email_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_phone_numbers (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_postal_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_im_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_organizations (GDATA_CONTACTS_CONTACT (entry));

	/* We walk them in reverse order, so we can find
	 * the correct primaries */
	iter = g_list_last (attributes);
	for (; iter; iter = iter->prev) {
		EVCardAttribute *attr;
		const gchar *name;

		attr = iter->data;
		name = e_vcard_attribute_get_name (attr);

		if (0 == g_ascii_strcasecmp (name, EVC_UID) ||
		    0 == g_ascii_strcasecmp (name, EVC_N) ||
		    0 == g_ascii_strcasecmp (name, EVC_FN) ||
		    0 == g_ascii_strcasecmp (name, EVC_LABEL) ||
		    0 == g_ascii_strcasecmp (name, EVC_VERSION) ||
		    0 == g_ascii_strcasecmp (name, EVC_X_FILE_AS) ||
		    0 == g_ascii_strcasecmp (name, EVC_TITLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_ROLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_NOTE)) {
			/* Ignore UID, VERSION, X-EVOLUTION-FILE-AS, N, FN, LABEL, TITLE, ROLE, NOTE */
		} else if (0 == g_ascii_strcasecmp (name, EVC_EMAIL)) {
			/* EMAIL */
			GDataGDEmailAddress *email;

			email = gdata_gd_email_address_from_attribute (attr, &have_email_primary);
			if (email) {
				gdata_contacts_contact_add_email_address (GDATA_CONTACTS_CONTACT (entry), email);
				g_object_unref (email);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_TEL)) {
			/* TEL */
			GDataGDPhoneNumber *number;

			number = gdata_gd_phone_number_from_attribute (attr, &have_phone_primary);
			if (number) {
				gdata_contacts_contact_add_phone_number (GDATA_CONTACTS_CONTACT (entry), number);
				g_object_unref (number);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_ADR)) {
			/* ADR (we ignore LABEL, since it should be the same as ADR, and ADR is more structured) */
			GDataGDPostalAddress *address;

			address = gdata_gd_postal_address_from_attribute (attr, &have_postal_primary);
			if (address) {
				gdata_contacts_contact_add_postal_address (GDATA_CONTACTS_CONTACT (entry), address);
				g_object_unref (address);
			}
		} else if (0 == g_ascii_strcasecmp (name, EVC_ORG)) {
			/* ORG */
			GDataGDOrganization *org;

			org = gdata_gd_organization_from_attribute (attr, &have_org_primary);
			if (org) {
				gdata_contacts_contact_add_organization (GDATA_CONTACTS_CONTACT (entry), org);
				g_object_unref (org);
			}
		} else if (0 == g_ascii_strncasecmp (name, "X-", 2) && is_known_google_im_protocol (name + 2)) {
			/* X-IM */
			GDataGDIMAddress *im;

			im = gdata_gd_im_address_from_attribute (attr, &have_im_primary);
			if (im) {
				gdata_contacts_contact_add_im_address (GDATA_CONTACTS_CONTACT (entry), im);
				g_object_unref (im);
			}
		} else if (e_vcard_attribute_is_single_valued (attr)) {
			gchar *value;

			/* Add the attribute as an extended property */
			value = e_vcard_attribute_get_value (attr);
			gdata_contacts_contact_set_extended_property (GDATA_CONTACTS_CONTACT (entry), name, value);
			g_free (value);
		} else {
			GList *values;

			values = e_vcard_attribute_get_values (attr);
			if (values && values->data && ((gchar *)values->data)[0])
				__debug__ ("unsupported vcard field: %s: %s", name, (gchar *)values->data);
		}
	}

	/* TITLE and ROLE */
	title = e_contact_get (contact, E_CONTACT_TITLE);
	role = e_contact_get (contact, E_CONTACT_ROLE);
	if (title || role) {
		GDataGDOrganization *org = NULL;

		/* Find an appropriate org: try to add them to the primary organization, but fall back to the first listed organization if none
		 * are marked as primary. */
		if (have_org_primary) {
			org = gdata_contacts_contact_get_primary_organization (GDATA_CONTACTS_CONTACT (entry));
		} else {
			GList *orgs = gdata_contacts_contact_get_organizations (GDATA_CONTACTS_CONTACT (entry));
			if (orgs)
				org = orgs->data;
		}

		/* Set the title and role */
		if (org && title)
			gdata_gd_organization_set_title (org, title);
		if (org && role)
			gdata_gd_organization_set_job_description (org, role);
	}

	#ifdef HAVE_GDATA_07
	gdata_contacts_contact_remove_all_websites (GDATA_CONTACTS_CONTACT (entry));

	url = e_contact_get_const (contact, E_CONTACT_HOMEPAGE_URL);
	if (url && *url) {
		GDataGContactWebsite *website = gdata_gcontact_website_new (url, GDATA_GCONTACT_WEBSITE_HOME_PAGE, NULL, FALSE);
		if (website) {
			gdata_contacts_contact_add_website (GDATA_CONTACTS_CONTACT (entry), website);
			g_object_unref (website);
		}
	}

	url = e_contact_get_const (contact, E_CONTACT_BLOG_URL);
	if (url && *url) {
		GDataGContactWebsite *website = gdata_gcontact_website_new (url, GDATA_GCONTACT_WEBSITE_BLOG, NULL, FALSE);
		if (website) {
			gdata_contacts_contact_add_website (GDATA_CONTACTS_CONTACT (entry), website);
			g_object_unref (website);
		}
	}

	gdata_contacts_contact_set_birthday (GDATA_CONTACTS_CONTACT (entry), NULL, TRUE);
	bdate = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (bdate) {
		GDate *gdate = g_date_new_dmy (bdate->day, bdate->month, bdate->year);

		if (gdate) {
			gdata_contacts_contact_set_birthday (GDATA_CONTACTS_CONTACT (entry), gdate, TRUE);
			g_date_free (gdate);
		}
		e_contact_date_free (bdate);
	}
	#endif

	return TRUE;
}

static void
foreach_extended_props_cb (const gchar *name, const gchar *value, EVCard *vcard)
{
	EVCardAttribute *attr;

	attr = e_vcard_attribute_new (NULL, name);
	e_vcard_add_attribute_with_value (vcard, attr, value);
}

EContact *
_e_contact_new_from_gdata_entry (GDataEntry *entry)
{
	EVCard *vcard;
	EVCardAttribute *attr;
	GList *email_addresses, *im_addresses, *phone_numbers, *postal_addresses, *orgs;
	const gchar *uid, *note;
	GList *itr;
	GDataGDName *name;
	GDataGDEmailAddress *email;
	GDataGDIMAddress *im;
	GDataGDPhoneNumber *phone_number;
	GDataGDPostalAddress *postal_address;
	GDataGDOrganization *org;
	GHashTable *extended_props;
	#ifdef HAVE_GDATA_07
	GList *websites;
	GDate bdate;
	gboolean bdate_has_year;
	#endif

	uid = gdata_entry_get_id (entry);
	if (NULL == uid)
		return NULL;

	vcard = E_VCARD (e_contact_new ());

	/* UID */
	attr = e_vcard_attribute_new (NULL, EVC_UID);
	e_vcard_add_attribute_with_value (vcard, attr, uid);

	/* FN, N */
	name = gdata_contacts_contact_get_name (GDATA_CONTACTS_CONTACT (entry));
	if (name) {
		EContactName name_struct;

		/* Set the full name */
		e_contact_set (E_CONTACT (vcard), E_CONTACT_FULL_NAME, gdata_gd_name_get_full_name (name));

		/* We just need to set the E_CONTACT_NAME field, and all the other name attribute values
		 * in the EContact will be populated automatically from that */
		name_struct.family = (gchar *) gdata_gd_name_get_family_name (name);
		name_struct.given = (gchar *) gdata_gd_name_get_given_name (name);
		name_struct.additional = (gchar *) gdata_gd_name_get_additional_name (name);
		name_struct.prefixes = (gchar *) gdata_gd_name_get_prefix (name);
		name_struct.suffixes = (gchar *) gdata_gd_name_get_suffix (name);

		e_contact_set (E_CONTACT (vcard), E_CONTACT_NAME, &name_struct);
	}

	/* NOTE */
	note = gdata_entry_get_content (entry);
	if (note)
		e_contact_set (E_CONTACT (vcard), E_CONTACT_NOTE, note);

	/* EMAIL - primary first */
	email = gdata_contacts_contact_get_primary_email_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_email_address (vcard, email);

	email_addresses = gdata_contacts_contact_get_email_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = email_addresses; itr; itr = itr->next) {
		email = itr->data;
		if (gdata_gd_email_address_is_primary (email) == TRUE)
			continue;
		add_attribute_from_gdata_gd_email_address (vcard, email);
	}

	/* X-IM - primary first */
	im = gdata_contacts_contact_get_primary_im_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_im_address (vcard, im);

	im_addresses = gdata_contacts_contact_get_im_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = im_addresses; itr; itr = itr->next) {
		im = itr->data;
		if (gdata_gd_im_address_is_primary (im) == TRUE)
			continue;
		add_attribute_from_gdata_gd_im_address (vcard, im);
	}

	/* TEL - primary first */
	phone_number = gdata_contacts_contact_get_primary_phone_number (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_phone_number (vcard, phone_number);

	phone_numbers = gdata_contacts_contact_get_phone_numbers (GDATA_CONTACTS_CONTACT (entry));
	for (itr = phone_numbers; itr; itr = itr->next) {
		phone_number = itr->data;
		if (gdata_gd_phone_number_is_primary (phone_number) == TRUE)
			continue;
		add_attribute_from_gdata_gd_phone_number (vcard, phone_number);
	}

	/* LABEL and ADR - primary first */
	postal_address = gdata_contacts_contact_get_primary_postal_address (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_postal_address (vcard, postal_address);

	postal_addresses = gdata_contacts_contact_get_postal_addresses (GDATA_CONTACTS_CONTACT (entry));
	for (itr = postal_addresses; itr; itr = itr->next) {
		postal_address = itr->data;
		if (gdata_gd_postal_address_is_primary (postal_address) == TRUE)
			continue;
		add_attribute_from_gdata_gd_postal_address (vcard, postal_address);
	}

	/* ORG - primary first */
	org = gdata_contacts_contact_get_primary_organization (GDATA_CONTACTS_CONTACT (entry));
	orgs = gdata_contacts_contact_get_organizations (GDATA_CONTACTS_CONTACT (entry));
	add_attribute_from_gdata_gd_organization (vcard, org);

	if (org || orgs) {
		if (!org)
			org = orgs->data;

		/* EVC_TITLE and EVC_ROLE from the primary organization (or the first organization in the list if there isn't a primary org) */
		attr = e_vcard_attribute_new (NULL, EVC_TITLE);
		e_vcard_add_attribute_with_value (vcard, attr, gdata_gd_organization_get_title (org));

		attr = e_vcard_attribute_new (NULL, EVC_ROLE);
		e_vcard_add_attribute_with_value (vcard, attr, gdata_gd_organization_get_job_description (org));
	}

	for (itr = orgs; itr; itr = itr->next) {
		org = itr->data;
		if (gdata_gd_organization_is_primary (org) == TRUE)
			continue;
		add_attribute_from_gdata_gd_organization (vcard, org);
	}

	/* Extended properties */
	extended_props = gdata_contacts_contact_get_extended_properties (GDATA_CONTACTS_CONTACT (entry));
	g_hash_table_foreach (extended_props, (GHFunc) foreach_extended_props_cb, vcard);

	#ifdef HAVE_GDATA_07
	websites = gdata_contacts_contact_get_websites (GDATA_CONTACTS_CONTACT (entry));
	for (itr = websites; itr != NULL; itr = itr->next) {
		GDataGContactWebsite *website = itr->data;
		const gchar *uri, *reltype;

		if (!website)
			continue;

		uri = gdata_gcontact_website_get_uri (website);
		reltype = gdata_gcontact_website_get_relation_type (website);

		if (!uri || !*uri || !reltype)
			continue;

		if (g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_HOME_PAGE))
			e_contact_set (E_CONTACT (vcard), E_CONTACT_HOMEPAGE_URL, uri);
		else if (g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_BLOG))
			e_contact_set (E_CONTACT (vcard), E_CONTACT_BLOG_URL, uri);
	}

	g_date_clear (&bdate, 1);
	bdate_has_year = gdata_contacts_contact_get_birthday (GDATA_CONTACTS_CONTACT (entry), &bdate);
	/* ignore birthdays without year */
	if (g_date_valid (&bdate) && bdate_has_year) {
		EContactDate *date = e_contact_date_new ();

		if (date) {
			date->day = g_date_get_day (&bdate);
			date->month =  g_date_get_month (&bdate);
			date->year = g_date_get_year (&bdate);

			e_contact_set (E_CONTACT (vcard), E_CONTACT_BIRTH_DATE, date);
			e_contact_date_free (date);
		}
	}
	#endif

	return E_CONTACT (vcard);
}

void
_e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry)
{
	EVCardAttribute *attr;
	gchar *entry_xml;
	GDataLink *link;

	/* Cache the XML representing the entry */
	entry_xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	attr = e_vcard_attribute_new ("", GDATA_ENTRY_XML_ATTR);
	e_vcard_attribute_add_value (attr, entry_xml);
	e_vcard_add_attribute (E_VCARD (contact), attr);
	g_free (entry_xml);

	/* Also add the update URI for the entry, since that's not serialised by gdata_parsable_get_xml */
	link = gdata_entry_look_up_link (entry, GDATA_LINK_EDIT);
	if (link != NULL) {
		attr = e_vcard_attribute_new ("", GDATA_ENTRY_LINK_ATTR);
		e_vcard_attribute_add_value (attr, gdata_link_get_uri (link));
		e_vcard_add_attribute (E_VCARD (contact), attr);
	}
}

void
_e_contact_remove_gdata_entry_xml (EContact *contact)
{
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_XML_ATTR);
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_LINK_ATTR);
}

const gchar *
_e_contact_get_gdata_entry_xml (EContact *contact, const gchar **edit_link)
{
	EVCardAttribute *attr;
	GList *values = NULL;

	/* Return the edit link if asked */
	if (edit_link != NULL) {
		attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_LINK_ATTR);
		if (attr != NULL)
			values = e_vcard_attribute_get_values (attr);
		if (values != NULL)
			*edit_link = values->data;
	}

	/* Return the entry's XML */
	attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_XML_ATTR);
	values = e_vcard_attribute_get_values (attr);

	return values ? values->data : NULL;
}

struct RelTypeMap {
	const gchar *rel;
	const gchar *types[2];
};

/* NOTE: These maps must be kept ordered with the one-to-many types first */
static const struct RelTypeMap rel_type_map_phone[] = {
	{ "home", { "HOME", "VOICE" }},
	{ "home_fax", { "HOME", "FAX" }},
	{ "work", { "WORK", "VOICE" }},
	{ "work_fax", { "WORK", "FAX" }},
	{ "work_mobile", { "WORK", "CELL" }},
	{ "work_pager", { "WORK", "PAGER" }},
	{ "assistant", { EVC_X_ASSISTANT, NULL }},
	{ "callback", { EVC_X_CALLBACK, NULL }},
	{ "car", { "CAR", NULL }},
	{ "company_main", {EVC_X_COMPANY, NULL }},
	{ "fax", { "FAX", NULL }},
	{ "isdn", { "ISDN", NULL }},
	{ "main", { "PREF", NULL }},
	{ "mobile", { "CELL", NULL }},
	{ "other", { "VOICE", NULL }},
	{ "other_fax", { "FAX", NULL }},
	{ "pager", { "PAGER", NULL }},
	{ "radio", { EVC_X_RADIO, NULL }},
	{ "telex", { EVC_X_TELEX, NULL }},
	{ "tty_tdd", { EVC_X_TTYTDD, NULL }}
};

static const struct RelTypeMap rel_type_map_im[] = {
	{ "home", { "HOME", NULL }},
	{ "netmeeting", { "NETMEETING", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static const struct RelTypeMap rel_type_map_others[] = {
	{ "home", { "HOME", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static gboolean
_add_type_param_from_google_rel (EVCardAttribute *attr, const struct RelTypeMap rel_type_map[], guint map_len, const gchar *rel)
{
	const gchar * field;
	guint i;

	field = strstr (rel ? rel : "", "#");
	if (NULL == field)
		return FALSE;

	field++;
	for (i = 0; i < map_len; i++) {
		if (0 == g_ascii_strcasecmp (rel_type_map[i].rel, field)) {
			EVCardAttributeParam *param;
			param = e_vcard_attribute_param_new ("TYPE");
			e_vcard_attribute_param_add_value (param, rel_type_map[i].types[0]);
			if (rel_type_map[i].types[1])
				e_vcard_attribute_param_add_value (param, rel_type_map[i].types[1]);
			e_vcard_attribute_add_param (attr, param);
			return TRUE;
		}
	}
	g_warning ("Unknown relationship '%s'", rel);

	return TRUE;
}

static gboolean
add_type_param_from_google_rel_phone (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone), rel);
}

static gboolean
add_type_param_from_google_rel_im (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_im, G_N_ELEMENTS (rel_type_map_im), rel);
}

static gboolean
add_type_param_from_google_rel (EVCardAttribute *attr, const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others), rel);
}

static void
add_label_param (EVCardAttribute *attr, const gchar *label)
{
	if (label && label[0] != '\0') {
		EVCardAttributeParam *param;
		param = e_vcard_attribute_param_new (GOOGLE_LABEL_PARAM);
		e_vcard_attribute_add_param_with_value (attr, param, label);
	}
}

static gchar *
_google_rel_from_types (GList *types, const struct RelTypeMap rel_type_map[], guint map_len)
{
	const gchar format[] = "http://schemas.google.com/g/2005#%s";
	guint i;

	/* For each of the entries in the map... */
	for (i = 0; i < map_len; i++) {
		GList *cur;
		gboolean first_matched = FALSE, second_matched = rel_type_map[i].types[1] ? FALSE : TRUE;

		/* ...iterate through all the vCard's types and see if two of them match the types in the current map entry. */
		for (cur = types; cur != NULL; cur = cur->next) {
			if (0 == g_ascii_strcasecmp (rel_type_map[i].types[0], cur->data))
				first_matched = TRUE;
			else if (!rel_type_map[i].types[1] || 0 == g_ascii_strcasecmp (rel_type_map[i].types[1], cur->data))
				second_matched = TRUE;

			/* If they do, return the rel value from that entry... */
			if (first_matched && second_matched)
				return g_strdup_printf (format, rel_type_map[i].rel);
		}
	}

	/* ...otherwise return an "other" result. */
	return g_strdup_printf (format, "other");
}

static gchar *
google_rel_from_types (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others));
}

static gchar *
google_rel_from_types_phone (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone));
}

static gboolean
is_known_google_im_protocol (const gchar *protocol)
{
	const gchar *known_protocols[] = {
		"AIM", "MSN", "YAHOO", "SKYPE", "QQ",
		"GOOGLE_TALK", "ICQ", "JABBER"
	};
	guint i;

	if (NULL == protocol)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS (known_protocols); i++) {
		if (0 == g_ascii_strcasecmp (known_protocols[i], protocol))
			return TRUE;
	}

	return FALSE;
}

static gchar *
field_name_from_google_im_protocol (const gchar *google_protocol)
{
	gchar *protocol;
	if (!google_protocol)
		return NULL;

	protocol = g_strrstr (google_protocol, "#");
	if (!protocol)
		return NULL;

	return g_strdup_printf ("X-%s", protocol + 1);
}

static gchar *
google_im_protocol_from_field_name (const gchar *field_name)
{
	const gchar format[] = "http://schemas.google.com/g/2005#%s";

	if (!field_name || strlen (field_name) < 3)
		return NULL;

	return g_strdup_printf (format, field_name + 2);
}

static void
add_primary_param (EVCardAttribute *attr, gboolean has_type)
{
	EVCardAttributeParam *param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
	e_vcard_attribute_add_param_with_value (attr, param, "1");

	if (!has_type) {
		param = e_vcard_attribute_param_new ("TYPE");
		e_vcard_attribute_add_param_with_value (attr, param, "PREF");
	}
}

static GList *
get_google_primary_type_label (EVCardAttribute *attr, gboolean *primary, const gchar **label)
{
	GList *params;
	GList *types = NULL;

	*primary = FALSE;
	*label = NULL;
	params = e_vcard_attribute_get_params (attr);

	while (params) {
		const gchar *name;

		name = e_vcard_attribute_param_get_name (params->data);
		if (g_ascii_strcasecmp (name, GOOGLE_PRIMARY_PARAM) == 0) {
			GList *values;

			values = e_vcard_attribute_param_get_values (params->data);
			if (values && values->data &&
				(((const gchar *)values->data)[0] == '1' ||
				 0 == g_ascii_strcasecmp (values->data, "yes"))) {
				*primary = TRUE;
			}
		}

		if (g_ascii_strcasecmp (name, GOOGLE_LABEL_PARAM) == 0) {
			GList *values;

			values = e_vcard_attribute_param_get_values (params->data);
			*label = values ? values->data : NULL;
		}

		if (g_ascii_strcasecmp (name, "TYPE") == 0)
			types = e_vcard_attribute_param_get_values (params->data);
		params = params->next;
	}

	return types;
}

static void
add_attribute_from_gdata_gd_email_address (EVCard *vcard, GDataGDEmailAddress *email)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!email || !gdata_gd_email_address_get_address (email))
		return;

	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_email_address_get_relation_type (email));
	if (gdata_gd_email_address_is_primary (email))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_email_address_get_label (email));

	e_vcard_attribute_add_value (attr, gdata_gd_email_address_get_address (email));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_im_address (EVCard *vcard, GDataGDIMAddress *im)
{
	EVCardAttribute *attr;
	gboolean has_type;
	gchar *field_name;

	if (!im || !gdata_gd_im_address_get_address (im))
		return;

	field_name = field_name_from_google_im_protocol (gdata_gd_im_address_get_protocol (im));
	if (!field_name)
		return;

	attr = e_vcard_attribute_new (NULL, field_name);
	has_type = add_type_param_from_google_rel_im (attr, gdata_gd_im_address_get_relation_type (im));
	if (gdata_gd_im_address_is_primary (im))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_im_address_get_label (im));

	e_vcard_attribute_add_value (attr, gdata_gd_im_address_get_address (im));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_phone_number (EVCard *vcard, GDataGDPhoneNumber *number)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!number || !gdata_gd_phone_number_get_number (number))
		return;

	attr = e_vcard_attribute_new (NULL, EVC_TEL);
	has_type = add_type_param_from_google_rel_phone (attr, gdata_gd_phone_number_get_relation_type (number));
	if (gdata_gd_phone_number_is_primary (number))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_phone_number_get_label (number));

	e_vcard_attribute_add_value (attr, gdata_gd_phone_number_get_number (number));

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_postal_address (EVCard *vcard, GDataGDPostalAddress *address)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!address || !gdata_gd_postal_address_get_address (address))
		return;

	/* Add the LABEL */
	attr = e_vcard_attribute_new (NULL, EVC_LABEL);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_postal_address_get_relation_type (address));
	if (gdata_gd_postal_address_is_primary (address))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_postal_address_get_label (address));

	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_address (address));

	if (attr)
		e_vcard_add_attribute (vcard, attr);

	/* Add the ADR */
	attr = e_vcard_attribute_new (NULL, EVC_ADR);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_postal_address_get_relation_type (address));
	if (gdata_gd_postal_address_is_primary (address))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_postal_address_get_label (address));

	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_po_box (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_house_name (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_street (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_city (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_region (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_postcode (address));
	e_vcard_attribute_add_value (attr, gdata_gd_postal_address_get_country (address));

	/* The following bits of data provided by the Google Contacts API can't be fitted into the vCard format:
	 *   gdata_gd_postal_address_get_mail_class
	 *   gdata_gd_postal_address_get_usage
	 *   gdata_gd_postal_address_get_agent
	 *   gdata_gd_postal_address_get_neighborhood
	 *   gdata_gd_postal_address_get_subregion
	 *   gdata_gd_postal_address_get_country_code */

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gdata_gd_organization (EVCard *vcard, GDataGDOrganization *org)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!org || !gdata_gd_organization_get_name (org))
		return;

	/* Add the LABEL */
	attr = e_vcard_attribute_new (NULL, EVC_ORG);
	has_type = add_type_param_from_google_rel (attr, gdata_gd_organization_get_relation_type (org));
	if (gdata_gd_organization_is_primary (org))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gd_organization_get_label (org));

	e_vcard_attribute_add_value (attr, gdata_gd_organization_get_name (org));
	e_vcard_attribute_add_value (attr, gdata_gd_organization_get_department (org));

	/* The following bits of data provided by the Google Contacts API can't be fitted into the vCard format:
	 *   gdata_gd_organization_get_title
	 *   gdata_gd_organization_get_job_description
	 *   gdata_gd_organization_get_symbol
	 *   gdata_gd_organization_get_location */

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static GDataGDEmailAddress *
gdata_gd_email_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDEmailAddress *email = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gchar *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		email = gdata_gd_email_address_new (values->data, rel, label, primary);
		g_free (rel);

		__debug__ ("New %semail entry %s (%s/%s)",
			   gdata_gd_email_address_is_primary (email) ? "primary " : "",
			   gdata_gd_email_address_get_address (email),
			   gdata_gd_email_address_get_relation_type (email),
			   gdata_gd_email_address_get_label (email));
	}

	return email;
}

static GDataGDIMAddress *
gdata_gd_im_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDIMAddress *im = NULL;
	GList *values;
	const gchar *name;

	name = e_vcard_attribute_get_name (attr);

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gchar *protocol, *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		protocol = google_im_protocol_from_field_name (name);
		im = gdata_gd_im_address_new (values->data, protocol, rel, label, primary);
		g_free (rel);
		g_free (protocol);

		__debug__ ("New %s%s entry %s (%s/%s)",
			   gdata_gd_im_address_is_primary (im) ? "primary " : "",
			   gdata_gd_im_address_get_protocol (im),
			   gdata_gd_im_address_get_address (im),
			   gdata_gd_im_address_get_relation_type (im),
			   gdata_gd_im_address_get_label (im));
	}

	return im;
}

static GDataGDPhoneNumber *
gdata_gd_phone_number_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDPhoneNumber *number = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types_phone (types);
		number = gdata_gd_phone_number_new (values->data, rel, label, NULL, primary);
		g_free (rel);

		__debug__ ("New %sphone-number entry %s (%s/%s)",
			   gdata_gd_phone_number_is_primary (number) ? "primary " : "",
			   gdata_gd_phone_number_get_number (number),
			   gdata_gd_phone_number_get_relation_type (number),
			   gdata_gd_phone_number_get_label (number));
	}

	return number;
}

static GDataGDPostalAddress *
gdata_gd_postal_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDPostalAddress *address = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types, *value;
		gchar *rel;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		address = gdata_gd_postal_address_new (rel, label, primary);
		g_free (rel);

		__debug__ ("New %spostal address entry %s (%s/%s)",
			   gdata_gd_postal_address_is_primary (address) ? "primary " : "",
			   gdata_gd_postal_address_get_address (address),
			   gdata_gd_postal_address_get_relation_type (address),
			   gdata_gd_postal_address_get_label (address));

		/* Set the components of the address from the vCard's attribute values */
		value = values;
		if (!value)
			return address;
		gdata_gd_postal_address_set_po_box (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_house_name (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_street (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_city (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_region (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_postcode (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		gdata_gd_postal_address_set_country (address, (*((gchar *) value->data) != '\0') ? value->data : NULL, NULL);
	}

	return address;
}

static GDataGDOrganization *
gdata_gd_organization_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
	GDataGDOrganization *org = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		rel = google_rel_from_types (types);
		org = gdata_gd_organization_new (values->data, NULL, rel, label, primary);
		if (values->next)
			gdata_gd_organization_set_department (org, values->next->data);
		g_free (rel);

		/* TITLE and ROLE are dealt with separately in _gdata_entry_update_from_e_contact() */

		__debug__ ("New %sorganization entry %s (%s/%s)",
			   gdata_gd_organization_is_primary (org) ? "primary " : "",
			   gdata_gd_organization_get_name (org),
			   gdata_gd_organization_get_relation_type (org),
			   gdata_gd_organization_get_label (org));
	}

	return org;
}

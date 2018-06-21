/* e-book-google-utils.c - Google contact conversion utilities.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010, 2011, 2012 Philip Withnall
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 *          Philip Withnall <philip@tecnocode.co.uk>
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <errno.h>

#include <glib/gi18n-lib.h>
#include <libebook/libebook.h>
#include <gdata/gdata.h>

#include "e-book-google-utils.h"

/* Definitions for our custom X-URIS vCard attribute for storing URIs.
 * See: bgo#659079. It would be nice to move this into EVCard sometime. */
#define GDATA_URIS_ATTR "X-URIS"
#define GDATA_URIS_TYPE_HOME_PAGE "X-HOME-PAGE"
#define GDATA_URIS_TYPE_BLOG "X-BLOG"
#define GDATA_URIS_TYPE_PROFILE "X-PROFILE"
#define GDATA_URIS_TYPE_FTP "X-FTP"

#define GOOGLE_SYSTEM_GROUP_ATTR "X-GOOGLE-SYSTEM-GROUP-IDS"

#define MULTIVALUE_ATTRIBUTE_SUFFIX "-MULTIVALUE"

gboolean __e_book_google_utils_debug__;
#define __debug__(...) (__e_book_google_utils_debug__ ? g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__) : (void) 0)

#define GOOGLE_PRIMARY_PARAM "X-EVOLUTION-UI-SLOT"
#define GOOGLE_LABEL_PARAM "X-GOOGLE-LABEL"
#define GDATA_ENTRY_XML_ATTR "X-GDATA-ENTRY-XML"
#define GDATA_ENTRY_LINK_ATTR "X-GDATA-ENTRY-LINK"

static void add_attribute_from_gdata_gd_email_address (EVCard *vcard, GDataGDEmailAddress *email);
static void add_attribute_from_gdata_gd_im_address (EVCard *vcard, GDataGDIMAddress *im);
static void add_attribute_from_gdata_gd_phone_number (EVCard *vcard, GDataGDPhoneNumber *number);
static void add_attribute_from_gdata_gd_postal_address (EVCard *vcard, GDataGDPostalAddress *address);
static void add_attribute_from_gdata_gd_organization (EVCard *vcard, GDataGDOrganization *org);
static void add_attribute_from_gc_contact_website (EVCard *vcard, GDataGContactWebsite *website);

static GDataGDEmailAddress *gdata_gd_email_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDIMAddress *gdata_gd_im_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPhoneNumber *gdata_gd_phone_number_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDPostalAddress *gdata_gd_postal_address_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGDOrganization *gdata_gd_organization_from_attribute (EVCardAttribute *attr, gboolean *primary);
static GDataGContactWebsite *gdata_gc_contact_website_from_attribute (EVCardAttribute *attr, gboolean *primary);

static gboolean is_known_google_im_protocol (const gchar *protocol);

GDataEntry *
gdata_entry_new_from_e_contact (EContact *contact,
				GHashTable *groups_by_name,
				GHashTable *system_groups_by_id,
				EContactGoogleCreateGroupFunc create_group,
				EBookBackendGoogle *bbgoogle,
				GCancellable *cancellable)
{
	GDataEntry *entry;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (groups_by_name != NULL, NULL);
	g_return_val_if_fail (system_groups_by_id != NULL, NULL);
	g_return_val_if_fail (g_hash_table_size (system_groups_by_id) > 0, FALSE);
	g_return_val_if_fail (create_group != NULL, NULL);

	entry = GDATA_ENTRY (gdata_contacts_contact_new (NULL));

	if (gdata_entry_update_from_e_contact (entry, contact, TRUE, groups_by_name, system_groups_by_id, create_group, bbgoogle, cancellable))
		return entry;

	g_object_unref (entry);

	return NULL;
}

static void
remove_anniversary (GDataContactsContact *contact)
{
	GList *events, *itr;

	events = gdata_contacts_contact_get_events (contact);
	if (!events)
		return;

	events = g_list_copy (events);
	g_list_foreach (events, (GFunc) g_object_ref, NULL);

	gdata_contacts_contact_remove_all_events (contact);
	for (itr = events; itr; itr = itr->next) {
		GDataGContactEvent *event = itr->data;

		if (g_strcmp0 (gdata_gcontact_event_get_relation_type (event), GDATA_GCONTACT_EVENT_ANNIVERSARY) != 0)
			gdata_contacts_contact_add_event (contact, event);
	}

	g_list_foreach (events, (GFunc) g_object_unref, NULL);
	g_list_free (events);
}

gboolean
gdata_entry_update_from_e_contact (GDataEntry *entry,
				   EContact *contact,
				   gboolean ensure_personal_group,
				   GHashTable *groups_by_name,
				   GHashTable *system_groups_by_id,
				   EContactGoogleCreateGroupFunc create_group,
				   EBookBackendGoogle *bbgoogle,
				   GCancellable *cancellable)
{
	GList *attributes, *iter, *category_names, *extended_property_names;
	EContactName *name_struct = NULL;
	EContactPhoto *photo;
	gboolean have_email_primary = FALSE;
	gboolean have_im_primary = FALSE;
	gboolean have_phone_primary = FALSE;
	gboolean have_postal_primary = FALSE;
	gboolean have_org_primary = FALSE;
	gboolean have_uri_primary = FALSE;
	gchar *title, *role, *note, *nickname;
	EContactDate *bdate;
	const gchar *url;

#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
	const gchar *file_as;
#endif
#endif

	g_return_val_if_fail (GDATA_IS_ENTRY (entry), FALSE);
	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (groups_by_name != NULL, FALSE);
	g_return_val_if_fail (system_groups_by_id != NULL, FALSE);
	g_return_val_if_fail (g_hash_table_size (system_groups_by_id) > 0, FALSE);
	g_return_val_if_fail (create_group != NULL, FALSE);

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

#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
	/* File as */
	file_as = e_contact_get (contact, E_CONTACT_FILE_AS);
	if (file_as && *file_as)
		gdata_contacts_contact_set_file_as (GDATA_CONTACTS_CONTACT (entry), file_as);
	else
		gdata_contacts_contact_set_file_as (GDATA_CONTACTS_CONTACT (entry), NULL);
#endif
#endif

	/* NOTE */
	note = e_contact_get (contact, E_CONTACT_NOTE);
	if (note)
		gdata_entry_set_content (entry, note);
	else
		gdata_entry_set_content (entry, NULL);
	g_free (note);

	/* Nickname */
	nickname = e_contact_get (contact, E_CONTACT_NICKNAME);
	gdata_contacts_contact_set_nickname (GDATA_CONTACTS_CONTACT (entry), nickname && *nickname ? nickname : NULL);
	g_free (nickname);

	/* Clear out all the old attributes */
	gdata_contacts_contact_remove_all_email_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_phone_numbers (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_postal_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_im_addresses (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_organizations (GDATA_CONTACTS_CONTACT (entry));
	gdata_contacts_contact_remove_all_websites (GDATA_CONTACTS_CONTACT (entry));

	category_names = gdata_contacts_contact_get_groups (GDATA_CONTACTS_CONTACT (entry));
	for (iter = category_names; iter != NULL; iter = g_list_delete_link (iter, iter))
		gdata_contacts_contact_remove_group (GDATA_CONTACTS_CONTACT (entry), iter->data);

	extended_property_names = g_hash_table_get_keys (gdata_contacts_contact_get_extended_properties (GDATA_CONTACTS_CONTACT (entry)));
	for (iter = extended_property_names; iter != NULL; iter = g_list_delete_link (iter, iter)) {
		gdata_contacts_contact_set_extended_property (GDATA_CONTACTS_CONTACT (entry), iter->data, NULL);
	}

	/* We walk them in reverse order, so we can find
	 * the correct primaries */
	iter = g_list_last (attributes);
	for (; iter; iter = iter->prev) {
		EVCardAttribute *attr;
		const gchar *name;

		attr = iter->data;
		name = e_vcard_attribute_get_name (attr);

		if (0 == g_ascii_strcasecmp (name, EVC_UID) ||
		    0 == g_ascii_strcasecmp (name, EVC_REV) ||
		    0 == g_ascii_strcasecmp (name, EVC_N) ||
		    0 == g_ascii_strcasecmp (name, EVC_FN) ||
		    0 == g_ascii_strcasecmp (name, EVC_LABEL) ||
		    0 == g_ascii_strcasecmp (name, EVC_VERSION) ||
		    0 == g_ascii_strcasecmp (name, EVC_X_FILE_AS) ||
		    0 == g_ascii_strcasecmp (name, EVC_TITLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_ROLE) ||
		    0 == g_ascii_strcasecmp (name, EVC_NOTE) ||
		    0 == g_ascii_strcasecmp (name, EVC_CATEGORIES) ||
		    0 == g_ascii_strcasecmp (name, EVC_PHOTO) ||
		    0 == g_ascii_strcasecmp (name, GOOGLE_SYSTEM_GROUP_ATTR) ||
		    0 == g_ascii_strcasecmp (name, e_contact_field_name (E_CONTACT_NICKNAME)) ||
		    0 == g_ascii_strcasecmp (name, E_GOOGLE_X_PHOTO_ETAG)) {
			/* Ignore attributes which are treated separately */
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
		} else if (0 == g_ascii_strcasecmp (name, GDATA_URIS_ATTR)) {
			/* X-URIS */
			GDataGContactWebsite *website;

			website =gdata_gc_contact_website_from_attribute (attr, &have_uri_primary);
			if (website) {
				gdata_contacts_contact_add_website (GDATA_CONTACTS_CONTACT (entry), website);
				g_object_unref (website);
			}
		} else if (e_vcard_attribute_is_single_valued (attr)) {
			gchar *value;

			/* Add the attribute as an extended property */
			value = e_vcard_attribute_get_value (attr);
			gdata_contacts_contact_set_extended_property (GDATA_CONTACTS_CONTACT (entry), name, value);
			g_free (value);
		} else {
			gchar *multi_name;
			GList *values, *l;
			GString *value;

			value = g_string_new ("");
			values = e_vcard_attribute_get_values (attr);

			for (l = values; l != NULL; l = l->next) {
				gchar *escaped = e_vcard_escape_string (l->data);
				g_string_append (value, escaped);
				if (l->next != NULL)
					g_string_append (value, ",");
				g_free (escaped);
			}
			multi_name = g_strconcat (name, MULTIVALUE_ATTRIBUTE_SUFFIX, NULL);
			gdata_contacts_contact_set_extended_property (GDATA_CONTACTS_CONTACT (entry), multi_name, value->str);
			g_free (multi_name);
			g_string_free (value, TRUE);
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
		if (org != NULL && title != NULL && *title != '\0')
			gdata_gd_organization_set_title (org, title);
		if (org != NULL && role != NULL && *role != '\0')
			gdata_gd_organization_set_job_description (org, role);
	}

	g_free (title);
	g_free (role);

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

	remove_anniversary (GDATA_CONTACTS_CONTACT (entry));
	bdate = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
	if (bdate) {
		GDate *gdate = g_date_new_dmy (bdate->day, bdate->month, bdate->year);

		if (gdate) {
			GDataGContactEvent *anni = gdata_gcontact_event_new (gdate, GDATA_GCONTACT_EVENT_ANNIVERSARY, NULL);

			if (anni) {
				gdata_contacts_contact_add_event (GDATA_CONTACTS_CONTACT (entry), anni);
				g_object_unref (anni);
			}

			g_date_free (gdate);
		}
		e_contact_date_free (bdate);
	}

	/* Map X-GOOGLE-SYSTEM-GROUP-IDS from outside to CATEGORIES.
	 * They will be mapped again to system group ids below; this is done
	 * so e-d-s / evolution (which use CATEGORIES), folks / gnome-contacts
	 * (which use X-GOOGLE-SYSTEM-GROUP-IDS) and google contacts (which
	 * uses the GData group IDs) all stay in sync */
	{
		EVCardAttribute *system_group_attr;
		EVCardAttribute *categories_attr;

		system_group_attr = e_vcard_get_attribute (E_VCARD (contact), GOOGLE_SYSTEM_GROUP_ATTR);
		categories_attr = e_vcard_get_attribute (E_VCARD (contact), EVC_CATEGORIES);

		if (system_group_attr) {
			GList *system_groups = e_vcard_attribute_get_values (system_group_attr);
			GList *sys_group;

			for (sys_group = system_groups; sys_group; sys_group = sys_group->next) {
				const gchar *category_name;

				category_name = e_contact_map_google_with_evo_group (sys_group->data, TRUE);

				if (!categories_attr) {
					categories_attr = e_vcard_attribute_new (NULL, EVC_CATEGORIES);
					e_vcard_append_attribute (E_VCARD (contact), categories_attr);
				}

				e_vcard_attribute_add_value (categories_attr, category_name);
			}
		}
	}

	/* CATEGORIES */
	for (category_names = e_contact_get (contact, E_CONTACT_CATEGORY_LIST); category_names != NULL; category_names = category_names->next) {
		gchar *category_id = NULL;
		const gchar *category_name = category_names->data;
		const gchar *system_group_id;

		if (category_name == NULL || *category_name == '\0')
			continue;

		system_group_id = e_contact_map_google_with_evo_group (category_name, FALSE);
		if (system_group_id) {
			const gchar *group_entry_id = g_hash_table_lookup (system_groups_by_id, system_group_id);

			g_warn_if_fail (group_entry_id != NULL);

			category_id = g_strdup (group_entry_id);
		}

		if (category_id == NULL)
			category_id = g_strdup (g_hash_table_lookup (groups_by_name, category_name));
		if (category_id == NULL) {
			GError *local_error = NULL;

			category_id = create_group (bbgoogle, category_name, cancellable, &local_error);
			if (category_id == NULL) {
				g_warning ("Error creating group '%s': %s", category_name, local_error ? local_error->message : "Unknown error");
				g_clear_error (&local_error);
				continue;
			}
		}

		/* Add the category to Evolution’s category list. */
		e_categories_add (category_name, NULL, NULL, TRUE);

		gdata_contacts_contact_add_group (GDATA_CONTACTS_CONTACT (entry), category_id);
		if (g_strcmp0 (system_group_id, GDATA_CONTACTS_GROUP_CONTACTS) == 0)
			ensure_personal_group = FALSE;
		g_free (category_id);
	}

	/* to have contacts shown in My Contacts by default,
	 * see https://bugzilla.gnome.org/show_bug.cgi?id=663324
	 * for more details */
	if (ensure_personal_group) {
		const gchar *group_entry_id = g_hash_table_lookup (system_groups_by_id, GDATA_CONTACTS_GROUP_CONTACTS);

		g_warn_if_fail (group_entry_id != NULL);

		if (group_entry_id)
			gdata_contacts_contact_add_group (GDATA_CONTACTS_CONTACT (entry), group_entry_id);
	}

	/* PHOTO */
	photo = e_contact_get (contact, E_CONTACT_PHOTO);

	if (photo != NULL && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
		g_object_set_data_full (G_OBJECT (entry), "photo", photo, (GDestroyNotify) e_contact_photo_free);
	} else {
		g_object_set_data (G_OBJECT (entry), "photo", NULL);

		if (photo != NULL) {
			e_contact_photo_free (photo);
		}
	}

	return TRUE;
}

static void
foreach_extended_props_cb (const gchar *name,
                           const gchar *value,
                           EVCard *vcard)
{
	EVCardAttribute *attr;
	gchar *multi_name;
	GString *str;
	const gchar *p;

	if (g_str_has_suffix (name, MULTIVALUE_ATTRIBUTE_SUFFIX)) {
		multi_name = g_strndup (name, strlen (name) - strlen (MULTIVALUE_ATTRIBUTE_SUFFIX));

		attr = e_vcard_attribute_new (NULL, multi_name);
		g_free (multi_name);
		str = g_string_new ("");

		/* Unescape a string as described in RFC2426, section 5, breaking at unescaped commas */
		for (p = value ? value : ""; *p; p++) {
			if (*p == '\\') {
				p++;
				if (*p == '\0') {
					g_string_append_c (str, '\\');
					break;
				}
				switch (*p) {
				case 'n':  g_string_append_c (str, '\n'); break;
				case 'r':  g_string_append_c (str, '\r'); break;
				case ';':  g_string_append_c (str, ';'); break;
				case ',':  g_string_append_c (str, ','); break;
				case '\\': g_string_append_c (str, '\\'); break;
				default:
					g_warning ("invalid escape, passing it through");
					g_string_append_c (str, '\\');
					g_string_append_c (str, *p);
					break;
				}
			} else if (*p == ',') {
				if (str->len > 0) {
					e_vcard_attribute_add_value (attr, str->str);
					g_string_set_size (str, 0);
				}
			} else {
				g_string_append_c (str, *p);
			}
		}

		if (str->len > 0) {
			e_vcard_attribute_add_value (attr, str->str);
			g_string_set_size (str, 0);
		}
		g_string_free (str, TRUE);

		e_vcard_add_attribute (vcard, attr);

	} else {
		attr = e_vcard_attribute_new (NULL, name);
		e_vcard_add_attribute_with_value (vcard, attr, value);
	}
}

EContact *
e_contact_new_from_gdata_entry (GDataEntry *entry,
                                GHashTable *groups_by_id,
                                GHashTable *system_groups_by_entry_id)
{
	EVCard *vcard;
	EVCardAttribute *attr, *system_group_ids_attr;
	GList *email_addresses, *im_addresses, *phone_numbers, *postal_addresses, *orgs, *category_names, *category_ids;
	const gchar *uid, *note, *nickname;
	GList *itr;
	GDataGDName *name;
	GDataGDEmailAddress *email;
	GDataGDIMAddress *im;
	GDataGDPhoneNumber *phone_number;
	GDataGDPostalAddress *postal_address;
	GDataGDOrganization *org;
	GHashTable *extended_props;
	GList *websites, *events;
	GDate bdate;
	GDateTime *dt;
	gchar *rev = NULL;
	gboolean bdate_has_year;
	gboolean have_uri_home = FALSE, have_uri_blog = FALSE;

#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
	const gchar *file_as;
#endif
#endif

	g_return_val_if_fail (system_groups_by_entry_id != NULL, NULL);
	g_return_val_if_fail (g_hash_table_size (system_groups_by_entry_id) > 0, FALSE);

	uid = gdata_entry_get_id (entry);
	if (NULL == uid)
		return NULL;

	vcard = E_VCARD (e_contact_new ());

	/* UID */
	attr = e_vcard_attribute_new (NULL, EVC_UID);
	e_vcard_add_attribute_with_value (vcard, attr, uid);

	if (gdata_entry_get_etag (entry))
		e_vcard_util_set_x_attribute (vcard, E_GOOGLE_X_ETAG, gdata_entry_get_etag (entry));

	/* REV */
	attr = e_vcard_attribute_new (NULL, EVC_REV);
	dt = g_date_time_new_from_unix_utc (gdata_entry_get_updated (entry));
	if (dt) {
		rev = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%SZ");
		g_date_time_unref (dt);
	}

	if (!rev)
		rev = g_strdup_printf ("%" G_GINT64_FORMAT, gdata_entry_get_updated (entry));

	e_vcard_add_attribute_with_value (vcard, attr, rev);

	g_free (rev);

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

#if defined(GDATA_CHECK_VERSION)
#if GDATA_CHECK_VERSION(0, 11, 0)
	/* File as */
	file_as = gdata_contacts_contact_get_file_as (GDATA_CONTACTS_CONTACT (entry));
	if (file_as && *file_as)
		e_contact_set (E_CONTACT (vcard), E_CONTACT_FILE_AS, file_as);
#endif
#endif

	/* NOTE */
	note = gdata_entry_get_content (entry);
	if (note)
		e_contact_set (E_CONTACT (vcard), E_CONTACT_NOTE, note);

	/* Nickname */
	nickname = gdata_contacts_contact_get_nickname (GDATA_CONTACTS_CONTACT (entry));
	if (nickname)
		e_contact_set (E_CONTACT (vcard), E_CONTACT_NICKNAME, nickname);

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

	/* TITLE, ROLE and ORG - primary first */
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
		add_attribute_from_gdata_gd_organization (vcard, org);
	}

	/* CATEGORIES */
	category_ids = gdata_contacts_contact_get_groups (GDATA_CONTACTS_CONTACT (entry));
	category_names = NULL;
	system_group_ids_attr = e_vcard_attribute_new ("", GOOGLE_SYSTEM_GROUP_ATTR);

	for (itr = category_ids; itr != NULL; itr = g_list_delete_link (itr, itr)) {
		gchar *category_id, *category_name;
		const gchar *system_group_id;

		category_id = e_contact_sanitise_google_group_id (itr->data);
		category_name = g_hash_table_lookup (groups_by_id, category_id);

		if (category_name != NULL) {
			if (g_list_find_custom (category_names, category_name, (GCompareFunc) g_strcmp0) == NULL) {
				category_names = g_list_prepend (category_names, category_name);

				/* Add the category to Evolution’s category list. */
				e_categories_add (category_name, NULL, NULL, TRUE);
			}
		} else
			g_warning ("Couldn't find name for category with ID '%s'.", category_id);

		/* Maintain a list of the IDs of the system groups the contact is in. */
		system_group_id = g_hash_table_lookup (system_groups_by_entry_id, category_id);
		if (system_group_id != NULL) {
			e_vcard_attribute_add_value (system_group_ids_attr, system_group_id);
		}

		g_free (category_id);
	}

	e_contact_set (E_CONTACT (vcard), E_CONTACT_CATEGORY_LIST, category_names);
	g_list_free (category_names);

	/* Expose the IDs of the system groups the contact is in so that libfolks (and other clients) can use the information
	 * without having to reverse-engineer it from the (localised) category names on the contact. */
	if (e_vcard_attribute_get_values (system_group_ids_attr) != NULL) {
		e_vcard_add_attribute (vcard, system_group_ids_attr);
	} else {
		e_vcard_attribute_free (system_group_ids_attr);
	}

	/* Extended properties */
	extended_props = gdata_contacts_contact_get_extended_properties (GDATA_CONTACTS_CONTACT (entry));
	g_hash_table_foreach (extended_props, (GHFunc) foreach_extended_props_cb, vcard);

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

		if (!have_uri_home && g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_HOME_PAGE)) {
			e_contact_set (E_CONTACT (vcard), E_CONTACT_HOMEPAGE_URL, uri);
			have_uri_home = TRUE;
		} else if (!have_uri_blog && g_str_equal (reltype, GDATA_GCONTACT_WEBSITE_BLOG)) {
			e_contact_set (E_CONTACT (vcard), E_CONTACT_BLOG_URL, uri);
			have_uri_blog = TRUE;
		} else {
			add_attribute_from_gc_contact_website (vcard, website);
		}
	}

	g_date_clear (&bdate, 1);
	bdate_has_year = gdata_contacts_contact_get_birthday (GDATA_CONTACTS_CONTACT (entry), &bdate);
	if (!bdate_has_year) {
		GTimeVal curr_time = { 0 };
		GDate tmp_date;

		g_get_current_time (&curr_time);
		g_date_clear (&tmp_date, 1);
		g_date_set_time_val (&tmp_date, &curr_time);

		g_date_set_year (&bdate, g_date_get_year (&tmp_date));
	}

	if (g_date_valid (&bdate)) {
		EContactDate *date = e_contact_date_new ();

		if (date) {
			date->day = g_date_get_day (&bdate);
			date->month = g_date_get_month (&bdate);
			date->year = g_date_get_year (&bdate);

			e_contact_set (E_CONTACT (vcard), E_CONTACT_BIRTH_DATE, date);
			e_contact_date_free (date);
		}
	}

	events = gdata_contacts_contact_get_events (GDATA_CONTACTS_CONTACT (entry));
	for (itr = events; itr; itr = itr->next) {
		GDataGContactEvent *event = itr->data;

		if (!event)
			continue;

		if (!gdata_gcontact_event_get_relation_type (event) ||
		    !g_str_equal (gdata_gcontact_event_get_relation_type (event), GDATA_GCONTACT_EVENT_ANNIVERSARY))
			continue;

		g_date_clear (&bdate, 1);
		gdata_gcontact_event_get_date (event, &bdate);

		if (g_date_valid (&bdate)) {
			EContactDate *date = e_contact_date_new ();

			if (date) {
				date->day = g_date_get_day (&bdate);
				date->month = g_date_get_month (&bdate);
				date->year = g_date_get_year (&bdate);

				e_contact_set (E_CONTACT (vcard), E_CONTACT_ANNIVERSARY, date);
				e_contact_date_free (date);
			}
		}

		break;
	}

	return E_CONTACT (vcard);
}

void
e_contact_add_gdata_entry_xml (EContact *contact,
                               GDataEntry *entry)
{
	EVCardAttribute *attr;
	gchar *entry_xml;
	GDataLink *edit_link;

	/* Cache the XML representing the entry */
	entry_xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	attr = e_vcard_attribute_new ("", GDATA_ENTRY_XML_ATTR);
	e_vcard_attribute_add_value (attr, entry_xml);
	e_vcard_add_attribute (E_VCARD (contact), attr);
	g_free (entry_xml);

	/* Also add the update URI for the entry, since that's not serialised by gdata_parsable_get_xml */
	edit_link = gdata_entry_look_up_link (entry, GDATA_LINK_EDIT);
	if (edit_link != NULL) {
		attr = e_vcard_attribute_new ("", GDATA_ENTRY_LINK_ATTR);
		e_vcard_attribute_add_value (attr, gdata_link_get_uri (edit_link));
		e_vcard_add_attribute (E_VCARD (contact), attr);
	}
}

void
e_contact_remove_gdata_entry_xml (EContact *contact)
{
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_XML_ATTR);
	e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_LINK_ATTR);
}

const gchar *
e_contact_get_gdata_entry_xml (EContact *contact,
                               const gchar **edit_uri)
{
	EVCardAttribute *attr;
	GList *values = NULL;

	/* Return the edit URI if asked */
	if (edit_uri != NULL) {
		attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_LINK_ATTR);
		if (attr != NULL)
			values = e_vcard_attribute_get_values (attr);
		if (values != NULL)
			*edit_uri = values->data;
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
	{ "isdn", { "ISDN", NULL }},
	{ "main", { "PREF", NULL }},
	{ "mobile", { "CELL", NULL }},
	{ "other", { "VOICE", NULL }},
	{ "other_fax", { "FAX", NULL }},
	{ "pager", { "PAGER", NULL }},
	{ "radio", { EVC_X_RADIO, NULL }},
	{ "telex", { EVC_X_TELEX, NULL }},
	{ "tty_tdd", { EVC_X_TTYTDD, NULL }},

	/* XXX This has no clear mapping to an EContact field.
	 *     It's listed here for completeness, but ordered
	 *     last so that "other_fax" is preferred. */
	{ "fax", { "FAX", NULL }}
};

static const struct RelTypeMap rel_type_map_im[] = {
	{ "home", { "HOME", NULL }},
	{ "netmeeting", { "NETMEETING", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static const struct RelTypeMap rel_type_map_uris[] = {
	{ GDATA_GCONTACT_WEBSITE_HOME_PAGE, { GDATA_URIS_TYPE_HOME_PAGE, NULL }},
	{ GDATA_GCONTACT_WEBSITE_BLOG, { GDATA_URIS_TYPE_BLOG, NULL }},
	{ GDATA_GCONTACT_WEBSITE_PROFILE, { GDATA_URIS_TYPE_PROFILE, NULL }},
	{ GDATA_GCONTACT_WEBSITE_FTP, { GDATA_URIS_TYPE_FTP, NULL }},
	{ GDATA_GCONTACT_WEBSITE_HOME, { "HOME", NULL }},
	{ GDATA_GCONTACT_WEBSITE_OTHER, { "OTHER", NULL }},
	{ GDATA_GCONTACT_WEBSITE_WORK, { "WORK", NULL }},
};

static const struct RelTypeMap rel_type_map_others[] = {
	{ "home", { "HOME", NULL }},
	{ "other", { "OTHER", NULL }},
	{ "work", { "WORK", NULL }},
};

static gboolean
_add_type_param_from_google_rel (EVCardAttribute *attr,
                                 const struct RelTypeMap rel_type_map[],
                                 guint map_len,
                                 const gchar *rel)
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
add_type_param_from_google_rel_phone (EVCardAttribute *attr,
                                      const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone), rel);
}

static gboolean
add_type_param_from_google_rel_im (EVCardAttribute *attr,
                                   const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_im, G_N_ELEMENTS (rel_type_map_im), rel);
}

static gboolean
add_type_param_from_google_rel_uris (EVCardAttribute *attr,
                                     const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_uris, G_N_ELEMENTS (rel_type_map_uris), rel);
}

static gboolean
add_type_param_from_google_rel (EVCardAttribute *attr,
                                const gchar *rel)
{
	return _add_type_param_from_google_rel (attr, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others), rel);
}

static void
add_label_param (EVCardAttribute *attr,
                 const gchar *label)
{
	if (label && label[0] != '\0') {
		EVCardAttributeParam *param;
		param = e_vcard_attribute_param_new (GOOGLE_LABEL_PARAM);
		e_vcard_attribute_add_param_with_value (attr, param, label);
	}
}

static gchar *
_google_rel_from_types (GList *types,
                        const struct RelTypeMap rel_type_map[],
                        guint map_len,
                        gboolean use_prefix)
{
	const gchar *format = "http://schemas.google.com/g/2005#%s";
	guint i;
	if (!use_prefix)
		format = "%s";

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
	return _google_rel_from_types (types, rel_type_map_others, G_N_ELEMENTS (rel_type_map_others), TRUE);
}

static gchar *
google_rel_from_types_phone (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_phone, G_N_ELEMENTS (rel_type_map_phone), TRUE);
}

static gchar *
google_rel_from_types_uris (GList *types)
{
	return _google_rel_from_types (types, rel_type_map_uris, G_N_ELEMENTS (rel_type_map_uris), FALSE);
}

static gboolean
is_known_google_im_protocol (const gchar *protocol)
{
	const gchar *known_protocols[] = {
		"AIM", "MSN", "YAHOO", "SKYPE", "QQ",
		"GOOGLE-TALK", "ICQ", "JABBER"
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

	if (strcmp ("#GOOGLE_TALK", protocol) == 0)
		return g_strdup (EVC_X_GOOGLE_TALK);
	else
		return g_strdup_printf ("X-%s", protocol + 1);
}

static gchar *
google_im_protocol_from_field_name (const gchar *field_name)
{
	const gchar format[] = "http://schemas.google.com/g/2005#%s";

	if (!field_name || strlen (field_name) < 3)
		return NULL;

	if (strcmp (field_name, EVC_X_GOOGLE_TALK) == 0)
		return g_strdup_printf (format, "GOOGLE_TALK");
	else
		return g_strdup_printf (format, field_name + 2);
}

static void
add_primary_param (EVCardAttribute *attr,
                   gboolean has_type)
{
	EVCardAttributeParam *param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
	e_vcard_attribute_add_param_with_value (attr, param, "1");

	if (!has_type) {
		param = e_vcard_attribute_param_new ("TYPE");
		e_vcard_attribute_add_param_with_value (attr, param, "PREF");
	}
}

static GList *
get_google_primary_type_label (EVCardAttribute *attr,
                               gboolean *primary,
                               const gchar **label)
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
				(((const gchar *) values->data)[0] == '1' ||
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
add_attribute_from_gdata_gd_email_address (EVCard *vcard,
                                           GDataGDEmailAddress *email)
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
add_attribute_from_gdata_gd_im_address (EVCard *vcard,
                                        GDataGDIMAddress *im)
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
add_attribute_from_gdata_gd_phone_number (EVCard *vcard,
                                          GDataGDPhoneNumber *number)
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
add_attribute_from_gdata_gd_postal_address (EVCard *vcard,
                                            GDataGDPostalAddress *address)
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
add_attribute_from_gdata_gd_organization (EVCard *vcard,
                                          GDataGDOrganization *org)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!org)
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
	 *   gdata_gd_organization_get_title (handled by TITLE)
	 *   gdata_gd_organization_get_job_description (handled by ROLE)
	 *   gdata_gd_organization_get_symbol
	 *   gdata_gd_organization_get_location */

	if (attr)
		e_vcard_add_attribute (vcard, attr);
}

static void
add_attribute_from_gc_contact_website (EVCard *vcard,
                                       GDataGContactWebsite *website)
{
	EVCardAttribute *attr;
	gboolean has_type;

	if (!website || !gdata_gcontact_website_get_uri (website))
		return;

	attr = e_vcard_attribute_new (NULL, GDATA_URIS_ATTR);
	has_type = add_type_param_from_google_rel_uris (attr, gdata_gcontact_website_get_relation_type (website));
	if (gdata_gcontact_website_is_primary (website))
		add_primary_param (attr, has_type);
	add_label_param (attr, gdata_gcontact_website_get_label (website));

	e_vcard_attribute_add_value (attr, gdata_gcontact_website_get_uri (website));

	e_vcard_add_attribute (vcard, attr);
}
static GDataGDEmailAddress *
gdata_gd_email_address_from_attribute (EVCardAttribute *attr,
                                       gboolean *have_primary)
{
	GDataGDEmailAddress *email = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gchar *rel = NULL;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		if (label == NULL) /* rel and label are mutually exclusive (bgo#675712) */
			rel = google_rel_from_types (types);
		email = gdata_gd_email_address_new (values->data, rel, label, primary);
		g_free (rel);

		__debug__ (
			"New %semail entry %s (%s/%s)",
			gdata_gd_email_address_is_primary (email) ? "primary " : "",
			gdata_gd_email_address_get_address (email),
			gdata_gd_email_address_get_relation_type (email),
			gdata_gd_email_address_get_label (email));
	}

	return email;
}

static GDataGDIMAddress *
gdata_gd_im_address_from_attribute (EVCardAttribute *attr,
                                    gboolean *have_primary)
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

		__debug__ (
			"New %s%s entry %s (%s/%s)",
			gdata_gd_im_address_is_primary (im) ? "primary " : "",
			gdata_gd_im_address_get_protocol (im),
			gdata_gd_im_address_get_address (im),
			gdata_gd_im_address_get_relation_type (im),
			gdata_gd_im_address_get_label (im));
	}

	return im;
}

static GDataGDPhoneNumber *
gdata_gd_phone_number_from_attribute (EVCardAttribute *attr,
                                      gboolean *have_primary)
{
	GDataGDPhoneNumber *number = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel = NULL;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		if (label == NULL) /* rel and label are mutually exclusive (bgo#675712) */
			rel = google_rel_from_types_phone (types);
		number = gdata_gd_phone_number_new (values->data, rel, label, NULL, primary);
		g_free (rel);

		__debug__ (
			"New %sphone-number entry %s (%s/%s)",
			gdata_gd_phone_number_is_primary (number) ? "primary " : "",
			gdata_gd_phone_number_get_number (number),
			gdata_gd_phone_number_get_relation_type (number),
			gdata_gd_phone_number_get_label (number));
	}

	return number;
}

static GDataGDPostalAddress *
gdata_gd_postal_address_from_attribute (EVCardAttribute *attr,
                                        gboolean *have_primary)
{
	GDataGDPostalAddress *address = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values && values->data) {
		GList *types, *value;
		gchar *rel = NULL;
		const gchar *label;
		gboolean primary;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		if (label == NULL) /* rel and label are mutually exclusive (bgo#675712) */
			rel = google_rel_from_types (types);
		address = gdata_gd_postal_address_new (rel, label, primary);
		g_free (rel);

		/* Set the components of the address from the vCard's attribute values */
		value = values;
		gdata_gd_postal_address_set_po_box (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		value = value->next;
		if (!value)
			return address;
		label = (*((gchar *) value->data) != '\0') ? value->data : NULL;
		value = value->next;
		if (!value) {
			gdata_gd_postal_address_set_street (address, label);
			return address;
		}
		if (label) {
			const gchar *value_str = (*((gchar *) value->data) != '\0') ? value->data : NULL;

			if (value_str) {
				gchar *tmp;

				tmp = g_strconcat (value_str, "\n", label, NULL);
				gdata_gd_postal_address_set_street (address, tmp);
				g_free (tmp);
			} else {
				gdata_gd_postal_address_set_street (address, label);
			}
		} else {
			gdata_gd_postal_address_set_street (address, (*((gchar *) value->data) != '\0') ? value->data : NULL);
		}
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

		/* Throw it away if nothing was set */
		if (gdata_gd_postal_address_get_po_box (address) == NULL && gdata_gd_postal_address_get_house_name (address) == NULL &&
		    gdata_gd_postal_address_get_street (address) == NULL && gdata_gd_postal_address_get_city (address) == NULL &&
		    gdata_gd_postal_address_get_region (address) == NULL && gdata_gd_postal_address_get_postcode (address) == NULL &&
		    gdata_gd_postal_address_get_country (address) == NULL) {
			g_object_unref (address);
			return NULL;
		}

		__debug__ (
			"New %spostal address entry %s (%s/%s)",
			gdata_gd_postal_address_is_primary (address) ? "primary " : "",
			gdata_gd_postal_address_get_address (address),
			gdata_gd_postal_address_get_relation_type (address),
			gdata_gd_postal_address_get_label (address));
	}

	return address;
}

static GDataGDOrganization *
gdata_gd_organization_from_attribute (EVCardAttribute *attr,
                                      gboolean *have_primary)
{
	GDataGDOrganization *org = NULL;
	GList *values;

	values = e_vcard_attribute_get_values (attr);
	if (values) {
		GList *types;
		gboolean primary;
		gchar *rel = NULL;
		const gchar *label;

		types = get_google_primary_type_label (attr, &primary, &label);
		if (!*have_primary)
			*have_primary = primary;
		else
			primary = FALSE;

		if (label == NULL) /* rel and label are mutually exclusive (bgo#675712) */
			rel = google_rel_from_types (types);
		org = gdata_gd_organization_new (values->data, NULL, rel, label, primary);
		if (values->next != NULL && values->next->data != NULL && *((gchar *) values->next->data) != '\0')
			gdata_gd_organization_set_department (org, values->next->data);
		g_free (rel);

		/* TITLE and ROLE are dealt with separately in gdata_entry_update_from_e_contact() */

		__debug__ (
			"New %sorganization entry %s (%s/%s)",
			gdata_gd_organization_is_primary (org) ? "primary " : "",
			gdata_gd_organization_get_name (org),
			gdata_gd_organization_get_relation_type (org),
			gdata_gd_organization_get_label (org));
	}

	return org;
}

static GDataGContactWebsite *
gdata_gc_contact_website_from_attribute (EVCardAttribute *attr,
                                         gboolean *have_primary)
{
	GDataGContactWebsite *website = NULL;
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

		rel = google_rel_from_types_uris (types);
		website = gdata_gcontact_website_new (values->data, rel, label, primary);
		g_free (rel);

		__debug__ (
			"New %suri entry %s (%s/%s)",
			gdata_gcontact_website_is_primary (website) ? "primary " : "",
			gdata_gcontact_website_get_uri (website),
			gdata_gcontact_website_get_relation_type (website),
			gdata_gcontact_website_get_label (website));
	}

	return website;
}

const gchar *
e_contact_map_google_with_evo_group (const gchar *group_name,
                                     gboolean google_to_evo)
{
	struct _GroupsMap {
		const gchar *google_id;
		const gchar *evo_name;
	} groups_map[] = {
		/* System Group: My Contacts */
		{ GDATA_CONTACTS_GROUP_CONTACTS,  N_("Personal") },
		/* System Group: Friends */
		{ GDATA_CONTACTS_GROUP_FRIENDS,   N_("Friends") },
		/* System Group: Family */
		{ GDATA_CONTACTS_GROUP_FAMILY,    N_("Family") },
		/* System Group: Coworkers */
		{ GDATA_CONTACTS_GROUP_COWORKERS, N_("Coworkers") }
	};
	guint ii;

	if (!group_name)
		return NULL;

	for (ii = 0; ii < G_N_ELEMENTS (groups_map); ii++) {
		if (google_to_evo) {
			if (g_str_equal (group_name, groups_map[ii].google_id))
				return _(groups_map[ii].evo_name);
		} else {
			if (g_str_equal (group_name, _(groups_map[ii].evo_name)))
				return groups_map[ii].google_id;
		}
	}

	return NULL;
}

gchar *
e_contact_sanitise_google_group_id (const gchar *group_id)
{
	gchar *id, *base;

	id = g_strdup (group_id);

	/* Fix the ID to refer to the full projection, rather than the base projection, because Google think that returning different IDs for the
	 * same object is somehow a good idea. */
	if (id != NULL) {
		base = strstr (id, "/base/");
		if (base != NULL)
			memcpy (base, "/full/", 6);
	}

	return id;
}

gchar *
e_contact_sanitise_google_group_name (GDataEntry *group)
{
	const gchar *system_group_id = gdata_contacts_group_get_system_group_id (GDATA_CONTACTS_GROUP (group));
	const gchar *evo_name;

	evo_name = e_contact_map_google_with_evo_group (system_group_id, TRUE);

	if (system_group_id == NULL) {
		return g_strdup (gdata_entry_get_title (group)); /* Non-system group */
	} else if (evo_name) {
		return g_strdup (evo_name);
	} else {
		g_warning ("Unknown system group '%s' for group with ID '%s'.", system_group_id, gdata_entry_get_id (group));
		return g_strdup (gdata_entry_get_title (group));
	}
}

gchar *
e_book_google_utils_time_to_revision (gint64 unix_time)
{
	struct tm stm;
	time_t tt = (time_t) unix_time;
	gchar time_string[100] = { 0 };

	gmtime_r (&tt, &stm);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", &stm);

	return g_strdup (time_string);
}

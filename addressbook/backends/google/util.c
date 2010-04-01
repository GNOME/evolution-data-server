/* util.c - Google contact backend utility functions.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
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

#include <string.h>
#include <libsoup/soup.h>
#include "util.h"

/*#define GOOGLE_PRIMARY_PARAM "X-GOOGLE-PRIMARY"*/
#define GOOGLE_PRIMARY_PARAM "X-EVOLUTION-UI-SLOT"
#define GOOGLE_LABEL_PARAM "X-GOOGLE-LABEL"

static EVCardAttribute*
attribute_from_gdata_entry_email_address  (GDataEntryEmailAddress  *email);

static EVCardAttribute*
attribute_from_gdata_entry_im_address     (GDataEntryIMAddress     *im);

static EVCardAttribute*
attribute_from_gdata_entry_phone_number   (GDataEntryPhoneNumber   *number);

static EVCardAttribute*
attribute_from_gdata_entry_postal_address (GDataEntryPostalAddress *address);

static GDataEntryEmailAddress*
gdata_entry_email_address_from_attribute  (EVCardAttribute         *attr,
                                           gboolean                *primary);

static GDataEntryIMAddress*
gdata_entry_im_address_from_attribute     (EVCardAttribute         *attr,
                                           gboolean                *primary);

static GDataEntryPhoneNumber*
gdata_entry_phone_number_from_attribute   (EVCardAttribute         *attr,
                                           gboolean                *primary);

static GDataEntryPostalAddress*
gdata_entry_postal_address_from_attribute (EVCardAttribute         *attr,
                                           gboolean                *primary);

static gboolean
is_known_google_im_protocol (const gchar *protocol);

GDataEntry*
_gdata_entry_new_from_e_contact (EContact *contact)
{
    GDataEntry *entry;
    GDataEntryCategory *category;

    entry = gdata_entry_new ();

    category = g_new0 (GDataEntryCategory, 1);
    category->scheme = g_strdup ("http://schemas.google.com/g/2005#kind");
    category->term = g_strdup ("http://schemas.google.com/contact/2008#contact");
    gdata_entry_set_categories (entry, g_slist_append (NULL, category));

    if (_gdata_entry_update_from_e_contact (entry, contact))
        return entry;

    g_object_unref (entry);

    return NULL;
}

gboolean
_gdata_entry_update_from_e_contact (GDataEntry *entry,
                                    EContact   *contact)
{
    GList *attributes, *iter;
    gchar *fullname = NULL;
    GSList *email_addresses = NULL;
    GSList *im_addresses = NULL;
    GSList *phone_numbers = NULL;
    GSList *postal_addresses = NULL;
    gboolean have_email_primary = FALSE;
    gboolean have_im_primary = FALSE;
    gboolean have_phone_primary = FALSE;
    gboolean have_postal_primary = FALSE;

    attributes = e_vcard_get_attributes (E_VCARD (contact));

    fullname = g_strdup (e_contact_get (contact, E_CONTACT_FULL_NAME));
    if (NULL == fullname) {
        EContactName *name = e_contact_get (contact, E_CONTACT_NAME);

        fullname = e_contact_name_to_string (name);
        e_contact_name_free (name);
    }

    /* We walk them in reverse order, so we can find
     * the correct primaries */
    iter = g_list_last (attributes);
    for (; iter; iter = iter->prev) {
        EVCardAttribute *attr;
        const gchar *name;

        attr = iter->data;
        name = e_vcard_attribute_get_name (attr);

        /* EMAIL */

        /* Ignore UID, VERSION, X-EVOLUTION-FILE-AS, N, FN */
        if (0 == g_ascii_strcasecmp (name, EVC_UID) ||
            0 == g_ascii_strcasecmp (name, EVC_N) ||
            0 == g_ascii_strcasecmp (name, EVC_FN) ||
            0 == g_ascii_strcasecmp (name, EVC_VERSION) ||
            0 == g_ascii_strcasecmp (name, EVC_X_FILE_AS)) {
        } else
        if (0 == g_ascii_strcasecmp (name, EVC_EMAIL)) {
            GDataEntryEmailAddress *email;

            email = gdata_entry_email_address_from_attribute
                        (attr, &have_email_primary);
            if (email) {
                email_addresses = g_slist_append (email_addresses,
                                                  email);
            }
        } else

        /* TEL */
        if (0 == g_ascii_strcasecmp (name, EVC_TEL)) {
            GDataEntryPhoneNumber *number;

            number = gdata_entry_phone_number_from_attribute
                        (attr, &have_phone_primary);
            if (number) {
                phone_numbers = g_slist_append (phone_numbers,
                                                number);
            }
        } else

        /* LABEL */
        if (0 == g_ascii_strcasecmp (name, EVC_LABEL)) {
            GDataEntryPostalAddress *address;

            address = gdata_entry_postal_address_from_attribute
                        (attr, &have_postal_primary);
            if (address) {
                postal_addresses = g_slist_append (postal_addresses,
                                                   address);
            }
        } else

        /* X-IM */
        if (0 == g_ascii_strncasecmp (name, "X-", 2) &&
            is_known_google_im_protocol (name + 2)) {
            GDataEntryIMAddress *im;

            im = gdata_entry_im_address_from_attribute
                        (attr, &have_im_primary);
            if (im) {
                im_addresses = g_slist_append (im_addresses,
                                               im);
            }
        } else {
            GList *values;

            values = e_vcard_attribute_get_values (attr);
            if (values && values->data && ((gchar *)values->data)[0]) {
                __debug__ ("unsupported vcard field: %s: %s", name, (gchar *)values->data);
            }
        }
    }
    gdata_entry_set_title (entry, fullname);
    g_free (fullname);
    gdata_entry_set_email_addresses (entry, email_addresses);
    gdata_entry_set_im_addresses (entry, im_addresses);
    gdata_entry_set_phone_numbers (entry, phone_numbers);
    gdata_entry_set_postal_addresses (entry, postal_addresses);

    return TRUE;
}

EContact*
_e_contact_new_from_gdata_entry (GDataEntry *entry)
{
    EVCard *vcard;
    EVCardAttribute *attr;
    GSList *email_addresses, *im_addresses, *phone_numbers, *postal_addresses;
    const gchar *name;
    const gchar *uid;
    GSList *itr;
    GDataEntryEmailAddress *email;
    GDataEntryIMAddress *im;
    GDataEntryPhoneNumber *phone_number;
    GDataEntryPostalAddress *postal_address;

    uid = gdata_entry_get_id (entry);
    if (NULL == uid) {
        return NULL;
    }

    vcard = E_VCARD (e_contact_new ());

    /* UID */
    attr = e_vcard_attribute_new (NULL, EVC_UID);
    e_vcard_add_attribute_with_value (vcard, attr, uid);

    /* FN - TODO: get title */
    name = gdata_entry_get_title (entry);
    if (name) {
        e_contact_set (E_CONTACT (vcard), E_CONTACT_FULL_NAME, (gconstpointer)name);
    }

    /* EMAIL - primary first */
    email = gdata_entry_get_primary_email_address (entry);
    attr = attribute_from_gdata_entry_email_address (email);
    if (attr) {
        e_vcard_add_attribute (vcard, attr);
    }

    email_addresses = gdata_entry_get_email_addresses (entry);
    for (itr = email_addresses; itr; itr = itr->next) {
        email = itr->data;
        if (TRUE == email->primary)
            continue;
        attr = attribute_from_gdata_entry_email_address (email);
        if (attr) {
            e_vcard_add_attribute (vcard, attr);
        }
    }

    /* X-IM - primary first */
    im = gdata_entry_get_primary_im_address (entry);
    attr = attribute_from_gdata_entry_im_address (im);
    if (attr) {
        e_vcard_add_attribute (vcard, attr);
    }
    im_addresses = gdata_entry_get_im_addresses (entry);
    for (itr = im_addresses; itr; itr = itr->next) {
        im = itr->data;
        if (TRUE == im->primary)
            continue;
        attr = attribute_from_gdata_entry_im_address (im);
        if (attr) {
            e_vcard_add_attribute (vcard, attr);
        }
    }

    /* TEL - primary first */
    phone_number = gdata_entry_get_primary_phone_number (entry);
    attr = attribute_from_gdata_entry_phone_number (phone_number);
    if (attr) {
        e_vcard_add_attribute (vcard, attr);
    }
    phone_numbers = gdata_entry_get_phone_numbers (entry);
    for (itr = phone_numbers; itr; itr = itr->next) {
        phone_number = itr->data;
        if (TRUE == phone_number->primary)
            continue;
        attr = attribute_from_gdata_entry_phone_number (phone_number);
        if (attr) {
            e_vcard_add_attribute (vcard, attr);
        }
    }

    /* LABEL - primary first TODO: ADR */
    postal_address = gdata_entry_get_primary_postal_address (entry);
    attr = attribute_from_gdata_entry_postal_address (postal_address);
    if (attr) {
        e_vcard_add_attribute (vcard, attr);
    }
    postal_addresses = gdata_entry_get_postal_addresses (entry);
    for (itr = postal_addresses; itr; itr = itr->next) {
        postal_address = itr->data;
        if (TRUE == postal_address->primary)
            continue;
        attr = attribute_from_gdata_entry_postal_address (postal_address);
        if (attr) {
            e_vcard_add_attribute (vcard, attr);
        }
    }

    return E_CONTACT (vcard);
}

#define GDATA_ENTRY_XML_ATTR "X-GDATA-ENTRY-XML"

void
_e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry)
{
    EVCardAttribute *attr;
    const gchar * entry_xml;

    entry_xml = gdata_entry_generate_xml (entry);

    attr = e_vcard_attribute_new ("", GDATA_ENTRY_XML_ATTR);
    e_vcard_attribute_add_value (attr, entry_xml);
    e_vcard_add_attribute (E_VCARD (contact), attr);
}

void
_e_contact_remove_gdata_entry_xml (EContact *contact)
{
    e_vcard_remove_attributes (E_VCARD (contact), NULL, GDATA_ENTRY_XML_ATTR);
}

const gchar *
_e_contact_get_gdata_entry_xml (EContact *contact)
{
    EVCardAttribute *attr;
    GList *values;

    attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_XML_ATTR);
    values = e_vcard_attribute_get_values (attr);

    return values ? values->data : NULL;
}

struct RelTypeMap {
    const gchar * rel;
    const gchar * types[3];
};

static const struct RelTypeMap rel_type_map_phone[] = {
    {"fax", { "FAX", NULL, NULL}},
    {"home", { "HOME", "VOICE", NULL}},
    {"home_fax", { "HOME", "FAX", NULL}},
    {"mobile", { "CELL", NULL, NULL}},
    {"other", { "VOICE", NULL, NULL}},
    {"pager", { "PAGER", NULL, NULL}},
    {"work", { "WORK", "VOICE", NULL}},
    {"work_fax", { "WORK", "FAX", NULL}}
};

static const struct RelTypeMap rel_type_map_others[] = {
    {"home", { "HOME", NULL, NULL}},
    {"other", { "OTHER", NULL, NULL}},
    {"work", { "WORK", NULL, NULL}},
};

static gboolean
_add_type_param_from_google_rel (EVCardAttribute *attr,
                                 const struct RelTypeMap rel_type_map[],
                                 gint map_len,
                                 const gchar *rel)
{
    const gchar * field;
    gint i;

    field = strstr (rel ? rel : "", "#");
    if (NULL == field)
        return FALSE;

    field++;
    for (i = 0; i < map_len; i++) {
        if (0 == g_ascii_strcasecmp (rel_type_map[i].rel, field)) {
            EVCardAttributeParam *param;
            const gchar * const * type;
            param = e_vcard_attribute_param_new ("TYPE");
            for (type = rel_type_map[i].types; *type; type++) {
                e_vcard_attribute_param_add_value (param, *type);
            }
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
    return _add_type_param_from_google_rel (attr,
                                            rel_type_map_phone,
                                            G_N_ELEMENTS (rel_type_map_phone),
                                            rel);
}

static gboolean
add_type_param_from_google_rel (EVCardAttribute *attr, const gchar *rel)
{
    return _add_type_param_from_google_rel (attr,
                                            rel_type_map_others,
                                            G_N_ELEMENTS (rel_type_map_others),
                                            rel);
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
_google_rel_from_types (GList *types,
                        const struct RelTypeMap rel_type_map[],
                        gint map_len)
{
    const gchar format[] = "http://schemas.google.com/g/2005#%s";

    while (types) {
        gint i;
        GList *cur = types;
        types = types->next;

        for (i = 0; i < map_len; i++) {
            if (0 == g_ascii_strcasecmp (rel_type_map[i].types[0], cur->data)) {
                while (types && rel_type_map[i].types[1]) {
                    if (0 == g_ascii_strcasecmp (rel_type_map[i].types[1], types->data)) {
                        return g_strdup_printf (format, rel_type_map[i].rel);
                    }
                    types = types->next;
                }
                return g_strdup_printf (format, rel_type_map[i].rel);
            }
        }
    }

    return g_strdup_printf (format, "other");
}

static gchar *
google_rel_from_types (GList *types)
{
    return _google_rel_from_types (types,
                                   rel_type_map_others,
                                   G_N_ELEMENTS (rel_type_map_others));
}

static gchar *
google_rel_from_types_phone (GList *types)
{
    return _google_rel_from_types (types,
                                   rel_type_map_phone,
                                   G_N_ELEMENTS (rel_type_map_phone));
}

static gboolean
is_known_google_im_protocol (const gchar *protocol)
{
    const gchar *known_protocols[] =
    {
        "AIM", "MSN", "YAHOO", "SKYPE", "QQ",
        "GOOGLE_TALK", "ICQ", "JABBER"
    };
    gint i;

    if (NULL == protocol)
        return FALSE;

    for (i = 0; i < G_N_ELEMENTS (known_protocols); i++) {
        if (0 == g_ascii_strcasecmp (known_protocols[i], protocol))
            return TRUE;
    }
    return FALSE;
}

static gchar *
field_name_from_google_im_protocol (const gchar * google_protocol)
{
    gchar *protocol;
    if (NULL == google_protocol)
        return NULL;

    protocol = g_strrstr (google_protocol, "#");
    if (NULL == protocol)
        return NULL;
    return g_strdup_printf ("X-%s", protocol + 1);
}

static gchar *
google_im_protocol_from_field_name (const gchar * field_name)
{
    const gchar format[] = "http://schemas.google.com/g/2005#%s";

    if (NULL == field_name ||
        strlen (field_name) < 3) {
        return NULL;
    }

    return g_strdup_printf (format, field_name + 2);
}

static void
add_primary_param (EVCardAttribute *attr, gboolean has_type)
{
    EVCardAttributeParam *param;
    param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
    e_vcard_attribute_add_param_with_value (attr, param, "1");
    if (FALSE == has_type) {
        param = e_vcard_attribute_param_new ("TYPE");
        e_vcard_attribute_add_param_with_value (attr, param, "PREF");
    }
}

static GList*
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
        if (0 == g_ascii_strcasecmp (name, GOOGLE_PRIMARY_PARAM)) {
            GList *values;

            values = e_vcard_attribute_param_get_values (params->data);
            if (values && values->data &&
                (((const gchar *)values->data)[0] == '1' ||
                 0 == g_ascii_strcasecmp (values->data, "yes"))) {
                *primary = TRUE;
            }
        }
        if (0 == g_ascii_strcasecmp (name, GOOGLE_LABEL_PARAM)) {
            GList *values;

            values = e_vcard_attribute_param_get_values (params->data);
            *label = values ? values->data : NULL;
        }
        if (0 == g_ascii_strcasecmp (name, "TYPE")) {
            types = e_vcard_attribute_param_get_values (params->data);
        }
        params = params->next;
    }
    return types;
}

static EVCardAttribute*
attribute_from_gdata_entry_email_address (GDataEntryEmailAddress *email)
{
    EVCardAttribute *attr;
    gboolean has_type;

    if (NULL == email || NULL == email->address)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
    has_type = add_type_param_from_google_rel (attr, email->rel);
    if (email->primary) {
        add_primary_param (attr, has_type);
    }
    add_label_param (attr, email->label);
    e_vcard_attribute_add_value (attr, email->address);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_im_address (GDataEntryIMAddress *im)
{
    EVCardAttribute *attr;
    gboolean has_type;
    gchar *field_name;

    if (NULL == im || NULL == im->address)
        return NULL;;

    field_name = field_name_from_google_im_protocol (im->protocol);
    if (NULL == field_name)
        return NULL;

    attr = e_vcard_attribute_new (NULL, field_name);
    has_type = add_type_param_from_google_rel (attr, im->rel);
    if (im->primary) {
        add_primary_param (attr, has_type);
    }
    add_label_param (attr, im->label);
    e_vcard_attribute_add_value (attr, im->address);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_phone_number (GDataEntryPhoneNumber *number)
{
    EVCardAttribute *attr;
    gboolean has_type;

    if (NULL == number || NULL == number->number)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_TEL);
    has_type = add_type_param_from_google_rel_phone (attr, number->rel);
    if (number->primary) {
        add_primary_param (attr, has_type);
    }
    add_label_param (attr, number->label);
    e_vcard_attribute_add_value (attr, number->number);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_postal_address (GDataEntryPostalAddress *address)
{
    EVCardAttribute *attr;
    gboolean has_type;

    if (NULL == address || NULL == address->address)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_LABEL);
    has_type = add_type_param_from_google_rel (attr, address->rel);
    if (address->primary) {
        add_primary_param (attr, has_type);
    }
    add_label_param (attr, address->label);
    e_vcard_attribute_add_value (attr, address->address);
    return attr;
}

static GDataEntryEmailAddress*
gdata_entry_email_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryEmailAddress *email = NULL;
    GList *values;

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        GList *types;
        const gchar *label;
        gboolean primary;

        types = get_google_primary_type_label (attr, &primary, &label);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        email = g_new0 (GDataEntryEmailAddress, 1);
        email->address = g_strdup (values->data);
        email->rel = google_rel_from_types (types);
        email->label = g_strdup (label);
        email->primary = primary;
        __debug__ ("New %semail entry %s (%s/%s)",
                    email->primary ? "primary " : "",
                    email->address,
                    email->rel,
                    email->label);
    }

    return email;
}

static GDataEntryIMAddress*
gdata_entry_im_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryIMAddress *im = NULL;
    GList *values;
    const gchar *name;

    name = e_vcard_attribute_get_name (attr);

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        GList *types;
        const gchar *label;
        gboolean primary;

        types = get_google_primary_type_label (attr, &primary, &label);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        im = g_new0 (GDataEntryIMAddress, 1);
        im->address = g_strdup (values->data);
        im->rel = google_rel_from_types (types);
        im->label = g_strdup (label);
        im->primary = primary;
        im->protocol = google_im_protocol_from_field_name (name);
        __debug__ ("New %s%s entry %s (%s/%s)",
                    im->primary ? "primary " : "",
                    im->protocol,
                    im->address,
                    im->rel,
                    im->label);
    }

    return im;
}

static GDataEntryPhoneNumber*
gdata_entry_phone_number_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryPhoneNumber *number = NULL;
    GList *values;

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        GList *types;
        gboolean primary;
        const gchar *label;

        types = get_google_primary_type_label (attr, &primary, &label);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        number = g_new0 (GDataEntryPhoneNumber, 1);
        number->number = g_strdup (values->data);
        number->rel = google_rel_from_types_phone (types);
        number->label = g_strdup (label);
        number->primary = primary;
        __debug__ ("New %sphone-number entry %s (%s/%s)",
                    number->primary ? "primary " : "",
                    number->number,
                    number->rel,
                    number->label);
    }

    return number;
}

static GDataEntryPostalAddress*
gdata_entry_postal_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryPostalAddress *address = NULL;
    GList *values;

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        GList *types;
        const gchar *label;
        gboolean primary;

        types = get_google_primary_type_label (attr, &primary, &label);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        address = g_new0 (GDataEntryPostalAddress, 1);
        address->address = g_strdup (values->data);
        address->rel = google_rel_from_types (types);
        address->label = g_strdup (label);
        address->primary = primary;
        __debug__ ("New %spostal address entry %s (%s/%s)",
                    address->primary ? "primary " : "",
                    address->address,
                    address->rel,
                    address->label);
    }

    return address;
}

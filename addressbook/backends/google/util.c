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

#define GOOGLE_PRIMARY_PARAM "X-GOOGLE-PRIMARY"

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

#if 0
static GList*
name_values_from_fullname   (const char *fullname);

static char*
fullname_from_name_values   (GList      *values);
#endif

static gboolean
is_known_google_im_protocol (const char *protocol);

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
    char *fullname = NULL;
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
        const char *name;

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
        if (g_ascii_strncasecmp (name, "X-", 2) &&
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
            if (values && values->data && ((char*)values->data)[0]) {
                __debug__ ("unsupported vcard field: %s: %s", name, (char*)values->data);
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
    const char *name;
    const char *uid;
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
        e_contact_set (E_CONTACT (vcard), E_CONTACT_FULL_NAME, (const gpointer)name);
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

#if 0
static GList*
name_values_from_fullname (const char *fullname)
{
    const char *comma = NULL;
    char **names;
    GList *name_values = NULL;

    if (NULL == fullname)
        return NULL;

    comma = strstr (fullname, ",");
    names = g_strsplit_set (fullname, ", ", 3);
    if (NULL == names[0]) {
        goto out;
    }
    /* Homer */
    if (NULL == names[1]) {
        name_values = g_list_append (NULL, names[0]);
        goto out;
    }
    if (comma) {
    /* Simpson, Homer */
        name_values = g_list_append (NULL, names[0]);
        name_values = g_list_append (name_values, names[1]);
    /* Simpson, Homer J.*/
        if (names[2]) {
            name_values = g_list_append (name_values, names[2]);
        }
    } else {
    /* Homer J. Simpson */    
        if (names[2]) {
            name_values = g_list_append (NULL, names[2]);
            name_values = g_list_append (name_values, names[0]);
            name_values = g_list_append (name_values, names[1]);
        }
    /* Homer Simpson */    
        else {
            name_values = g_list_append (NULL, names[1]);
            name_values = g_list_append (name_values, names[0]);
        }
    }

out:
    g_free (names);

    return name_values;
}
#endif

#if 0
static GString*
string_prepend_with_space (GString *string, const char *val)
{
    if (NULL == val ||
        0 == val[0])
        return string;
    g_string_prepend (string, " ");
    g_string_prepend (string, val);

    return string;
}

static GString*
string_append_with_space (GString *string, const char *val)
{
    if (NULL == val ||
        0 == val[0])
        return string;
    string = g_string_append (string, " ");
    string = g_string_append (string, val);

    return string;
}
#endif

#if 0
static char*
fullname_from_name_values (GList *values)
{
    GString *name;
    const char *givenname;

    if (NULL == values ||
        NULL == values->data)
        return NULL;

    /* Family Name */
    name = g_string_new (values->data);

    values = values->next;
    if (NULL == values)
        goto out;

    /* Given Name */
    givenname = values->data;

    values = values->next;
    if (NULL == values) {
        string_prepend_with_space (name, givenname);
        goto out;
    }

    /* Additional Names */
    string_prepend_with_space (name, values->data);
    string_prepend_with_space (name, givenname);

    values = values->next;
    if (NULL == values)
        goto out;

    /* TODO: Honoric stuff should go into ORG? */
#if 0
    /* Honorific Prefixes */
    string_prepend_with_space (name, values->data);

    values = values->next;
    if (NULL == values)
        goto out;

    /* Honorific Suffixes */
    string_append_with_space (name, values->data);
#endif

out:
    return g_string_free (name, FALSE);
}
#endif

#define GDATA_ENTRY_XML_ATTR "X-GDATA-ENTRY-XML"

void
_e_contact_add_gdata_entry_xml (EContact *contact, GDataEntry *entry)
{
    EVCardAttribute *attr;
    const char* entry_xml;

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

const char*
_e_contact_get_gdata_entry_xml (EContact *contact)
{
    EVCardAttribute *attr;
    GList *values;

    attr = e_vcard_get_attribute (E_VCARD (contact), GDATA_ENTRY_XML_ATTR);
    values = e_vcard_attribute_get_values (attr);

    return values ? values->data : NULL;
}

#if 0
gboolean test_repository_availability (void)
{
    SoupSession *session;
    SoupMessage *message;
    int http_status;
    const char *uri = "http://www.google.com/m8/feeds/contacts";

    session = soup_session_sync_new ();
    message = soup_message_new (SOUP_METHOD_GET, uri);

    http_status = soup_session_send_message (session, message);
    __debug__ ("%s: HTTP %d (%s)",
               G_STRFUNC,
               http_status,
               soup_status_get_phrase (http_status));
    g_object_unref (message);
    g_object_unref (session);

    /* Everything below 100 means we can't reach www.google.com */
    return (http_status >= 100);
}
#endif

static char*
type_from_google_rel_label (const char* rel, const char *label)
{
    if (rel) {
        char *type;
        type = g_strrstr (rel, "#");
        if (NULL == type)
            return NULL;
        return g_ascii_strup (type + 1, -1);
    }
    if (label) {
        return g_strdup_printf ("X-%s", label);
    }
    return NULL;
}

static void
google_rel_label_from_type (const char* type, char **rel, char **label)
{
    const char *format = "http://schemas.google.com/g/2005#%s";

    *label = NULL;
    *rel = NULL;
    if (NULL == type) {
        *rel = g_strdup_printf (format, "other");
        return;
    }

    if (0 == strcmp (type, "WORK")) {
        *rel = g_strdup_printf (format, "work");
        return;
    }
    if (0 == strcmp (type, "HOME")) {
        *rel = g_strdup_printf (format, "home");
        return;
    }

    if (0 == strncmp (type, "X-", 2)) {
        *label = g_strdup (type + 2);
        return;
    }

    *rel = g_strdup_printf (format, "other");
}

static gboolean
is_known_google_im_protocol (const char *protocol)
{
    const char *known_protocols[] =
    {
        "AIM", "MSN", "YAHOO",     "SKYPE", "QQ",
        "GOOGLE_TALK", "ICQ", "JABBER"
    };
    int i;

    if (NULL == protocol)
        return FALSE;
    
    for (i = 0; i < G_N_ELEMENTS (known_protocols); i++) {
        if (0 == strcmp (known_protocols[i], protocol))
            return TRUE;
    }
    return FALSE;
}

static char*
field_name_from_google_im_protocol (const char* google_protocol)
{
    char *protocol;
    if (NULL == google_protocol)
        return NULL;

    protocol = g_strrstr (google_protocol, "#");
    if (NULL == protocol)
        return NULL;
    return g_strdup_printf ("X-%s", protocol + 1);
}

static char*
google_im_protocol_from_field_name (const char* field_name)
{
    const char *format = "http://schemas.google.com/g/2005#%s";

    if (NULL == field_name ||
        strlen (field_name) < 3) {
        return NULL;
    }

    return g_strdup_printf (format, field_name + 2);
}

static EVCardAttribute*
attribute_from_gdata_entry_email_address (GDataEntryEmailAddress *email)
{
    EVCardAttribute *attr;
    EVCardAttributeParam *param;
    char *type;

    if (NULL == email || NULL == email->address)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
    type = type_from_google_rel_label (email->rel, email->label);
    if (email->primary) {
        param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
        e_vcard_attribute_add_param (attr, param);
        if (NULL == type) {
            param = e_vcard_attribute_param_new ("TYPE");
            e_vcard_attribute_add_param_with_value (attr, param, "PREF");
        }
    }
    if (type) {
        param = e_vcard_attribute_param_new ("TYPE");
        e_vcard_attribute_add_param_with_value (attr, param, type);
        g_free (type);
    }
    e_vcard_attribute_add_value (attr, email->address);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_im_address (GDataEntryIMAddress *im)
{
    EVCardAttribute *attr;
    EVCardAttributeParam *param;
    char *type;
    char *field_name;

    if (NULL == im || NULL == im->address)
        return NULL;;

    field_name = field_name_from_google_im_protocol (im->protocol);
    if (NULL == field_name)
        return NULL;

    attr = e_vcard_attribute_new (NULL, field_name);
    type = type_from_google_rel_label (im->rel, im->label);
    if (im->primary) {
        param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
        e_vcard_attribute_add_param (attr, param);
        if (NULL == type) {
            param = e_vcard_attribute_param_new ("TYPE");
            e_vcard_attribute_add_param_with_value (attr, param, "PREF");
        }
    }
    if (type) {
        param = e_vcard_attribute_param_new ("TYPE");
        e_vcard_attribute_add_param_with_value (attr, param, type);
        g_free (type);
    }
    e_vcard_attribute_add_value (attr, im->address);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_phone_number (GDataEntryPhoneNumber *number)
{
    EVCardAttribute *attr;
    EVCardAttributeParam *param;
    char *type;

    if (NULL == number || NULL == number->number)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_TEL);
    /* TODO: This needs more work */
    type = type_from_google_rel_label (number->rel, number->label);
    if (number->primary) {
        param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
        e_vcard_attribute_add_param (attr, param);
        if (NULL == type) {
            param = e_vcard_attribute_param_new ("TYPE");
            e_vcard_attribute_add_param_with_value (attr, param, "PREF");
        }
    }
    if (type) {
        param = e_vcard_attribute_param_new ("TYPE");
        e_vcard_attribute_add_param_with_value (attr, param, type);
        g_free (type);
    }
    e_vcard_attribute_add_value (attr, number->number);
    return attr;
}

static EVCardAttribute*
attribute_from_gdata_entry_postal_address (GDataEntryPostalAddress *address)
{
    EVCardAttribute *attr;
    EVCardAttributeParam *param;
    char *type;

    if (NULL == address || NULL == address->address)
        return NULL;;

    attr = e_vcard_attribute_new (NULL, EVC_LABEL);
    /* TODO: This needs more work */
    type = type_from_google_rel_label (address->rel, address->label);
    if (address->primary) {
        param = e_vcard_attribute_param_new (GOOGLE_PRIMARY_PARAM);
        e_vcard_attribute_add_param (attr, param);
        if (NULL == type) {
            param = e_vcard_attribute_param_new ("TYPE");
            e_vcard_attribute_add_param_with_value (attr, param, "PREF");
        }
    }
    if (type) {
        param = e_vcard_attribute_param_new ("TYPE");
        e_vcard_attribute_add_param_with_value (attr, param, type);
        g_free (type);
    }
    e_vcard_attribute_add_value (attr, address->address);
    return attr;
}

static void
get_google_primary_and_type (EVCardAttribute *attr, gboolean *primary, const char **type)
{
    GList *params;

    *primary = FALSE;
    *type = NULL;
    params = e_vcard_attribute_get_params (attr);
    while (params) {
        const char *name;

        name = e_vcard_attribute_get_name (params->data);
        if (0 == g_ascii_strcasecmp (name, GOOGLE_PRIMARY_PARAM)) {
            *primary = TRUE;
        }
        if (0 == g_ascii_strcasecmp (name, "TYPE")) {
            GList *values;

            values = e_vcard_attribute_param_get_values (params->data);
            *type = values ? values->data : NULL;
        }
        params = params->next;
    }
}

static GDataEntryEmailAddress*
gdata_entry_email_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryEmailAddress *email = NULL;
    GList *values;

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        const char *type;
        gboolean primary;

        get_google_primary_and_type (attr, &primary, &type);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        email = g_new0 (GDataEntryEmailAddress, 1);
        email->address = g_strdup (values->data);
        google_rel_label_from_type (type, &email->rel, &email->label);
        email->primary = primary;
    }

    return email;
}

static GDataEntryIMAddress*
gdata_entry_im_address_from_attribute (EVCardAttribute *attr, gboolean *have_primary)
{
    GDataEntryIMAddress *im = NULL;
    GList *values;
    const char *name;

    name = e_vcard_attribute_get_name (attr);

    values = e_vcard_attribute_get_values (attr);
    if (values) {
        const char *type;
        gboolean primary;

        get_google_primary_and_type (attr, &primary, &type);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        im = g_new0 (GDataEntryIMAddress, 1);
        im->address = g_strdup (values->data);
        google_rel_label_from_type (type, &im->rel, &im->label);
        im->primary = primary;
        im->protocol = google_im_protocol_from_field_name (name);
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
        const char *type;
        gboolean primary;

        get_google_primary_and_type (attr, &primary, &type);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        number = g_new0 (GDataEntryPhoneNumber, 1);
        number->number = g_strdup (values->data);
        /* TODO: this needs more work */
        google_rel_label_from_type (type, &number->rel, &number->label);
        number->primary = primary;
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
        const char *type;
        gboolean primary;

        get_google_primary_and_type (attr, &primary, &type);
        if (FALSE == *have_primary) {
            *have_primary = primary;
        } else {
            primary = FALSE;
        }

        address = g_new0 (GDataEntryPostalAddress, 1);
        address->address = g_strdup (values->data);
        google_rel_label_from_type (type, &address->rel, &address->label);
        address->primary = primary;
    }

    return address;
}

/* e-book-backend-google.c - Google contact backendy.
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

#include <config.h>
#include <string.h>

#include <libebook/e-contact.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>

#include <gdata-service-iface.h>
#include <gdata-google-service.h>

#include "e-book-backend-google.h"
#include "util.h"

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_BACKEND_SYNC);

struct _EBookBackendGooglePrivate
{
    gint mode;
    char *base_uri;

    GDataGoogleService *service;
    GHashTable *gdata_entries;

    guint refresh_interval;
    guint refresh_feed_id;
    char *feed_last_updated;

    gboolean authorized;

    GList *bookviews;
    GList *pending_auth_bookviews;
};

#define GET_PRIVATE(obj)      \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj),  \
     E_TYPE_BOOK_BACKEND_GOOGLE,  \
     EBookBackendGooglePrivate))

gboolean __e_book_backend_google_debug__;

static EBookBackendSyncStatus
e_book_backend_google_initial_query (EBookBackendGoogle *backend);

static EBookBackendSyncStatus
e_book_backend_google_update_query  (EBookBackendGoogle *backend,
                                     GList             **added,
                                     GList             **modified,
                                     GList             **deleted);

static EBookBackendSyncStatus
ebookbackend_status_from_soup_error (int http_error);

static gboolean
e_book_backend_google_ensure_auth (EBookBackendGoogle *backend)
{
    EBookBackendGooglePrivate *priv;
    gboolean is_authorized;

    priv = GET_PRIVATE (backend);

    is_authorized = (priv->authorized && priv->base_uri && priv->service);

    if (FALSE == is_authorized) {
        e_book_backend_notify_auth_required (E_BOOK_BACKEND (backend));
        __debug__ ("auth required");
    }

    return is_authorized;
}

static EBookBackendSyncStatus
e_book_backend_google_create_contact (EBookBackendSync *backend,
                                      EDataBook        *book,
                                      guint32           opid,
                                      const char       *vcard_str,
                                      EContact        **contact)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    GDataEntry *entry, *new_entry;
    GError *error = NULL;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    __debug__ ("Creating: %s", vcard_str);
    *contact = NULL;

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }

    if (FALSE == e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend))) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    }

    entry = gdata_entry_create_from_vcard (vcard_str);
    __debug__ ("new entry with xml: %s", gdata_entry_generate_xml (entry));
    new_entry = gdata_service_insert_entry (GDATA_SERVICE (priv->service),
                                            priv->base_uri, entry, &error);
    g_object_unref (entry);

    if (new_entry) {
        const char *uid;

        uid = gdata_entry_get_id (new_entry);
        *contact = e_contact_from_gdata_entry (new_entry);
        g_hash_table_insert (priv->gdata_entries, g_strdup (uid), new_entry);
    }
    if (error) {
        status = ebookbackend_status_from_soup_error (error->code);
        __debug__ ("Creating contact failed (HTTP %d): %s", error->code, error->message);
        g_clear_error (&error);
    }

    if (NULL == *contact) {
        return status;
    }
    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_remove_contacts (EBookBackendSync *backend,
                                       EDataBook        *book,
                                       guint32           opid,
                                       GList            *id_list,
                                       GList           **ids)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    GList *id_iter;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    *ids = NULL;

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }

    if (FALSE == e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend))) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    }

    for (id_iter = id_list; id_iter; id_iter = id_iter->next) {
        GDataEntry *entry;
        GError *error = NULL;
        char *uid;

        uid = id_iter->data;
        entry = g_hash_table_lookup (priv->gdata_entries, uid);
        if (NULL == entry) {
            continue;
        }

        if (gdata_service_delete_entry (GDATA_SERVICE (priv->service), entry, &error)) {
            g_hash_table_remove (priv->gdata_entries, uid);
            *ids = g_list_append (*ids, g_strdup (uid));
        }
        if (error) {
            /* Only last error will be reported */
            status = ebookbackend_status_from_soup_error (error->code);
            __debug__ ("Deleting contact %s failed (HTTP %d): %s", uid, error->code, error->message);
            g_clear_error (&error);
        }
    }

    if (NULL == *ids) {
        return status;
    }
    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_modify_contact (EBookBackendSync *backend,
                                      EDataBook        *book,
                                      guint32           opid,
                                      const char       *vcard_str,
                                      EContact        **contact)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    EVCardAttribute *uid_attr;
    EVCard *vcard;
    GDataEntry *entry, *copy, *new_entry;
    GError *error = NULL;
    char *uid, *xml;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    __debug__ ("Updating: %s", vcard_str);
    *contact = NULL;

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }

    if (FALSE == e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend))) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    }

    vcard = e_vcard_new_from_string (vcard_str);
    if (NULL == vcard)
        return GNOME_Evolution_Addressbook_OtherError;

    uid_attr = e_vcard_get_attribute (vcard, EVC_UID);
    if (NULL == uid_attr)
        return GNOME_Evolution_Addressbook_ContactNotFound;

    uid = e_vcard_attribute_get_value (uid_attr);

    entry = g_hash_table_lookup (priv->gdata_entries, uid ? uid : "");

    if (NULL == entry)
        return GNOME_Evolution_Addressbook_ContactNotFound;

    xml = gdata_entry_generate_xml (entry);
    /* We operate on a copy; if we cannot commit our changes we should also
     * leave the local entry as it was.
     * There is a warning from libxml2 as result form this, because libgdata
     * caches the xml from which it was created. However, if it is created
     * from a feed, this xml will miss all namespace information, thus the warning */
    copy = gdata_entry_new_from_xml (xml);

    gdata_entry_update_from_e_vcard (copy, vcard);
    new_entry = gdata_service_update_entry (GDATA_SERVICE (priv->service), copy, &error);

    g_object_unref (copy);
    g_object_unref (vcard);

    if (new_entry) {
        *contact = e_contact_from_gdata_entry (new_entry);
        /* Will unref/free the old entry */
        g_hash_table_replace (priv->gdata_entries, uid, new_entry);
    }
    if (error) {
        status = ebookbackend_status_from_soup_error (error->code);
        __debug__ ("Updating contact failed (HTTP %d): %s", error->code, error->message);
        g_clear_error (&error);
    }

    if (NULL == *contact) {
        return status;
    }
    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_get_contact (EBookBackendSync *backend,
                                   EDataBook        *book,
                                   guint32           opid,
                                   const char       *uid,
                                   char            **vcard_str)
{
    EBookBackendGooglePrivate *priv;
    GDataEntry *entry;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    if (FALSE == e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend))) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    }

    entry = g_hash_table_lookup (priv->gdata_entries, uid);

    if (NULL == entry) {
        *vcard_str = NULL;
        return GNOME_Evolution_Addressbook_ContactNotFound;
    }

    *vcard_str = vcard_from_gdata_entry (GDATA_ENTRY (entry));

    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_get_contact_list (EBookBackendSync *backend,
                                        EDataBook        *book,
                                        guint32           opid,
                                        const char       *query,
                                        GList           **contacts)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSExp *sexp;
    GHashTableIter iter;
    GDataEntry *entry;
    char *uid;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    *contacts = NULL;
    if (FALSE == e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend))) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    }

    sexp = e_book_backend_sexp_new (query);

    g_hash_table_iter_init (&iter, priv->gdata_entries);
    while (g_hash_table_iter_next (&iter, (gpointer)&uid, (gpointer)&entry)) {
        char *vcard_str;

        vcard_str = vcard_from_gdata_entry (GDATA_ENTRY (entry));
        if (vcard_str && e_book_backend_sexp_match_vcard (sexp, vcard_str)) {
            *contacts = g_list_append (*contacts, vcard_str);
        } else {
            g_free (vcard_str);
        }
    }
    g_object_unref (sexp);


    return GNOME_Evolution_Addressbook_Success;
}

static gboolean
refresh_feed (gpointer data)
{
    EBookBackendGooglePrivate *priv;
    GList *added, *modified, *deleted;
    GList *iter;

    g_return_val_if_fail (E_IS_BOOK_BACKEND_GOOGLE (data), FALSE);

    priv = GET_PRIVATE (data);

    if (NULL == priv->bookviews) {
        priv->refresh_feed_id = 0;

        return FALSE;
    }
    e_book_backend_google_update_query (E_BOOK_BACKEND_GOOGLE (data),
                                        &added, &modified, &deleted);

    if (FALSE == (added || modified || deleted))
        return TRUE;

    modified = g_list_concat (added, modified);
    while (modified) {
        char *vcard_str;

        vcard_str = vcard_from_gdata_entry (GDATA_ENTRY (modified->data));
        __debug__ ("Update or new entry: %s", vcard_str);
        if (vcard_str) {
            for (iter = priv->bookviews; iter; iter = iter->next) {
                e_data_book_view_notify_update_vcard (iter->data, g_strdup (vcard_str));
            }
            g_free (vcard_str);
        }

        g_object_unref (modified->data);
        modified = g_list_delete_link (modified, modified);
    }

    while (deleted) {
        const char *uid;

        uid = gdata_entry_get_id (GDATA_ENTRY (deleted->data));
        __debug__ ("Deleted entry: %s", uid);
        if (uid) {
            for (iter = priv->bookviews; iter; iter = iter->next) {
                e_data_book_view_notify_remove (iter->data, uid);
            }
        }

        g_object_unref (deleted->data);
        deleted = g_list_delete_link (deleted, deleted);
    }

    for (iter = priv->bookviews; iter; iter = iter->next) {
        e_data_book_view_notify_complete (iter->data, GNOME_Evolution_Addressbook_Success);
    }

    return TRUE;
}

static void
do_initial_bookview_population (EBookBackendGoogle *backend,
                                EDataBookView      *bookview)
{
    EBookBackendGooglePrivate *priv;
    GHashTableIter iter;
    GDataEntry *entry;
    char *uid;

    priv = GET_PRIVATE (backend);

    if (NULL == priv->gdata_entries)
        return;

    g_hash_table_iter_init (&iter, priv->gdata_entries);
    while (g_hash_table_iter_next (&iter, (gpointer)&uid, (gpointer)&entry)) {
        char *vcard_str;

        vcard_str = vcard_from_gdata_entry (GDATA_ENTRY (entry));
        //__debug__ ("%s", vcard_str);
        e_data_book_view_notify_update_vcard (bookview, vcard_str);

    }

    e_data_book_view_notify_complete (bookview, GNOME_Evolution_Addressbook_Success);

    if ((0 == priv->refresh_feed_id) && priv->refresh_interval) {
        priv->refresh_feed_id = g_timeout_add_seconds (priv->refresh_interval,
                                                       refresh_feed,
                                                       backend);
    }
}

static void
e_book_backend_google_start_book_view (EBookBackend  *backend,
                                       EDataBookView *bookview)
{
    EBookBackendGooglePrivate *priv;
    gboolean have_auth;

    g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend));
    g_return_if_fail (E_IS_DATA_BOOK_VIEW (bookview));

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    priv->bookviews = g_list_append (priv->bookviews, bookview);

    bonobo_object_ref (bookview);
    e_data_book_view_notify_status_message (bookview, "Loading...");

    have_auth = e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend));
    if (have_auth) {
        do_initial_bookview_population (E_BOOK_BACKEND_GOOGLE (backend),
                                        bookview);
    } else {
        priv->pending_auth_bookviews = g_list_append (priv->pending_auth_bookviews,
                                                      bookview);
    }
}

static void
e_book_backend_google_stop_book_view (EBookBackend  *backend,
                                      EDataBookView *bookview)
{
    EBookBackendGooglePrivate *priv;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    priv->bookviews = g_list_remove (priv->bookviews, bookview);
    priv->pending_auth_bookviews = g_list_remove (priv->pending_auth_bookviews, bookview);
    bonobo_object_unref (bookview);

    if ((NULL == priv->bookviews) && priv->refresh_feed_id) {
        g_source_remove (priv->refresh_feed_id);
        priv->refresh_feed_id = 0;
    }
}

static EBookBackendSyncStatus
e_book_backend_google_update_query  (EBookBackendGoogle *backend,
                                     GList             **added,
                                     GList             **modified,
                                     GList             **deleted)
{
    EBookBackendGooglePrivate *priv;
    GDataFeed *feed;
    GSList *iter, *entries;
    GError *error = NULL;
    char *uri, *updated_min = NULL;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    *added = *modified = *deleted = NULL;

    if (NULL == priv->service ||
        NULL == priv->base_uri) {
        return GNOME_Evolution_Addressbook_AuthenticationFailed;
    }

    if (priv->feed_last_updated) {
        updated_min = g_strdup_printf ("updated-min=%s", priv->feed_last_updated);
    }

    uri = build_uri (priv->base_uri, "max-results=100", "showdeleted=true", updated_min,  NULL);
    __debug__ ("URI is '%s'", uri);
    feed = gdata_service_get_feed (GDATA_SERVICE (priv->service), uri, &error);
    g_free (uri);

    if (error) {
        EBookBackendSyncStatus status;

        __debug__ ("Update query failed (HTTP %d): %s", error->code, error->message);
        status = ebookbackend_status_from_soup_error (error->code);
        g_clear_error (&error);

        return status;
    }
    if (NULL == feed) {
        return GNOME_Evolution_Addressbook_OtherError;
    }

    g_free (priv->feed_last_updated);
    priv->feed_last_updated = g_strdup (gdata_feed_get_updated (GDATA_FEED (feed)));

    entries = gdata_feed_get_entries (feed);
    __debug__ ("Update-feed has %d entries", entries ? g_slist_length (entries) : -1);

    for (iter = entries; iter; iter = iter->next) {
        GDataEntry *entry, *old_entry;
        const char *edit_link, *old_edit_link;
        gboolean is_deleted;
        const char *uid;

        entry = GDATA_ENTRY (iter->data);
        uid = gdata_entry_get_id (entry);
        is_deleted = gdata_entry_is_deleted (entry);

        old_entry = g_hash_table_lookup (priv->gdata_entries, uid);
        if (is_deleted) {
            /* Do we have this item in our list? */
            if (NULL == old_entry) {
                continue;
            } else {
                *deleted = g_list_append (*deleted, g_object_ref (entry));
                g_hash_table_remove (priv->gdata_entries, uid);
            }
        } else {
            /* Is this a new entry */
            if (NULL == old_entry) {
                *added = g_list_append (*added, g_object_ref (entry));
                g_hash_table_insert (priv->gdata_entries,
                                     g_strdup (uid),
                                     g_object_ref (entry));
            }
        }

        edit_link = gdata_entry_get_edit_link (entry);
        old_edit_link = gdata_entry_get_edit_link (old_entry);
        if (0 == strcmp (edit_link ? edit_link : "", old_edit_link ? old_edit_link : ""))
            continue;

        g_hash_table_replace (priv->gdata_entries, g_strdup (uid), g_object_ref (entry));
        *modified = g_list_append (*modified, g_object_ref (entry));
    }
    g_object_unref (feed);

    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_initial_query (EBookBackendGoogle *backend)
{
    EBookBackendGooglePrivate *priv;
    GDataFeed *feed;
    GSList *iter, *entries;
    GError *error = NULL;
    char *uri;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    if (NULL == priv->service ||
        NULL == priv->base_uri) {
        return GNOME_Evolution_Addressbook_AuthenticationFailed;
    }

    uri = build_uri (priv->base_uri, "max-results=100", NULL);
    __debug__ ("URI is '%s'", uri);
    feed = gdata_service_get_feed (GDATA_SERVICE (priv->service), uri, &error);
    g_free (uri);

    if (error) {
        EBookBackendSyncStatus status;

        __debug__ ("Initial query failed (HTTP %d): %s", error->code, error->message);
        status = ebookbackend_status_from_soup_error (error->code);
        g_clear_error (&error);

        return status;
    }
    if (NULL == feed) {
        return GNOME_Evolution_Addressbook_OtherError;
    }

    g_free (priv->feed_last_updated);
    priv->feed_last_updated = g_strdup (gdata_feed_get_updated (GDATA_FEED (feed)));
    if (priv->gdata_entries) {
        g_hash_table_destroy (priv->gdata_entries);
    }
    priv->gdata_entries = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 g_object_unref);

    entries = gdata_feed_get_entries (feed);
    __debug__ ("Feed has %d entries", entries ? g_slist_length (entries) : -1);
    for (iter = entries; iter; iter = iter->next) {
        GDataEntry *entry;
        const char* uid;

        entry = GDATA_ENTRY (iter->data);
        uid = gdata_entry_get_id (entry);
        g_hash_table_insert (priv->gdata_entries, g_strdup (uid), g_object_ref (entry));
    }
    g_object_unref (feed);

    return GNOME_Evolution_Addressbook_Success;
}


static EBookBackendSyncStatus
e_book_backend_google_authenticate_user (EBookBackendSync *backend,
                                         EDataBook        *book,
                                         guint32           opid,
                                         const char       *username,
                                         const char       *password,
                                         const char       *auth_method)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        g_warning ("Offline mode not mplemented...");
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }
    if (NULL == priv->service) {
        g_warning ("Book not open; could not authenticate");
        return GNOME_Evolution_Addressbook_RepositoryOffline;
    }

    g_free (priv->base_uri);
    priv->base_uri = NULL;

    if (NULL == username || username[0] == 0 ||
        NULL == password || password[0] == 0) {
        return GNOME_Evolution_Addressbook_AuthenticationFailed;
    }

    priv->base_uri = build_base_uri (username);
    gdata_service_set_credentials (GDATA_SERVICE (priv->service), username, password);

    status = e_book_backend_google_initial_query (E_BOOK_BACKEND_GOOGLE (backend));
    if (status == GNOME_Evolution_Addressbook_Success) {
        GList *iter;

        priv->authorized = TRUE;
        e_book_backend_notify_writable (E_BOOK_BACKEND (backend), TRUE);

        for (iter = priv->pending_auth_bookviews; iter; iter = iter->next) {
            do_initial_bookview_population (E_BOOK_BACKEND_GOOGLE (backend),
                                            iter->data);
        }
    } else {
        g_free (priv->base_uri);
        priv->base_uri = NULL;
        priv->authorized = FALSE;
    }

    return status;
}

static EBookBackendSyncStatus
e_book_backend_google_get_supported_auth_methods (EBookBackendSync *backend,
                                                  EDataBook        *book,
                                                  guint32           opid,
                                                  GList           **methods)
{
    char *auth_method;

    __debug__ (G_STRFUNC);
    auth_method = g_strdup_printf ("plain/password");
    *methods = g_list_append (NULL, auth_method);

    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_get_required_fields (EBookBackendSync *backend,
                                           EDataBook        *book,
                                           guint32           opid,
                                           GList           **fields_out)
{
    __debug__ (G_STRFUNC);

    *fields_out = NULL;
    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_get_supported_fields (EBookBackendSync *backend,
                                            EDataBook        *book,
                                            guint32           opid,
                                            GList           **fields_out)
{
    const int supported_fields[] =
    {
        E_CONTACT_FULL_NAME,
        E_CONTACT_EMAIL_1,
        E_CONTACT_EMAIL_2,
        E_CONTACT_EMAIL_3,
        E_CONTACT_EMAIL_4,
        E_CONTACT_ADDRESS_LABEL_HOME,
        E_CONTACT_ADDRESS_LABEL_WORK,
        E_CONTACT_ADDRESS_LABEL_OTHER,
        E_CONTACT_PHONE_HOME,
        E_CONTACT_PHONE_HOME_FAX,
        E_CONTACT_PHONE_BUSINESS,
        E_CONTACT_PHONE_BUSINESS_FAX,
        E_CONTACT_PHONE_MOBILE,
        E_CONTACT_PHONE_PAGER,
        E_CONTACT_IM_AIM,
        E_CONTACT_IM_JABBER,
        E_CONTACT_IM_YAHOO,
        E_CONTACT_IM_MSN,
        E_CONTACT_IM_ICQ,
        E_CONTACT_ADDRESS,
        E_CONTACT_ADDRESS_HOME,
        E_CONTACT_ADDRESS_WORK,
        E_CONTACT_ADDRESS_OTHER
    };
    GList *fields = NULL;
    int i;

    __debug__ (G_STRFUNC);

    for (i = 0; i < G_N_ELEMENTS (supported_fields); i++) {
        const char *field_name;

        field_name = e_contact_field_name (supported_fields[i]);
        fields = g_list_append (fields, g_strdup (field_name));
    }

    *fields_out = fields;
    return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_google_get_changes (EBookBackendSync *backend,
                                   EDataBook        *book,
                                   guint32           opid,
                                   const char       *change_id,
                                   GList           **changes_out)
{
    __debug__ (G_STRFUNC);
    return GNOME_Evolution_Addressbook_OtherError;
}

static EBookBackendSyncStatus
e_book_backend_google_remove (EBookBackendSync *backend,
                              EDataBook        *book,
                              guint32           opid)
{
    __debug__ (G_STRFUNC);
    return GNOME_Evolution_Addressbook_PermissionDenied;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_google_load_source (EBookBackend *backend,
                                   ESource      *source,
                                   gboolean      only_if_exists)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (backend);
    const char *refresh_interval;

    if (priv->service) {
        g_warning ("Source already loaded!");
        return GNOME_Evolution_Addressbook_OtherError;
    }

    refresh_interval = e_source_get_property (source, "refresh-interval");
    if (refresh_interval) {
        guint val;

        if (1 == sscanf (refresh_interval, "%u", &val)) {
            priv->refresh_interval = val;
        }
    }

    __debug__ (G_STRFUNC);
    if (priv->mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
        gboolean status;

        status = test_repository_availability ();
        if (FALSE == status) {
            e_book_backend_notify_connection_status (backend, FALSE);

            return GNOME_Evolution_Addressbook_RepositoryOffline;
        }

        priv->service = gdata_google_service_new ("cp", "evolution-client-0.0.1");

        e_book_backend_set_is_loaded (backend, TRUE);
        e_book_backend_notify_connection_status (backend, TRUE);

        return GNOME_Evolution_Addressbook_Success;
    } else {
        g_warning ("Offline mode not yet implemented...");
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }
}

static char *
e_book_backend_google_get_static_capabilities (EBookBackend *backend)
{
    __debug__ (G_STRFUNC);
    return g_strdup("net,do-initial-query,contact-lists");
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_google_cancel_operation (EBookBackend *backend, EDataBook *book)
{
    __debug__ (G_STRFUNC);
    return GNOME_Evolution_Addressbook_CouldNotCancel;
}

static void
e_book_backend_google_set_mode (EBookBackend *backend, GNOME_Evolution_Addressbook_BookMode mode)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (backend);

    __debug__ (G_STRFUNC);

    if (mode == priv->mode) {
        return;
    }
    priv->mode = mode;

    if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
        if (e_book_backend_is_loaded (backend)) {
            gboolean status;

            status = test_repository_availability ();

            if (FALSE == status) {
                e_book_backend_notify_writable (backend, FALSE);
                e_book_backend_notify_connection_status (backend, FALSE);
            } else {
                status = e_book_backend_google_ensure_auth (E_BOOK_BACKEND_GOOGLE (backend));
                e_book_backend_notify_writable (backend, status);
                e_book_backend_notify_connection_status (backend, TRUE);
            }
        } else {
            e_book_backend_set_is_writable (backend, FALSE);
        }
    } else {
        e_book_backend_notify_writable (backend, FALSE);
        e_book_backend_notify_connection_status (backend, FALSE);
        g_warning ("Offline mode not implemented yet...");
    }
}

static void
e_book_backend_google_dispose (GObject *object)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (object);

    __debug__ (G_STRFUNC);

    if (priv->refresh_feed_id) {
        g_source_remove (priv->refresh_feed_id);
        priv->refresh_feed_id = 0;
    }

    while (priv->pending_auth_bookviews) {
        priv->pending_auth_bookviews =
                g_list_delete_link (priv->pending_auth_bookviews,
                                    priv->pending_auth_bookviews);
    }
    while (priv->bookviews) {
        bonobo_object_unref (priv->bookviews->data);
        priv->bookviews = g_list_delete_link (priv->bookviews,
                                              priv->bookviews);
    }
    if (priv->service) {
        g_object_unref (priv->service);
        priv->service = NULL;
    }

    G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
e_book_backend_google_finalize (GObject *object)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (object);

    __debug__ (G_STRFUNC);

    g_free (priv->base_uri);
    g_free (priv->feed_last_updated);
    if (priv->gdata_entries) {
        g_hash_table_destroy (priv->gdata_entries);
    }
    G_OBJECT_CLASS (e_book_backend_google_parent_class)->finalize (object);
}

static void
e_book_backend_google_class_init (EBookBackendGoogleClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    EBookBackendClass *backend_class;
    EBookBackendSyncClass *sync_class;

    backend_class = E_BOOK_BACKEND_CLASS (klass);
    sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
    g_type_class_add_private (klass, sizeof (EBookBackendGooglePrivate));

    /* Set the virtual methods. */
    backend_class->load_source             = e_book_backend_google_load_source;
    backend_class->get_static_capabilities = e_book_backend_google_get_static_capabilities;
    backend_class->start_book_view         = e_book_backend_google_start_book_view;
    backend_class->stop_book_view          = e_book_backend_google_stop_book_view;
    backend_class->cancel_operation        = e_book_backend_google_cancel_operation;
    backend_class->set_mode                = e_book_backend_google_set_mode;
    sync_class->remove_sync                = e_book_backend_google_remove;
    sync_class->create_contact_sync        = e_book_backend_google_create_contact;
    sync_class->remove_contacts_sync       = e_book_backend_google_remove_contacts;
    sync_class->modify_contact_sync        = e_book_backend_google_modify_contact;
    sync_class->get_contact_sync           = e_book_backend_google_get_contact;
    sync_class->get_contact_list_sync      = e_book_backend_google_get_contact_list;
    sync_class->get_changes_sync           = e_book_backend_google_get_changes;
    sync_class->authenticate_user_sync     = e_book_backend_google_authenticate_user;
    sync_class->get_supported_fields_sync  = e_book_backend_google_get_supported_fields;
    sync_class->get_required_fields_sync   = e_book_backend_google_get_required_fields;
    sync_class->get_supported_auth_methods_sync  = e_book_backend_google_get_supported_auth_methods;

    object_class->dispose  = e_book_backend_google_dispose;
    object_class->finalize = e_book_backend_google_finalize;

    __e_book_backend_google_debug__ = g_getenv ("GOOGLE_BACKEND_DEBUG") ? TRUE : FALSE;
}

static void
e_book_backend_google_init (EBookBackendGoogle *backend)
{
    __debug__ (G_STRFUNC);
}

EBookBackend *
e_book_backend_google_new (void)
{
    EBookBackendGoogle *backend;

    __debug__ (G_STRFUNC);
    backend = g_object_new (E_TYPE_BOOK_BACKEND_GOOGLE, NULL);

    return E_BOOK_BACKEND (backend);
}

static EBookBackendSyncStatus
ebookbackend_status_from_soup_error (int http_error)
{
    if (http_error < 200) {
        return GNOME_Evolution_Addressbook_RepositoryOffline;
    } else
    if (http_error == 401) {
        return GNOME_Evolution_Addressbook_AuthenticationRequired;
    } else
    if (http_error == 403) {
        return GNOME_Evolution_Addressbook_AuthenticationFailed;
    } else {
        return GNOME_Evolution_Addressbook_OtherError;
    }
}


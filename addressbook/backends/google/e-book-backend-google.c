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
#include <errno.h>

#include <libebook/e-contact.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>

#include "e-book-backend-google.h"
#include "google-book.h"
#include "util.h"

G_DEFINE_TYPE (EBookBackendGoogle, e_book_backend_google, E_TYPE_BOOK_BACKEND_SYNC);

struct _EBookBackendGooglePrivate
{
    gint mode;
    GoogleBook *book;
    GList *bookviews;
};

#define GET_PRIVATE(obj)      \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj),  \
     E_TYPE_BOOK_BACKEND_GOOGLE,  \
     EBookBackendGooglePrivate))

gboolean __e_book_backend_google_debug__;

static EBookBackendSyncStatus e_book_backend_status_from_google_book_error (GoogleBookError error_code);

static EBookBackendSyncStatus
e_book_backend_google_create_contact (EBookBackendSync *backend,
                                      EDataBook        *book,
                                      guint32           opid,
                                      const char       *vcard_str,
                                      EContact        **out_contact)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    EContact *contact;
    GError *error = NULL;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    __debug__ ("Creating: %s", vcard_str);
    *out_contact = NULL;

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }

    contact = e_contact_new_from_vcard (vcard_str);
    google_book_add_contact (priv->book, contact, out_contact, &error);
    g_object_unref (contact);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Creating contact failed: %s", error->message);
        g_clear_error (&error);
        *out_contact = NULL;
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

    for (id_iter = id_list; id_iter; id_iter = id_iter->next) {
        GError *error = NULL;
        const char *uid;

        uid = id_iter->data;
        google_book_remove_contact (priv->book, uid, &error);
        if (error) {
            /* Only last error will be reported */
            status = e_book_backend_status_from_google_book_error (error->code);
            __debug__ ("Deleting contact %s failed: %s", uid, error->message);
            g_clear_error (&error);
        } else {
            *ids = g_list_append (*ids, g_strdup (uid));
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
                                      EContact        **out_contact)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    EContact *contact;
    GError *error = NULL;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    __debug__ ("Updating: %s", vcard_str);
    *out_contact = NULL;

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_OfflineUnavailable;
    }

    contact = e_contact_new_from_vcard (vcard_str);
    google_book_update_contact (priv->book, contact, out_contact, &error);
    g_object_unref (contact);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Modifying contact failed: %s", error->message);
        g_clear_error (&error);
        *out_contact = NULL;
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
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    EContact *contact;
    GError *error = NULL;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    contact = google_book_get_contact (priv->book, uid, &error);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Getting contact with uid %s failed: %s", uid, error->message);
        g_clear_error (&error);
        return status;
    }
    *vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
    g_object_unref (contact);

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
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_OtherError;
    EBookBackendSExp *sexp;
    GError *error = NULL;
    GList *all_contacts;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    *contacts = NULL;

    all_contacts = google_book_get_all_contacts (priv->book, &error);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Getting all contacts failed: %s", error->message);
        g_clear_error (&error);
        return status;
    }

    sexp = e_book_backend_sexp_new (query);
    while (all_contacts) {
        EContact *contact;

        contact = all_contacts->data;
        if (e_book_backend_sexp_match_contact (sexp, contact)) {
            char *vcard_str;
            vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
            *contacts = g_list_append (*contacts, vcard_str);
        }
        g_object_unref (contact);
        all_contacts = g_list_delete_link (all_contacts, all_contacts);
    }
    g_object_unref (sexp);

    return GNOME_Evolution_Addressbook_Success;
}

static void
on_google_book_contact_added (GoogleBook *book, EContact *contact, gpointer user_data)
{
    EBookBackendGooglePrivate *priv;
    GList *iter;

    priv = GET_PRIVATE (user_data);
    for (iter = priv->bookviews; iter; iter = iter->next) {
        g_object_ref (contact);
        e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), contact);
    }
}

static void
on_google_book_contact_removed (GoogleBook *book, const char *uid, gpointer user_data)
{
    EBookBackendGooglePrivate *priv;
    GList *iter;

    priv = GET_PRIVATE (user_data);
    for (iter = priv->bookviews; iter; iter = iter->next) {
        e_data_book_view_notify_remove (E_DATA_BOOK_VIEW (iter->data), g_strdup (uid));
    }
}

static void
on_google_book_contact_changed (GoogleBook *book, EContact *contact, gpointer user_data)
{
    EBookBackendGooglePrivate *priv;
    GList *iter;

    priv = GET_PRIVATE (user_data);
    for (iter = priv->bookviews; iter; iter = iter->next) {
        g_object_ref (contact);
        e_data_book_view_notify_update (E_DATA_BOOK_VIEW (iter->data), contact);
    }
}

static void
on_google_book_sequence_complete (GoogleBook *book, GError *error, gpointer user_data)
{
    EBookBackendGooglePrivate *priv;
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_Success;
    GList *iter;

    priv = GET_PRIVATE (user_data);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Book-view query failed: %s", error->message);
        status = e_book_backend_status_from_google_book_error (error->code);
        g_clear_error (&error);
    }
    for (iter = priv->bookviews; iter; iter = iter->next) {
        e_data_book_view_notify_complete (E_DATA_BOOK_VIEW (iter->data), GNOME_Evolution_Addressbook_Success);
    }
}

static void
e_book_backend_google_start_book_view (EBookBackend  *backend,
                                       EDataBookView *bookview)
{
    EBookBackendGooglePrivate *priv;
    GList *cached_contacts;

    g_return_if_fail (E_IS_BOOK_BACKEND_GOOGLE (backend));
    g_return_if_fail (E_IS_DATA_BOOK_VIEW (bookview));

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    priv->bookviews = g_list_append (priv->bookviews, bookview);

    bonobo_object_ref (bookview);
    e_data_book_view_notify_status_message (bookview, "Loading...");

    google_book_set_live_mode (priv->book, TRUE);
    cached_contacts = google_book_get_all_contacts_in_live_mode (priv->book);
    while (cached_contacts) {
        EContact *contact = cached_contacts->data;

        e_data_book_view_notify_update (bookview, contact);
        g_object_unref (contact);
        cached_contacts = g_list_delete_link (cached_contacts, cached_contacts);
    }
    e_data_book_view_notify_complete (bookview, GNOME_Evolution_Addressbook_Success);
}

static void
e_book_backend_google_stop_book_view (EBookBackend  *backend,
                                      EDataBookView *bookview)
{
    EBookBackendGooglePrivate *priv;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    priv->bookviews = g_list_remove (priv->bookviews, bookview);
    bonobo_object_unref (bookview);

    if (NULL == priv->bookviews) {
        google_book_set_live_mode (priv->book, FALSE);
    }
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
    EBookBackendSyncStatus status = GNOME_Evolution_Addressbook_Success;
    GError *error = NULL;
    char *book_username;
    gboolean match;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (backend);

    if (priv->mode != GNOME_Evolution_Addressbook_MODE_REMOTE) {
        return GNOME_Evolution_Addressbook_Success;
    }

    if (NULL == username || username[0] == 0) {
        return GNOME_Evolution_Addressbook_AuthenticationFailed;
    }

    g_object_get (G_OBJECT (priv->book),
                  "username", &book_username,
                  NULL);

    match = (0 == strcmp (username, book_username));
    g_free (book_username);
    if (FALSE == match) {
        g_warning ("Username given when loading source and on authentication did not match!");
        return GNOME_Evolution_Addressbook_OtherError;
    }

    google_book_connect_to_google (priv->book, password, &error);
    if (error) {
        status = e_book_backend_status_from_google_book_error (error->code);
        __debug__ ("Authentication failed: %s", error->message);
        status = e_book_backend_status_from_google_book_error (error->code);
        g_clear_error (&error);
    } else {
        e_book_backend_notify_writable (E_BOOK_BACKEND (backend), TRUE);
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
    return GNOME_Evolution_Addressbook_Success;
}

static void
on_google_book_auth_required (GoogleBook *book, gpointer user_data)
{
    __debug__ (G_STRFUNC);
    e_book_backend_notify_auth_required (E_BOOK_BACKEND (user_data));
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_google_load_source (EBookBackend *backend,
                                   ESource      *source,
                                   gboolean      only_if_exists)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (backend);
    const char *refresh_interval_str, *use_ssl_str, *use_cache_str;
    guint refresh_interval;
    gboolean use_ssl, use_cache;
    const char *username;

    if (priv->book) {
        g_warning ("Source already loaded!");
        return GNOME_Evolution_Addressbook_OtherError;
    }

    username = e_source_get_property (source, "username");

    if (NULL == username || username[0] == '\0') {
        g_warning ("No or empty username!");
        return GNOME_Evolution_Addressbook_OtherError;
    }

    refresh_interval_str = e_source_get_property (source, "refresh-interval");
    use_ssl_str = e_source_get_property (source, "ssl");
    use_cache_str = e_source_get_property (source, "offline_sync");

    if (refresh_interval_str) {
        if (1 != sscanf (refresh_interval_str, "%u", &refresh_interval)) {
            g_warning ("Could not parse refresh-interval!");
            refresh_interval = 3600;
        }
    }

    use_ssl = TRUE;
    if (use_ssl_str) {
        if (0 == g_ascii_strcasecmp (use_ssl_str, "false") || 0 == strcmp (use_ssl_str, "0"))
            use_ssl = FALSE;
    }

    use_cache = TRUE;
    if (use_cache_str) {
        if (0 == g_ascii_strcasecmp (use_cache_str, "false") || 0 == strcmp (use_cache_str, "0"))
            use_cache = FALSE;
    }

    priv->book = google_book_new (username, use_cache);

    g_object_set (G_OBJECT (priv->book),
                  "refresh-interval", refresh_interval,
                  "use-ssl", use_ssl,
                  NULL);
    g_object_connect (G_OBJECT (priv->book),
                      "signal::contact-added", G_CALLBACK (on_google_book_contact_added), backend,
                      "signal::contact-changed", G_CALLBACK (on_google_book_contact_changed), backend,
                      "signal::contact-removed", G_CALLBACK (on_google_book_contact_removed), backend,
                      "signal::sequence-complete", G_CALLBACK (on_google_book_sequence_complete), backend,
                      "signal::auth-required", G_CALLBACK (on_google_book_auth_required), backend,
                      NULL);

    __debug__ (G_STRFUNC);

    e_book_backend_set_is_loaded (backend, TRUE);
    e_book_backend_notify_connection_status (backend, TRUE);
    e_book_backend_set_is_writable (backend, FALSE);
    google_book_set_offline_mode (priv->book, (priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL));

    return GNOME_Evolution_Addressbook_Success;
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

    if (NULL == priv->book) {
        return;
    }

    if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
        google_book_set_offline_mode (priv->book, FALSE);
    } else {
        google_book_set_offline_mode (priv->book, TRUE);
    }
}

static void
e_book_backend_google_dispose (GObject *object)
{
    EBookBackendGooglePrivate *priv = GET_PRIVATE (object);

    __debug__ (G_STRFUNC);

    while (priv->bookviews) {
        bonobo_object_unref (priv->bookviews->data);
        priv->bookviews = g_list_delete_link (priv->bookviews,
                                              priv->bookviews);
    }
    if (priv->book) {
        g_object_unref (priv->book);
        priv->book = NULL;
    }

    G_OBJECT_CLASS (e_book_backend_google_parent_class)->dispose (object);
}

static void
e_book_backend_google_finalize (GObject *object)
{
    //EBookBackendGooglePrivate *priv = GET_PRIVATE (object);

    __debug__ (G_STRFUNC);

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
e_book_backend_status_from_google_book_error (GoogleBookError error_code)
{
    return GNOME_Evolution_Addressbook_OtherError;
}


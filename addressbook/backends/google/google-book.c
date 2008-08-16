/* goggle-book.c - Google contact list abstraction with caching.
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
#include <libedata-book/e-book-backend-cache.h>
#include <gdata-service-iface.h>
#include <gdata-google-service.h>

#include "util.h"
#include "google-book.h"

G_DEFINE_TYPE (GoogleBook, google_book, G_TYPE_OBJECT)

#define GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_GOOGLE_BOOK, GoogleBookPrivate))

typedef struct _GoogleBookPrivate GoogleBookPrivate;

enum
{
    PROP_NONE,

    PROP_USERNAME,
    PROP_USE_CACHE,
    PROP_REFRESH_INTERVAL,
    PROP_USE_SSL
};

enum
{
    CONTACT_ADDED,
    CONTACT_CHANGED,
    CONTACT_REMOVED,
    SEQUENCE_COMPLETE,
    AUTH_REQUIRED,

    LAST_SIGNAL
};

static guint google_book_signals [LAST_SIGNAL];

typedef enum
{
    NO_CACHE,
    ON_DISK_CACHE,
    IN_MEMORY_CACHE
} CacheType;

struct _GoogleBookPrivate
{
    char *username;
    CacheType cache_type;
    union {
        EBookBackendCache *on_disk;
        struct {
            GHashTable *contacts;
            GHashTable *gdata_entries;
            GTimeVal last_updated;
        } in_memory;
    } cache;

    gboolean offline;
    GDataService *service;
    guint refresh_interval;
    char *base_uri;
    /* FIXME - this one should not be needed */
    char *add_base_uri;

    gboolean live_mode;

    /* In live mode we will send out signals in an idle_handler */
    guint idle_id;

    guint refresh_id;
};

static gboolean
google_book_get_new_contacts_in_chunks (GoogleBook *book,
                                        int         chunk_size,
                                        GError    **error);

static void
google_book_error_from_soup_error      (GError *soup_error,
                                        GError **error,
                                        const char *message);

static void
google_book_cache_init (GoogleBook *book, gboolean on_disk)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    if (on_disk) {
        priv->cache_type = ON_DISK_CACHE;
        priv->cache.on_disk = e_book_backend_cache_new (priv->username);
    } else {
        priv->cache_type = IN_MEMORY_CACHE;
        priv->cache.in_memory.contacts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                g_free, g_object_unref);
        priv->cache.in_memory.gdata_entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                     g_free, g_object_unref);
        memset (&priv->cache.in_memory.last_updated, 0, sizeof (GTimeVal));
    }
}

static EContact*
google_book_cache_add_contact (GoogleBook *book, GDataEntry *entry)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    EContact *contact;
    const char *uid;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        contact = _e_contact_new_from_gdata_entry (entry);
        _e_contact_add_gdata_entry_xml (contact, entry);
        e_book_backend_cache_add_contact (priv->cache.on_disk, contact);
        _e_contact_remove_gdata_entry_xml (contact);
        return contact;
    case IN_MEMORY_CACHE:
        contact = _e_contact_new_from_gdata_entry (entry);
        uid = e_contact_get_const (contact, E_CONTACT_UID);
        g_hash_table_insert (priv->cache.in_memory.contacts,
                             g_strdup (uid), g_object_ref (contact));
        g_hash_table_insert (priv->cache.in_memory.gdata_entries,
                             g_strdup (uid), g_object_ref (entry));
        return contact;
    case NO_CACHE:
        break;
    }
    return NULL;
}

static gboolean
google_book_cache_remove_contact (GoogleBook *book, const char *uid)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    gboolean success = TRUE;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        return e_book_backend_cache_remove_contact (priv->cache.on_disk, uid);
    case IN_MEMORY_CACHE:
        success = g_hash_table_remove (priv->cache.in_memory.contacts, uid);
        return success && g_hash_table_remove (priv->cache.in_memory.gdata_entries, uid);
    case NO_CACHE:
    break;
    }
    return FALSE;
}

static EContact*
google_book_cache_get_contact (GoogleBook *book, const char *uid, GDataEntry **entry)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    EContact *contact;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        contact = e_book_backend_cache_get_contact (priv->cache.on_disk, uid);
        if (contact) {
            if (entry) {
                const char *entry_xml;
                entry_xml = _e_contact_get_gdata_entry_xml (contact);
                *entry = gdata_entry_new_from_xml (entry_xml);
            }
            _e_contact_remove_gdata_entry_xml (contact);
        }
        return contact;
    case IN_MEMORY_CACHE:
        contact = g_hash_table_lookup (priv->cache.in_memory.contacts, uid);
        if (entry) {
            *entry = g_hash_table_lookup (priv->cache.in_memory.gdata_entries, uid);
            if (*entry) {
                g_object_ref (*entry);
            }
        }
        if (contact) {
            g_object_ref (contact);
        }
        return contact;
    case NO_CACHE:
        break;
    }
    return NULL;
}

static GList*
_g_hash_table_to_list (GHashTable *ht)
{
    GList *l = NULL;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, ht);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
        l = g_list_prepend (l, g_object_ref (G_OBJECT (value)));
    }

    l = g_list_reverse (l);

    return l;
}

static GList*
google_book_cache_get_contacts (GoogleBook *book)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    GList *contacts, *iter;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        contacts = e_book_backend_cache_get_contacts (priv->cache.on_disk,
                                                      "(contains \"x-evolution-any-field\" \"\")");
        for (iter = contacts; iter; iter = iter->next) {
            _e_contact_remove_gdata_entry_xml (iter->data);
        }
        return contacts;
    case IN_MEMORY_CACHE:
        return _g_hash_table_to_list (priv->cache.in_memory.contacts);
    case NO_CACHE:
        break;
    }
        return NULL;
}

static void
google_book_cache_freeze (GoogleBook *book)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    if (priv->cache_type == ON_DISK_CACHE) {
        e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache.on_disk));
    }
}

static void
google_book_cache_thaw (GoogleBook *book)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    if (priv->cache_type == ON_DISK_CACHE) {
        e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache.on_disk));
    }
}

static char*
google_book_cache_get_last_update (GoogleBook *book)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        return e_book_backend_cache_get_time (priv->cache.on_disk);
    case IN_MEMORY_CACHE:
        if (priv->cache.in_memory.contacts) {
            return g_time_val_to_iso8601 (&priv->cache.in_memory.last_updated);
        }
        break;
    case NO_CACHE:
        break;
    }
    return NULL;
}

static gboolean
google_book_cache_get_last_update_tv (GoogleBook *book, GTimeVal *tv)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    char *last_update;
    int rv;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        last_update = e_book_backend_cache_get_time (priv->cache.on_disk);
        rv = last_update ? g_time_val_from_iso8601 (last_update, tv) : FALSE;
        g_free (last_update);
        return rv;
    case IN_MEMORY_CACHE:
        memcpy (tv, &priv->cache.in_memory.last_updated, sizeof (GTimeVal));
        return priv->cache.in_memory.contacts != NULL;
    case NO_CACHE:
        break;
    }
    return FALSE;
}

static void
google_book_cache_set_last_update (GoogleBook *book, GTimeVal *tv)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    char *time;

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        time = g_time_val_to_iso8601 (tv);
        /* Work around a bug in EBookBackendCache */
        e_file_cache_remove_object (E_FILE_CACHE (priv->cache.on_disk), "last_update_time");
        e_book_backend_cache_set_time (priv->cache.on_disk, time);
        g_free (time);
        return;
    case IN_MEMORY_CACHE:
        memcpy (&priv->cache.in_memory.last_updated, tv, sizeof (GTimeVal));
    case NO_CACHE:
        break;
    }
}

static gboolean
google_book_cache_needs_update (GoogleBook *book, guint *remaining_secs)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    GTimeVal last, current;
    guint diff;
    gboolean rv;

    if (remaining_secs) {
        *remaining_secs = G_MAXUINT;
    }
    /* We never want to update in offline mode */
    if (priv->offline) {
        return FALSE;
    }

    rv = google_book_cache_get_last_update_tv (book, &last);

    if (FALSE == rv) {
        return TRUE;
    }
    g_get_current_time (&current);
    if (last.tv_sec > current.tv_sec) {
        g_warning ("last update is in the feature?");

        /* Do an update so we can fix this */
        return TRUE;
    }
    diff = current.tv_sec - last.tv_sec;

    if (diff >= priv->refresh_interval) {
        return TRUE;
    }
    if (remaining_secs) {
        *remaining_secs = priv->refresh_interval - diff;
    }
    __debug__ ("No update needed. Next update needed in %d secs", priv->refresh_interval - diff);

    return FALSE;
}

static gboolean on_refresh_timeout (gpointer user_data);

static gboolean
google_book_cache_refresh_if_needed (GoogleBook *book, GError **error)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);
    guint remaining_secs;
    int rv = TRUE;
    gboolean install_timeout;

    __debug__ (G_STRFUNC);

    if (priv->offline || NULL == priv->service) {
        __debug__ ("We are not connected to Google%s.", 
                   priv->offline ? " (offline mode)" : "");
        return TRUE;
    }

    install_timeout = (priv->live_mode) &&
                      (priv->refresh_interval > 0) &&
                      (0 == priv->refresh_id);

    if (google_book_cache_needs_update (book, &remaining_secs)) {
        rv = google_book_get_new_contacts_in_chunks (book, 32, error);
        if (install_timeout) {
            priv->refresh_id =
                g_timeout_add_seconds (priv->refresh_interval,
                                       on_refresh_timeout,
                                       book);
        }
    } else {
        if (install_timeout) {
            __debug__ ("Installing timeout with %d seconds", 
                       remaining_secs);
            priv->refresh_id =
                g_timeout_add_seconds (remaining_secs,
                                       on_refresh_timeout,
                                       book);
        }
    }
    return rv;
}

static gboolean
on_refresh_timeout (gpointer user_data)
{
    GoogleBook *book = user_data;
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);

    priv->refresh_id = 0;
    if (priv->live_mode) {
        google_book_cache_refresh_if_needed (book, NULL);
    }

    return FALSE;
}


static void
google_book_cache_destroy (GoogleBook *book)
{
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    switch (priv->cache_type) {
    case ON_DISK_CACHE:
        g_object_unref (priv->cache.on_disk);
        break;
    case IN_MEMORY_CACHE:
        g_hash_table_destroy (priv->cache.in_memory.contacts);
        g_hash_table_destroy (priv->cache.in_memory.gdata_entries);
        break;
    case NO_CACHE:
        break;
    }
    priv->cache_type = NO_CACHE;
}

static void
google_book_construct_base_uri (GoogleBook *book, gboolean use_ssl)
{
    const char *format = "%swww.google.com/m8/feeds/contacts/%s/base";
    char *esc_username;
    GoogleBookPrivate *priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_free (priv->base_uri);
    g_free (priv->add_base_uri);

    esc_username = g_uri_escape_string (priv->username, NULL, FALSE);
    priv->base_uri = g_strdup_printf (format, use_ssl ? "https://" : "http://", esc_username);
    /* FIXME - always use non ssl mode when adding entries. Somehow this does not
     * work on SSL; i.e. get duplicate entries and SOUP returns error 7 - connection
     * terminated unexpectedly
     */
    priv->add_base_uri = g_strdup_printf (format, "http://", esc_username);
    g_free (esc_username);
}

static void
google_book_get_property (GObject *object, guint property_id,
                          GValue *value,   GParamSpec *pspec)
{
    GoogleBookPrivate *priv = GET_PRIVATE (object);
    gboolean use_ssl = FALSE;

    switch (property_id) {
    case PROP_USERNAME:
        g_value_set_string (value, priv->username);
        break;
    case PROP_USE_CACHE:
        g_value_set_boolean (value, (priv->cache_type == ON_DISK_CACHE));
        break;
    case PROP_REFRESH_INTERVAL:
        g_value_set_uint (value, priv->refresh_interval);
        break;
    case PROP_USE_SSL:
        if (priv->base_uri) {
            if (strstr (priv->base_uri, "https://")) {
                use_ssl = TRUE;
            }
        }
        g_value_set_boolean (value, use_ssl);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
google_book_set_property (GObject *object, guint property_id,
                          const GValue *value, GParamSpec *pspec)
{
    GoogleBookPrivate *priv = GET_PRIVATE (object);

    switch (property_id) {
    case PROP_USERNAME:
        priv->username = g_value_dup_string (value);
        break;
    case PROP_USE_CACHE:
        google_book_cache_init (GOOGLE_BOOK (object), g_value_get_boolean (value));
        break;
    case PROP_REFRESH_INTERVAL:
        priv->refresh_interval = g_value_get_uint (value);
        /* FIXME - actually apply this */
        break;
    case PROP_USE_SSL:
        google_book_construct_base_uri (GOOGLE_BOOK (object), g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
google_book_dispose (GObject *object)
{
    GoogleBookPrivate *priv = GET_PRIVATE (object);

    if (priv->idle_id) {
        g_source_remove (priv->idle_id);
        priv->idle_id = 0;
    }

    if (priv->service) {
        g_object_unref (priv->service);
        priv->service = NULL;
    }
    google_book_cache_destroy (GOOGLE_BOOK (object));

    if (G_OBJECT_CLASS (google_book_parent_class)->dispose)
        G_OBJECT_CLASS (google_book_parent_class)->dispose (object);
}

static void
google_book_finalize (GObject *object)
{
    GoogleBookPrivate *priv = GET_PRIVATE (object);

    g_free (priv->base_uri);
    g_free (priv->add_base_uri);
    g_free (priv->username);

    if (G_OBJECT_CLASS (google_book_parent_class)->finalize)
        G_OBJECT_CLASS (google_book_parent_class)->finalize (object);
}

static void
google_book_emit_contact_added (GoogleBook *book, EContact *contact)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    if (priv->live_mode) {
        g_signal_emit (book, google_book_signals [CONTACT_ADDED], 0, contact);
    }
}

static void
google_book_emit_contact_changed (GoogleBook *book, EContact *contact)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    if (priv->live_mode) {
        g_signal_emit (book, google_book_signals [CONTACT_CHANGED], 0, contact);
    }
}

static void
google_book_emit_contact_removed (GoogleBook *book, const char *uid)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    if (priv->live_mode) {
        g_signal_emit (book, google_book_signals [CONTACT_REMOVED], 0, uid);
    }
}

static void
google_book_emit_sequence_complete (GoogleBook *book, GError *error)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    if (priv->live_mode) {
        g_signal_emit (book, google_book_signals [SEQUENCE_COMPLETE], 0, error);
    }
}

static void
google_book_emit_auth_required (GoogleBook *book)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_signal_emit (book, google_book_signals [AUTH_REQUIRED], 0);
}


static void
google_book_class_init (GoogleBookClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (GoogleBookPrivate));

    object_class->get_property = google_book_get_property;
    object_class->set_property = google_book_set_property;
    object_class->dispose = google_book_dispose;
    object_class->finalize = google_book_finalize;

    g_object_class_install_property (object_class,
                                     PROP_USERNAME,
                                     g_param_spec_string ("username",
                                                          "Username",
                                                          "The username",
                                                          NULL,
                                                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

    g_object_class_install_property (object_class,
                                     PROP_USE_CACHE,
                                     g_param_spec_boolean ("use-cache",
                                                           "UseCache",
                                                           "Whether a on-disk cache should be used",
                                                           TRUE,
                                                           G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_REFRESH_INTERVAL,
                                     g_param_spec_uint ("refresh-interval",
                                                        "RefreshInterval",
                                                        "Specifies the number of seconds until "
                                                        "the local cache is updated from the "
                                                        "server. 0 means no updates.",
                                                        0, G_MAXUINT, 3600,
                                                        G_PARAM_READWRITE));
    g_object_class_install_property (object_class,
                                     PROP_USE_SSL,
                                     g_param_spec_boolean ("use-ssl",
                                                           "UseSSL",
                                                           "Whether SSL should be used or not",
                                                           TRUE,
                                                           G_PARAM_READWRITE));
    google_book_signals [CONTACT_CHANGED] =
        g_signal_new ("contact-changed",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoogleBookClass, contact_changed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

    google_book_signals [CONTACT_ADDED] =
        g_signal_new ("contact-added",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoogleBookClass, contact_added),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

    google_book_signals [CONTACT_REMOVED] =
        g_signal_new ("contact-removed",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoogleBookClass, contact_removed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

    google_book_signals [SEQUENCE_COMPLETE] =
        g_signal_new ("sequence-complete",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoogleBookClass, sequence_complete),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

    google_book_signals [AUTH_REQUIRED] =
        g_signal_new ("auth-required",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoogleBookClass, auth_required),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
google_book_init (GoogleBook *self)
{
    __debug__ (G_STRFUNC);
}

GoogleBook*
google_book_new (const char *username, gboolean use_cache)
{
    return g_object_new (TYPE_GOOGLE_BOOK,
                         "username", username,
                         "use-cache", use_cache,
                         "use-ssl", TRUE,
                         "refresh-interval", 3600,
                         NULL);
}

gboolean
google_book_connect_to_google (GoogleBook *book, const char *password, GError **error)
{
    GoogleBookPrivate *priv;
    GDataService *service;
    GError *soup_error = NULL;

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);
    g_return_val_if_fail (NULL != password,      FALSE);

    priv = GET_PRIVATE (book);

    if (priv->service) {
        g_warning ("Connection to google already established.");
        return TRUE;
    }

    service = (GDataService*)gdata_google_service_new ("cp", "evolution-client-0.0.1");
    gdata_service_set_credentials (GDATA_SERVICE (service), priv->username, password);
    gdata_google_service_authenticate (GDATA_GOOGLE_SERVICE (service), &soup_error);

    if (soup_error) {
        google_book_error_from_soup_error (soup_error, error,
                                           "Connecting to google failed");
        priv->service = NULL;
        return FALSE;
    }
    priv->service = service;

    return google_book_cache_refresh_if_needed (book, error);
}

void
google_book_set_offline_mode (GoogleBook *book, gboolean offline)
{
    GoogleBookPrivate *priv;

    __debug__ (G_STRFUNC);
    g_return_if_fail (IS_GOOGLE_BOOK (book));

    priv = GET_PRIVATE (book);

    priv->offline = offline;
    if (offline && priv->service) {
        g_object_unref (priv->service);
        priv->service = NULL;
    }
    if (offline == FALSE) {
        if (priv->service) {
            google_book_cache_refresh_if_needed (book, NULL);
        } else {
            google_book_emit_auth_required (book);
        }
    }
}

gboolean
google_book_add_contact (GoogleBook *book,
                         EContact   *contact,
                         EContact  **out_contact,
                         GError    **error)
{
    GoogleBookPrivate *priv;
    GDataEntry *entry, *new_entry;
    GError *soup_error = NULL;

    *out_contact = NULL;

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    priv = GET_PRIVATE (book);

    g_return_val_if_fail (priv->service, FALSE);

    entry = _gdata_entry_new_from_e_contact (contact);
    __debug__ ("new entry with xml: %s", gdata_entry_generate_xml (entry));
    new_entry = gdata_service_insert_entry (GDATA_SERVICE (priv->service),
                                            priv->add_base_uri, entry, &soup_error);
    g_object_unref (entry);
    if (soup_error) {
        google_book_error_from_soup_error (soup_error, error,
                                           "Adding entry failed");
        return FALSE;
    }

    *out_contact = google_book_cache_add_contact (book, new_entry);

    g_object_unref (new_entry);

    return TRUE;
}

gboolean
google_book_update_contact (GoogleBook *book,
                            EContact   *contact,
                            EContact  **out_contact,
                            GError    **error)
{
    GoogleBookPrivate *priv;
    GDataEntry *entry, *new_entry;
    GError *soup_error = NULL;
    EContact *cached_contact;
    const char *uid;

    *out_contact = NULL;

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    priv = GET_PRIVATE (book);

    g_return_val_if_fail (priv->service, FALSE);

    uid = e_contact_get (contact, E_CONTACT_UID);

    entry = NULL;
    cached_contact = google_book_cache_get_contact (book, uid, &entry);
    if (NULL == cached_contact) {
        g_set_error (error,
                    GOOGLE_BOOK_ERROR,
                    GOOGLE_BOOK_ERROR_CONTACT_NOT_FOUND,
                    "Contact with uid %s not found in cache.", uid);
        return FALSE;
    }
    g_object_unref (cached_contact);
    _gdata_entry_update_from_e_contact (entry, contact);

    __debug__ ("Before:\n%s", gdata_entry_generate_xml (entry));
    new_entry = gdata_service_update_entry (GDATA_SERVICE (priv->service), entry, &soup_error);
    g_object_unref (entry);

    if (soup_error) {
        google_book_error_from_soup_error (soup_error, error,
                                           "Updating entry failed");
        return FALSE;
    }
    __debug__ ("After:\n%s", new_entry ? gdata_entry_generate_xml (new_entry) : NULL);

    *out_contact = google_book_cache_add_contact (book, new_entry);

    g_object_unref (new_entry);

    return TRUE;
}

gboolean
google_book_remove_contact (GoogleBook *book, const char *uid, GError **error)
{
    GoogleBookPrivate *priv;
    GDataEntry *entry = NULL;
    GError *soup_error = NULL;
    EContact *cached_contact;

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    priv = GET_PRIVATE (book);

    g_return_val_if_fail (priv->service, FALSE);

    cached_contact = google_book_cache_get_contact (book, uid, &entry);
    if (NULL == cached_contact) {
        g_set_error (error,
                    GOOGLE_BOOK_ERROR,
                    GOOGLE_BOOK_ERROR_CONTACT_NOT_FOUND,
                    "Contact with uid %s not found in cache.", uid);
        return FALSE;
    }

    google_book_cache_remove_contact (book, uid);
    gdata_service_delete_entry (GDATA_SERVICE (priv->service), entry, &soup_error);
    g_object_unref (entry);
    g_object_unref (cached_contact);

    if (soup_error) {
        google_book_error_from_soup_error (soup_error, error,
                                           "Removing entry failed");
        return FALSE;
    }

    return TRUE;
}

static void
process_subsequent_entry (gpointer list_data, gpointer user_data)
{
    GoogleBookPrivate *priv;
    GoogleBook *book = user_data;
    GDataEntry *entry;
    EContact *cached_contact;
    gboolean is_deleted;
    const char *uid;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (book);
    entry = GDATA_ENTRY (list_data);
    uid = gdata_entry_get_id (entry);
    is_deleted = gdata_entry_is_deleted (entry);

    cached_contact = google_book_cache_get_contact (book, uid, NULL);
    if (is_deleted) {
        /* Do we have this item in our cache? */
        if (NULL != cached_contact) {
            google_book_cache_remove_contact (book, uid);
            google_book_emit_contact_removed (book, uid);
        }
    } else {
        EContact *contact;

        contact = google_book_cache_add_contact (book, entry);

        if (cached_contact) {
            google_book_emit_contact_changed (book, contact);
        } else {
            google_book_emit_contact_added (book, contact);
        }
        g_object_unref (contact);
    }
    if (cached_contact) {
        g_object_unref (cached_contact);
    }
}

static void
process_initial_entry (gpointer list_data, gpointer user_data)
{
    GoogleBookPrivate *priv;
    GoogleBook *book = user_data;
    GDataEntry *entry;
    const char* uid;
    EContact *contact;

    __debug__ (G_STRFUNC);
    priv = GET_PRIVATE (book);
    entry = GDATA_ENTRY (list_data);
    uid = gdata_entry_get_id (entry);

    contact = google_book_cache_add_contact (book, entry);

    google_book_emit_contact_added (GOOGLE_BOOK (book), contact);
    g_object_unref (contact);
}

static gboolean
google_book_get_new_contacts_in_chunks (GoogleBook *book,
                                        int         chunk_size,
                                        GError    **error)
{
    GoogleBookPrivate *priv;
    int start_index = 1;
    char *last_updated;
    GError *our_error = NULL;
    gboolean rv = TRUE;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (priv->service, FALSE);

    last_updated = google_book_cache_get_last_update (book);

    google_book_cache_freeze (book);

    while (start_index > 0) {
        GDataFeed *feed;
        GSList *entries;
        GString *uri;
        int results;
        GError *soup_error = NULL;

        uri = g_string_new (priv->base_uri);
        g_string_append_printf (uri, "?max-results=%d&start-index=%d",
                                chunk_size, start_index);
        if (last_updated) {
            g_string_append_printf (uri, "&updated-min=%s&showdeleted=true",
                                    last_updated);
        }

        __debug__ ("URI is '%s'", uri->str);
        feed = gdata_service_get_feed (priv->service, uri->str, &soup_error);
        g_string_free (uri, TRUE);

        if (soup_error) {
            google_book_error_from_soup_error (soup_error, &our_error,
                                               "Downloading feed failed");
            google_book_emit_sequence_complete (book, our_error);
            g_propagate_error (error, our_error);

            rv = FALSE;
            goto out;
        }

        entries = gdata_feed_get_entries (feed);
        results = entries ? g_slist_length (entries) : 0;
        __debug__ ("Feed has %d entries", results);

        if (last_updated) {
            g_slist_foreach (entries, process_subsequent_entry, book);
        } else {
            g_slist_foreach (entries, process_initial_entry, book);
        }

        if (results == chunk_size) {
            start_index += results;
        } else {
            GTimeVal current_time;

            start_index = -1;
            g_get_current_time (&current_time);
            google_book_cache_set_last_update (book, &current_time);
            google_book_emit_sequence_complete (book, NULL);
        }
        g_object_unref (feed);
    }
out:
    g_free (last_updated);
    google_book_cache_thaw (book);

    return rv;
}


EContact*
google_book_get_contact (GoogleBook *book,
                         const char *uid,
                         GError    **error)
{
    GoogleBookPrivate *priv;
    EContact *contact;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    google_book_cache_refresh_if_needed (book, error);

    contact = google_book_cache_get_contact (book, uid, NULL);

    if (contact) {
        if (*error) {
            /* We found the contact, so forget about errors during refresh */
            g_clear_error (error);
        }
        return contact;
    } else {
        if (NULL == *error) {
            g_set_error (error,
                        GOOGLE_BOOK_ERROR,
                        GOOGLE_BOOK_ERROR_CONTACT_NOT_FOUND,
                        "Contact with uid %s not found in cache.", uid);
        }
    }
    return NULL;
}

GList*
google_book_get_all_contacts (GoogleBook *book,
                              GError **error)
{
    GoogleBookPrivate *priv;
    GList *contacts;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    google_book_cache_refresh_if_needed (book, error);

    contacts = google_book_cache_get_contacts (book);

    if (contacts) {
        if (*error) {
            /* We found the contact, so forget about errors during refresh */
            g_clear_error (error);
        }
        return contacts;
    }
    return NULL;
}

static gboolean
on_refresh_idle (gpointer user_data)
{
    GoogleBook *book = user_data;
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    priv->idle_id = 0;

    google_book_cache_refresh_if_needed (book, NULL);

    return FALSE;
}

GList*
google_book_get_all_contacts_in_live_mode (GoogleBook *book)
{
    GoogleBookPrivate *priv;
    gboolean need_update;
    GList *contacts;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

    priv->live_mode = TRUE;

    need_update = google_book_cache_needs_update (book, NULL);

    if (need_update) {
        if (NULL == priv->service) {
            /* We need authorization first */
            google_book_emit_auth_required (book);
        } else {
            priv->idle_id = g_idle_add (on_refresh_idle, book);
        }
    }
    contacts = google_book_cache_get_contacts (book);

    __debug__ ("%d contacts found in cache", g_list_length (contacts));
    return contacts;
}

void
google_book_set_live_mode (GoogleBook *book, gboolean live_mode)
{
    GoogleBookPrivate *priv;

    priv = GET_PRIVATE (book);

    __debug__ (G_STRFUNC);
    priv->live_mode = live_mode;

    if (FALSE == live_mode && priv->refresh_id > 0) {
        g_source_remove (priv->refresh_id);
        priv->refresh_id = 0;
    }
    if (priv->live_mode) {
        google_book_cache_refresh_if_needed (book, NULL);
    }
}

static void
google_book_error_from_soup_error (GError     *soup_error,
                                   GError    **error,
                                   const char *message)
{
    GoogleBookError code;

    g_assert (soup_error);

    if (soup_error->code < 100) {
        code = GOOGLE_BOOK_ERROR_NETWORK_ERROR;
    } else
    if (soup_error->code == 200) {
        code = GOOGLE_BOOK_ERROR_NONE;
    } else
    if (soup_error->code == 400) {
        code = GOOGLE_BOOK_ERROR_INVALID_CONTACT;
    } else
    if (soup_error->code == 401) {
        code = GOOGLE_BOOK_ERROR_AUTH_REQUIRED;
    } else
    if (soup_error->code == 403) {
        code = GOOGLE_BOOK_ERROR_AUTH_FAILED;
    } else
    if (soup_error->code == 404) {
        code = GOOGLE_BOOK_ERROR_CONTACT_NOT_FOUND;
    } else
    if (soup_error->code == 409) {
        code = GOOGLE_BOOK_ERROR_CONFLICT;
    } else {
        code = GOOGLE_BOOK_ERROR_HTTP_ERROR;
    }
    g_set_error (error,
                GOOGLE_BOOK_ERROR,
                GOOGLE_BOOK_ERROR_HTTP_ERROR,
                "%s due to '%s' (HTTP code %d)",
                message ? message : "Action failed",
                soup_error->message,
                soup_error->code);
    g_clear_error (&soup_error);
}


/* goggle-book.h - Google contact list abstraction with caching.
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

#ifndef _GOOGLE_BOOK
#define _GOOGLE_BOOK

#include <libebook/e-contact.h>

G_BEGIN_DECLS

#define TYPE_GOOGLE_BOOK google_book_get_type()

#define GOOGLE_BOOK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GOOGLE_BOOK, GoogleBook))

#define GOOGLE_BOOK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_GOOGLE_BOOK, GoogleBookClass))

#define IS_GOOGLE_BOOK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GOOGLE_BOOK))

#define IS_GOOGLE_BOOK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_GOOGLE_BOOK))

#define GOOGLE_BOOK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_GOOGLE_BOOK, GoogleBookClass))

typedef struct _GoogleBook      GoogleBook;
typedef struct _GoogleBookClass GoogleBookClass;
typedef enum   _GoogleBookError GoogleBookError;

struct _GoogleBook
{
    GObject parent;
};

struct _GoogleBookClass
{
    GObjectClass parent_class;

    void (*contact_added) (EContact* contact);
    void (*contact_changed) (EContact* contact);
    void (*contact_removed) (const char *uid);
    void (*sequence_complete) (GError *error);

    void (*auth_required) (void);
};

enum _GoogleBookError
{
    GOOGLE_BOOK_ERROR_NONE,
    GOOGLE_BOOK_ERROR_CONTACT_NOT_FOUND,
    GOOGLE_BOOK_ERROR_INVALID_CONTACT,
    GOOGLE_BOOK_ERROR_CONFLICT,
    GOOGLE_BOOK_ERROR_AUTH_FAILED,
    GOOGLE_BOOK_ERROR_AUTH_REQUIRED,
    GOOGLE_BOOK_ERROR_NETWORK_ERROR,
    GOOGLE_BOOK_ERROR_HTTP_ERROR
};

#define GOOGLE_BOOK_ERROR (g_quark_from_string ("GoogleBookError"))

typedef void (*GoogleBookContactRetrievedCallback) (EContact *contact, gpointer user_data);

GType google_book_get_type (void);

GoogleBook* google_book_new (const char *username, gboolean use_cache);

gboolean google_book_connect_to_google (GoogleBook *book, const char *password, GError **error);

void google_book_set_offline_mode (GoogleBook *book, gboolean offline);
void google_book_set_live_mode    (GoogleBook *book, gboolean live_mode);

gboolean google_book_add_contact    (GoogleBook *book, EContact *contact, EContact **out_contact, GError **error);
gboolean google_book_update_contact (GoogleBook *book, EContact *contact, EContact **out_contact, GError **error);
gboolean google_book_remove_contact (GoogleBook *book, const char *uid, GError **error);

EContact *google_book_get_contact                   (GoogleBook *book, const char* uid, GError **error);
GList    *google_book_get_all_contacts              (GoogleBook *book, GError **error);
GList    *google_book_get_all_contacts_in_live_mode (GoogleBook *book);

G_END_DECLS

#endif /* _GOOGLE_BOOK */

/*
 * Copyright (C) 2009 Intel Corporation
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
 * Authors: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#ifndef _EBOOK_TEST_UTILS_H
#define _EBOOK_TEST_UTILS_H

#include <libebook/libebook.h>

typedef struct {
        GSourceFunc  cb;
        gpointer     user_data;
	EBookView   *view;
	EList       *list;
} EBookTestClosure;

void
test_print (const gchar *format,
	    ...);
void
ebook_test_utils_read_args (gint argc,
			    gchar **argv);

gboolean
ebook_test_utils_callback_quit (gpointer user_data);

gchar *
ebook_test_utils_new_vcard_from_test_case (const gchar *case_name);

gchar *
ebook_test_utils_book_add_contact_from_test_case_verify (EBook       *book,
                                                         const gchar  *case_name,
							 EContact   **contact);

gboolean
ebook_test_utils_contacts_are_equal_shallow (EContact *a,
                                             EContact *b);

const gchar *
ebook_test_utils_book_add_contact (EBook    *book,
                                   EContact *contact);
void
ebook_test_utils_book_async_add_contact (EBook       *book,
                                         EContact    *contact,
                                         GSourceFunc  callback,
                                         gpointer     user_data);

void
ebook_test_utils_book_commit_contact (EBook    *book,
                                      EContact *contact);
void
ebook_test_utils_book_async_commit_contact (EBook       *book,
                                            EContact    *contact,
                                            GSourceFunc  callback,
                                            gpointer     user_data);

EContact *
ebook_test_utils_book_get_contact (EBook      *book,
                                   const gchar *uid);
void
ebook_test_utils_book_async_get_contact (EBook       *book,
                                         const gchar  *uid,
                                         GSourceFunc  callback,
                                         gpointer     user_data);

GList *
ebook_test_utils_book_get_required_fields (EBook *book);
void
ebook_test_utils_book_async_get_required_fields (EBook       *book,
                                                 GSourceFunc  callback,
                                                 gpointer     user_data);

GList *
ebook_test_utils_book_get_supported_fields (EBook *book);
void
ebook_test_utils_book_async_get_supported_fields (EBook       *book,
						  GSourceFunc  callback,
                                                  gpointer     user_data);

GList *
ebook_test_utils_book_get_supported_auth_methods (EBook *book);
void
ebook_test_utils_book_async_get_supported_auth_methods (EBook       *book,
							GSourceFunc  callback,
							gpointer     user_data);

const gchar *
ebook_test_utils_book_get_static_capabilities (EBook *book);

void
ebook_test_utils_book_remove_contact (EBook      *book,
                                      const gchar *uid);
void
ebook_test_utils_book_async_remove_contact (EBook       *book,
					    EContact    *contact,
					    GSourceFunc  callback,
					    gpointer     user_data);
void
ebook_test_utils_book_async_remove_contact_by_id (EBook       *book,
                                                  const gchar  *uid,
                                                  GSourceFunc  callback,
                                                  gpointer     user_data);

void
ebook_test_utils_book_remove_contacts (EBook *book,
                                       GList *ids);
void
ebook_test_utils_book_async_remove_contacts (EBook       *book,
                                             GList       *uids,
                                             GSourceFunc  callback,
                                             gpointer     user_data);

void
ebook_test_utils_book_get_book_view (EBook       *book,
                                     EBookQuery  *query,
                                     EBookView  **view);
void
ebook_test_utils_book_async_get_book_view (EBook       *book,
                                           EBookQuery  *query,
                                           GSourceFunc  callback,
                                           gpointer     user_data);

#endif /* _EBOOK_TEST_UTILS_H */

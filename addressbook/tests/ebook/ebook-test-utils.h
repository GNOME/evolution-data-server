/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#ifndef _EBOOK_TEST_UTILS_H
#define _EBOOK_TEST_UTILS_H

#include <glib.h>
#include <libebook/e-book.h>

#define EBOOK_TEST_UTILS_DATA_DIR "data"
#define EBOOK_TEST_UTILS_VCARDS_DIR "vcards"

typedef struct {
        GSourceFunc  cb;
        gpointer     user_data;
	EBookView   *view;
	EList       *list;
} EBookTestClosure;

void
test_print (const char *format,
	    ...);

gboolean
ebook_test_utils_callback_quit (gpointer user_data);

char*
ebook_test_utils_new_vcard_from_test_case (const char *case_name);

char*
ebook_test_utils_book_add_contact_from_test_case_verify (EBook       *book,
                                                         const char  *case_name,
							 EContact   **contact);

gboolean
ebook_test_utils_contacts_are_equal_shallow (EContact *a,
                                             EContact *b);

EBook*
ebook_test_utils_book_new_from_uri (const char *uri);

EBook*
ebook_test_utils_book_new_temp (char **uri);

const char*
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

EContact*
ebook_test_utils_book_get_contact (EBook      *book,
                                   const char *uid);
void
ebook_test_utils_book_async_get_contact (EBook       *book,
                                         const char  *uid,
                                         GSourceFunc  callback,
                                         gpointer     user_data);

GList*
ebook_test_utils_book_get_required_fields (EBook *book);
void
ebook_test_utils_book_async_get_required_fields (EBook       *book,
                                                 GSourceFunc  callback,
                                                 gpointer     user_data);

GList*
ebook_test_utils_book_get_supported_fields (EBook *book);
void
ebook_test_utils_book_async_get_supported_fields (EBook       *book,
						  GSourceFunc  callback,
                                                  gpointer     user_data);

GList*
ebook_test_utils_book_get_supported_auth_methods (EBook *book);
void
ebook_test_utils_book_async_get_supported_auth_methods (EBook       *book,
							GSourceFunc  callback,
							gpointer     user_data);

const char*
ebook_test_utils_book_get_static_capabilities (EBook *book);

void
ebook_test_utils_book_remove_contact (EBook      *book,
                                      const char *uid);
void
ebook_test_utils_book_async_remove_contact (EBook       *book,
					    EContact    *contact,
					    GSourceFunc  callback,
					    gpointer     user_data);
void
ebook_test_utils_book_async_remove_contact_by_id (EBook       *book,
                                                  const char  *uid,
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
ebook_test_utils_book_open (EBook    *book,
                            gboolean  only_if_exists);

void
ebook_test_utils_book_remove (EBook *book);
void
ebook_test_utils_book_async_remove (EBook          *book,
                                    GSourceFunc     callback,
                                    gpointer        user_data);

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

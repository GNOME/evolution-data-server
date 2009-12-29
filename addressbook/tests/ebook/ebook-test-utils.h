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

#include <libebook/e-book.h>

typedef struct {
        GSourceFunc    cb;
        gpointer       user_data;
} EBookTestClosure;

EBook* ebook_test_utils_book_new_temp (char **uri);
void   ebook_test_utils_book_open (EBook *book, gboolean only_if_exists);
void   ebook_test_utils_book_remove (EBook *book);
void   ebook_test_utils_book_async_remove (EBook          *book,
                                           GSourceFunc     callback,
                                           gpointer        user_data);

#endif /* _EBOOK_TEST_UTILS_H */

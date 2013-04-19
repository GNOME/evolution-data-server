/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013, Openismus GmbH
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef DATA_TEST_UTILS_H
#define DATA_TEST_UTILS_H

#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>
#include "e-test-server-utils.h"

#define SQLITEDB_FOLDER_ID   "folder_id"

typedef struct {
	ETestServerFixture parent_fixture;

	EBookBackendSqliteDB *ebsdb;
} ESqliteDBFixture;

void     e_sqlitedb_fixture_setup          (ESqliteDBFixture *fixture,
					    gconstpointer     user_data);
void     e_sqlitedb_fixture_teardown       (ESqliteDBFixture *fixture,
					    gconstpointer     user_data);


gchar    *new_vcard_from_test_case         (const gchar *case_name);
EContact *new_contact_from_test_case       (const gchar *case_name);

gboolean add_contact_from_test_case_verify (EBookClient *book_client,
					    const gchar *case_name,
					    EContact   **contact);

void     assert_contacts_order             (GSList      *results,
					    const gchar *first_uid,
					    ...) G_GNUC_NULL_TERMINATED;

void     print_results                     (GSList      *results);

#endif /* DATA_TEST_UTILS_H */
